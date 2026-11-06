# SPDX-License-Identifier: MIT OR Apache-2.0
"""Human/CI report wrapper around CTest, never a second scheduler."""
import argparse
from dataclasses import asdict
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
import uuid
import xml.etree.ElementTree as ET

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "testing/support"))
from process import run


def source_identity(root, report):
    def git(*arguments):
        return subprocess.check_output(["git", "-C", str(root), *arguments], timeout=30)
    commit = git("rev-parse", "HEAD").decode().strip()
    dirty = bool(git("status", "--porcelain", "--untracked-files=all"))
    names = git("ls-files", "-z", "--cached", "--others", "--exclude-standard").split(b"\0")
    manifest = []
    for raw in sorted(set(names)):
        if not raw:
            continue
        relative = Path(os.fsdecode(raw))
        if relative.parts[0] in ("out", ".vs", ".git"):
            continue
        path = root / relative
        if not path.is_file():
            manifest.append({"path": str(relative), "deleted": True})
            continue
        content = path.read_bytes()
        manifest.append({"path": str(relative), "sha256": hashlib.sha256(content).hexdigest()})
        if dirty:
            destination = report / "source" / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(content)
    (report / "source-manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    if dirty:
        (report / "tracked.patch").write_bytes(git("diff", "--binary", "HEAD"))
    return {"commit": commit, "dirty": dirty, "manifest": "source-manifest.json",
            "snapshot": "source" if dirty else None}


def validate_inventory(expected, registered, label, frontend_only):
    actual = {}
    for test in registered:
        properties = {prop["name"]: prop["value"] for prop in test.get("properties", [])}
        if properties.get("DISABLED"):
            raise ValueError("unexpected disabled test: " + test["name"])
        actual[test["name"]] = set(properties.get("LABELS", []))
    if len(registered) != len(actual):
        raise ValueError("duplicate CTest registration identity")
    selected = {name: labels for name, labels in expected.items() if not label or label in labels}
    if not label:
        required = {"unit", "integration", "infrastructure"}
        if not frontend_only:
            required |= {"regression", "e2e"}
        categories = {name.split(".", 1)[0] for name in expected}
        if not required <= categories:
            raise ValueError(f"missing required categories: {required - categories}")
    if not selected or set(actual) != set(selected):
        raise ValueError(f"registration mismatch: missing={set(selected) - set(actual)}, extra={set(actual) - set(selected)}")
    for name, labels in selected.items():
        if actual[name] != labels:
            raise ValueError(f"incorrect labels: {name}: {actual[name]} != {labels}")
    return actual


def junit_counts(cases, actual):
    names = [case.attrib["name"] for case in cases]
    if len(names) != len(set(names)) or set(names) != set(actual):
        raise ValueError("JUnit did not execute exactly the selected cases")
    skipped = [case for case in cases if case.find("skipped") is not None or case.get("status") == "notrun"]
    failed = [case for case in cases if case.find("failure") is not None or case.find("error") is not None]
    return {"registered": len(actual), "executed": len(cases) - len(skipped),
            "skipped": len(skipped), "failed": len(failed),
            "passed": len(cases) - len(skipped) - len(failed)}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--report-dir", type=Path, required=True)
    parser.add_argument("--configuration", required=True)
    parser.add_argument("--label", choices=("unit", "regression", "integration", "e2e", "infrastructure", "frontend", "native", "toolchain"))
    parser.add_argument("--parallel", type=int, default=2)
    args = parser.parse_args()
    if args.parallel < 1 or not re.fullmatch(r"[A-Za-z0-9_-]+", args.configuration):
        parser.error("positive parallelism and a simple configuration name are required")
    root = Path(__file__).resolve().parents[1]
    build = args.build_dir.resolve()
    report = args.report_dir.resolve() / args.configuration / (time.strftime("%Y%m%dT%H%M%SZ", time.gmtime()) + "-" + uuid.uuid4().hex[:8])
    try:
        report.mkdir(parents=True)
    except OSError as error:
        print(f"cannot create report: {error}", file=sys.stderr)
        return 2
    summary = {"configuration": args.configuration, "selection": args.label,
               "started_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
               "status": "infrastructure_error", "report": str(report),
               "steps": {"discovery": {"executed": False}, "tests": {"executed": False}},
               "external_steps": {name: "not executed by this wrapper; see separate CI/human logs"
                                  for name in ("prepare", "configure", "build", "format", "architecture", "tidy")}}

    def record_step(name, observed):
        record = asdict(observed)
        record.pop("stdout")
        record.pop("stderr")
        summary["steps"][name] = dict(record, executed=True)

    try:
        summary["source"] = source_identity(root, report)
        summary["tools"] = {name: subprocess.check_output([name, "--version"], timeout=15).decode("utf-8", "replace").splitlines()[0]
                            for name in ("cmake", "ctest", "ninja")}
        summary["tools"]["python"] = sys.version
        cache_lines = (build / "CMakeCache.txt").read_text(encoding="utf-8").splitlines()
        cache = {}
        for line in cache_lines:
            if line and not line.startswith(("#", "//")) and ":" in line and "=" in line:
                key, value = line.split("=", 1)
                cache[key.split(":", 1)[0]] = value
        summary["build"] = {key: cache.get(key) for key in
                            ("CMAKE_BUILD_TYPE", "CMAKE_CXX_COMPILER", "LLVM_DIR",
                             "CHTHOLLY_FRONTEND_ONLY", "CHTHOLLY_SANITIZER")}
        compiler = cache.get("CMAKE_CXX_COMPILER")
        if compiler:
            command = [compiler] if Path(compiler).name.lower() == "cl.exe" else [compiler, "--version"]
            compiler_version = subprocess.run(command, capture_output=True, timeout=15)
            summary["tools"]["host_compiler"] = (compiler_version.stdout + compiler_version.stderr).decode("utf-8", "replace")[:2048]
        llvm_dir = cache.get("LLVM_DIR")
        if llvm_dir and cache.get("CHTHOLLY_FRONTEND_ONLY") != "ON":
            llvm_bin = (Path(llvm_dir) / "../../../bin").resolve()
            summary["tools"]["clang_cl"] = subprocess.check_output([str(llvm_bin / "clang-cl.exe"), "--version"], timeout=15).decode("utf-8", "replace")
            summary["tools"]["lld_link"] = subprocess.check_output([str(llvm_bin / "lld-link.exe"), "--version"], timeout=15).decode("utf-8", "replace")
        summary["rules"] = "R3 aggregate values, checked Copy, local initialization/moves, patterns, loops, mixed positional/label calls; inherited R2 numeric decisions"
        dependency_lock = json.loads((root / "cmake/dependencies.json").read_text(encoding="utf-8"))
        summary["numeric_library"] = dependency_lock["llvm_source"]
        summary["coverage_boundaries"] = {
            "llvm_support": "external uninstrumented 18.1.8 library",
            "target_runtime": "uninstrumented Windows clang-cl /MD; separate target execution cases",
            "frontend_target": "Windows x64 even when host is Linux"}
        runtime = build / "bin/lib/chtholly-runtime.lib"
        if runtime.is_file():
            summary["runtime"] = {"sha256": hashlib.sha256(runtime.read_bytes()).hexdigest(),
                                  "metadata": (runtime.parent / "chtholly-runtime.meta").read_text(encoding="utf-8")}
        expected = {}
        for line in (build / "testing/expected-tests.txt").read_text(encoding="utf-8").splitlines():
            name, labels = line.split("|", 1)
            if name in expected:
                raise ValueError("duplicate expected test identity: " + name)
            expected[name] = set(labels.split(","))
        selection = ["-L", args.label] if args.label else []
        inventory_command = ["ctest", "--test-dir", str(build), "--show-only=json-v1", *selection]
        summary["steps"]["discovery"] = {"executed": True, "argv": inventory_command, "status": "interrupted_or_incomplete"}
        inventory = run(inventory_command, root, timeout=180)
        record_step("discovery", inventory)
        (report / "discovery.stdout.txt").write_bytes(inventory.stdout)
        (report / "discovery.stderr.txt").write_bytes(inventory.stderr)
        if inventory.failure == "cancelled":
            summary["status"] = "cancelled"
            return 2
        if inventory.failure or inventory.returncode != 0:
            raise ValueError("CTest discovery failed")
        registered = json.loads(inventory.stdout)["tests"]
        actual = validate_inventory(expected, registered, args.label, cache.get("CHTHOLLY_FRONTEND_ONLY") == "ON")
        summary["expected_tests"] = {name: sorted(labels) for name, labels in actual.items()}
        environment = dict(os.environ, CHTHOLLY_REPORT_DIR=str(report / "cases"))
        execution_command = ["ctest", "--test-dir", str(build), "--output-on-failure", "--no-tests=error",
                             "--parallel", str(args.parallel), "--output-junit", str(report / "junit.xml"), *selection]
        summary["steps"]["tests"] = {"executed": True, "argv": execution_command, "status": "interrupted_or_incomplete"}
        execution = run(execution_command, root, timeout=1800, output_limit=32 * 1024 * 1024, env=environment)
        record_step("tests", execution)
        (report / "ctest.stdout.txt").write_bytes(execution.stdout)
        (report / "ctest.stderr.txt").write_bytes(execution.stderr)
        (report / "ctest.log").write_bytes(execution.stdout + execution.stderr)
        summary["command"] = execution.argv
        summary["duration_seconds"] = execution.duration
        if execution.failure == "cancelled":
            summary["status"] = "cancelled"
            return 2
        cases = ET.parse(report / "junit.xml").getroot().findall(".//testcase")
        summary.update(junit_counts(cases, actual))
        # JUnit is the sole total. Per-source-case reasons are supplementary,
        # not additional tests to add to the pass count.
        details = {}
        component_details = {}
        for file in (report / "cases").rglob("result.json"):
            detail = json.loads(file.read_text(encoding="utf-8"))
            if isinstance(detail, dict) and "case" in detail:
                component_details["integration.numeric." + detail["case"]] = {
                    "status": detail["status"], "error": detail.get("error"),
                    "inputs": detail.get("inputs"), "expectation": detail.get("expectation"),
                    "report": str(file.relative_to(report))}
            if isinstance(detail, dict) and "name" in detail:
                details[detail["name"]] = {
                    "status": detail["status"], "error": detail.get("error"),
                    "stage_failures": {stage: detail[stage].get("failure") for stage in ("compile", "run")
                                       if detail[stage].get("executed")}}
        summary["source_case_details"] = details
        summary["native_component_details"] = component_details
        if execution.failure or execution.returncode or summary["skipped"] or summary["failed"]:
            summary["status"] = "failed"
            return 1
        summary["status"] = "passed"
        return 0
    except (OSError, ValueError, KeyError, IndexError, subprocess.CalledProcessError, subprocess.TimeoutExpired, ET.ParseError) as error:
        summary["error"] = str(error)
        print(error, file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        summary["status"] = "cancelled"
        summary["error"] = "CTest cancelled; child tree cleanup requested"
        return 2
    finally:
        try:
            last_log = build / "Testing/Temporary/LastTest.log"
            if summary["steps"]["tests"]["executed"] and last_log.is_file():
                shutil.copyfile(last_log, report / "ctest.raw.log")
            (report / "run.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
            print(f"Report: {report}")
        except OSError as error:
            print(f"cannot save complete report: {error}", file=sys.stderr)
            return 2


if __name__ == "__main__":
    sys.exit(main())
