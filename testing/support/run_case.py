# SPDX-License-Identifier: MIT OR Apache-2.0
"""One explicitly registered source case; CTest owns selection and scheduling."""
from __future__ import annotations

import argparse
from dataclasses import asdict
import hashlib
import json
import os
from pathlib import Path, PureWindowsPath
import re
import shutil
import sys
import tempfile
from typing import Any

from process import run

FAILURE_CATEGORIES = {"Arithmetic.Overflow", "Arithmetic.DivisionByZero", "Arithmetic.ShiftCount",
                      "Conversion.OutOfRange", "Conversion.PrecisionLoss", "Conversion.NonFinite",
                      "Conversion.Fractional", "Panic", "Assertion", "Index.OutOfBounds"}
RUN_KINDS = ("run", "run_fail", "run_abort")


class ConfigurationError(ValueError):
    pass


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ConfigurationError("duplicate manifest field: " + key)
        result[key] = value
    return result


def fields(value: Any, required: set[str], optional: set[str], where: str) -> None:
    if not isinstance(value, dict) or not required <= value.keys() or value.keys() - required - optional:
        raise ConfigurationError(f"{where}: missing or unknown fields")


def stream_rule(value: Any) -> None:
    if not isinstance(value, dict) or len(value) != 1:
        raise ConfigurationError("output rule must contain exact or contains")
    if "exact" in value and isinstance(value["exact"], str):
        return
    if ("contains" in value and isinstance(value["contains"], list) and value["contains"]
            and all(isinstance(x, str) and x for x in value["contains"])):
        return
    raise ConfigurationError("invalid output rule")


def validate(data: Any) -> dict:
    fields(data, {"schema_version", "name", "kind", "source", "compile", "basis"}, {"run"}, "case")
    if type(data["schema_version"]) is not int or data["schema_version"] != 2:
        raise ConfigurationError("unsupported schema_version")
    if not isinstance(data["name"], str) or not re.fullmatch(r"(?:regression|e2e)\.[a-z0-9_]+\.[a-z0-9_]+", data["name"]):
        raise ConfigurationError("invalid case name")
    if data["kind"] not in ("compile", "compile_fail", *RUN_KINDS):
        raise ConfigurationError("unsupported kind")
    if not isinstance(data["basis"], str) or not data["basis"].strip():
        raise ConfigurationError("basis is required")
    if (not isinstance(data["source"], str) or not data["source"]
            or Path(data["source"]).is_absolute() or PureWindowsPath(data["source"]).anchor):
        raise ConfigurationError("source must be relative to the manifest")
    if ("run" in data) != (data["kind"] in RUN_KINDS):
        raise ConfigurationError("run kinds require exactly one run stage")
    for name in ("compile", "run"):
        if name not in data:
            continue
        stage = data[name]
        fields(stage, {"exit_code"}, {"timeout_seconds", "stdout", "stderr", "failure"} if name == "run" else {"timeout_seconds", "stdout", "stderr"}, name)
        if type(stage["exit_code"]) is not int or not 0 <= stage["exit_code"] <= 2147483647:
            raise ConfigurationError("exit_code must be a normal nonnegative integer")
        timeout = stage.get("timeout_seconds", 30 if name == "compile" else 5)
        if type(timeout) not in (int, float) or not 0 < timeout <= (30 if name == "compile" else 5):
            raise ConfigurationError("stage timeout exceeds the registered CTest budget")
        for stream in ("stdout", "stderr"):
            if stream in stage:
                stream_rule(stage[stream])
    expected = data["compile"]["exit_code"]
    if data["kind"] == "compile_fail":
        if expected != 1 or "stderr" not in data["compile"]:
            raise ConfigurationError("compile_fail requires exit_code=1 and a diagnostic expectation")
        diagnostic = data["compile"]["stderr"]
        parts = [diagnostic["exact"]] if "exact" in diagnostic else diagnostic["contains"]
        has_position = any(re.search(r"[^\s:]+:\d+:\d+: error:", part) for part in parts)
        has_reason = any(re.sub(r"^.*:\d+:\d+: error:\s*", "", part).strip() for part in parts)
        if not has_position or not has_reason:
            raise ConfigurationError("source failures require file/line/column and reason matching")
    elif expected != 0:
        raise ConfigurationError("successful compilation requires exit_code=0")
    if data["kind"] == "run_fail":
        stage = data["run"]
        if stage["exit_code"] != 120 or "failure" not in stage:
            raise ConfigurationError("run_fail requires exit_code=120 and structured failure")
        failure = stage["failure"]
        fields(failure, {"category", "file", "line", "column"}, set(), "run.failure")
        if not isinstance(failure["category"], str) or failure["category"] not in FAILURE_CATEGORIES:
            raise ConfigurationError("unknown language failure category")
        if failure["file"] != Path(data["source"]).name:
            raise ConfigurationError("run.failure.file must identify the case source filename")
        if any(type(failure[key]) is not int or failure[key] < 1 for key in ("line", "column")):
            raise ConfigurationError("language failure requires positive line and column")
    elif "run" in data and "failure" in data["run"]:
        raise ConfigurationError("only run_fail accepts a failure expectation")
    if data["kind"] == "run_abort":
        if data["run"]["exit_code"] != 121:
            raise ConfigurationError("run_abort requires exit_code=121")
        if any(data["run"].get(stream, {"exact": ""}) != {"exact": ""} for stream in ("stdout", "stderr")):
            raise ConfigurationError("abort must not emit a language diagnostic or normal output")
    return data


def normalize(text: str, work: Path) -> str:
    return text.replace("\r\n", "\n").replace(str(work), "<WORK>")


def matches(text: str, rule: dict) -> bool:
    return text == rule["exact"] if "exact" in rule else all(part in text for part in rule["contains"])


def execute_case(compiler: Path, manifest: Path, work_dir: Path, report_dir: Path) -> int:
    report = None
    result: dict = {"status": "infrastructure_error", "compile": {"executed": False, "reason": "not reached"},
                    "run": {"executed": False, "reason": "not reached"}, "manifest": str(manifest)}
    code = 2
    try:
        report_dir.mkdir(parents=True, exist_ok=True)
        report = Path(tempfile.mkdtemp(prefix="case-", dir=report_dir))
        manifest_bytes = manifest.read_bytes()
        result["manifest_sha256"] = hashlib.sha256(manifest_bytes).hexdigest()
        data = validate(json.loads(manifest_bytes.decode("utf-8"), object_pairs_hook=unique_object))
        result["name"] = data["name"]
        if data["kind"] not in RUN_KINDS:
            result["run"]["reason"] = "case has no run stage"
        if not compiler.is_file():
            raise ConfigurationError(f"compiler missing: {compiler}")
        source = (manifest.parent / data["source"]).resolve()
        if not source.is_file():
            raise ConfigurationError(f"source missing: {source}")
        work_dir.mkdir(parents=True, exist_ok=True)
        work = Path(tempfile.mkdtemp(prefix=data["name"] + "-", dir=work_dir)).resolve()
        (work / "input").mkdir()
        (work / "output").mkdir()
        source_copy = work / "input" / source.name
        shutil.copyfile(source, source_copy)
        output = work / "output/program.exe"
        result.update(work=str(work), manifest=str(manifest), basis=data["basis"],
                      compiler=str(compiler), compiler_sha256=hashlib.sha256(compiler.read_bytes()).hexdigest(),
                      source_sha256=hashlib.sha256(source_copy.read_bytes()).hexdigest())
        for name in ("compile", "run"):
            if name == "run" and data["kind"] not in RUN_KINDS:
                break
            spec = data[name]
            argv = [str(compiler), str(source_copy), "-o", str(output)] if name == "compile" else [str(output)]
            result[name] = {"executed": True, "argv": argv, "cwd": str(work), "failure": "interrupted_or_incomplete"}
            observed = run(argv, work, spec.get("timeout_seconds", 30 if name == "compile" else 5))
            record = asdict(observed)
            record.pop("stdout")
            record.pop("stderr")
            record["executed"] = True
            result[name] = record
            for stream in ("stdout", "stderr"):
                (report / f"{name}.{stream}.txt").write_bytes(getattr(observed, stream))
            if any(marker in observed.stderr for marker in
                   (b"AddressSanitizer", b"UndefinedBehaviorSanitizer", b"runtime error:", b"Assertion failed")):
                code = 1
                raise RuntimeError(f"{name}: sanitizer or assertion report")
            if observed.failure:
                infrastructure = observed.failure.startswith(("process_error", "process_cleanup_error", "pipe_"))
                infrastructure |= name == "compile" and observed.failure.startswith("start_error")
                infrastructure |= observed.failure == "cancelled"
                code = 2 if infrastructure else 1
                raise RuntimeError(f"{name}: {observed.failure}")
            code = 1
            if observed.returncode != spec["exit_code"]:
                raise RuntimeError(f"{name}: expected {spec['exit_code']}, got {observed.returncode}")
            for stream in ("stdout", "stderr"):
                try:
                    text = normalize(getattr(observed, stream).decode("utf-8"), work)
                except UnicodeDecodeError as error:
                    raise RuntimeError(f"{name}: {stream} is not UTF-8") from error
                if name == "run" and data["kind"] == "run_fail" and stream == "stderr":
                    failure = spec["failure"]
                    expected_diagnostic = (f"{source_copy}:{failure['line']}:{failure['column']}: "
                                           f"chtholly failure[{failure['category']}]\n")
                    if text != normalize(expected_diagnostic, work):
                        raise RuntimeError("run: language failure identity or location mismatch")
                    result["run"]["language_failure"] = failure
                    rule = spec.get(stream, {"exact": normalize(expected_diagnostic, work)})
                else:
                    rule = spec.get(stream, {"exact": ""})
                if not matches(text, rule):
                    raise RuntimeError(f"{name}: {stream} mismatch")
            if name == "compile":
                if data["kind"] == "compile_fail":
                    if output.exists():
                        raise RuntimeError("failed compilation published an executable")
                elif not output.is_file() or output.stat().st_size == 0:
                    raise RuntimeError("successful compilation produced no executable")
                else:
                    result["output_sha256"] = hashlib.sha256(output.read_bytes()).hexdigest()
                    runtime = compiler.parent / "lib/chtholly-runtime.lib"
                    metadata = compiler.parent / "lib/chtholly-runtime.meta"
                    if runtime.is_file() and metadata.is_file():
                        result["runtime"] = {"archive_sha256": hashlib.sha256(runtime.read_bytes()).hexdigest(),
                                             "metadata": metadata.read_text(encoding="utf-8")}
                    else:
                        result["runtime"] = {"recorded": False, "reason": "compiler fixture has no runtime package"}
        result["status"] = "passed"
        code = 0
    except (ConfigurationError, OSError, ValueError) as error:
        code = 2
        result["status"] = "infrastructure_error"
        result["error"] = str(error)
    except RuntimeError as error:
        result["status"] = "failed" if code == 1 else "infrastructure_error"
        if any(result[stage].get("failure") == "cancelled" for stage in ("compile", "run")):
            result["status"] = "cancelled"
        result["error"] = str(error)
    except KeyboardInterrupt:
        code = 2
        result["status"] = "cancelled"
        result["error"] = "process execution cancelled; child tree cleanup requested"
    finally:
        result["runner_exit_code"] = code
        if report is not None:
            try:
                (report / "result.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
            except OSError as error:
                print(f"cannot write result: {error}", file=sys.stderr)
                code = 2
                result["status"] = "infrastructure_error"
                result["error"] = f"cannot write result: {error}"
        result["runner_exit_code"] = code
        print(json.dumps(result), file=sys.stdout if code == 0 else sys.stderr)
    return code


def main() -> int:
    parser = argparse.ArgumentParser()
    for flag in ("compiler", "manifest", "work-dir", "report-dir"):
        parser.add_argument("--" + flag, required=True, type=Path)
    args = parser.parse_args()
    reports = Path(os.environ.get("CHTHOLLY_REPORT_DIR", str(args.report_dir)))
    return execute_case(args.compiler.resolve(), args.manifest.resolve(), args.work_dir, reports)


if __name__ == "__main__":
    sys.exit(main())
