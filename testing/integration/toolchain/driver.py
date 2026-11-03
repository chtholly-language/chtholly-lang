# SPDX-License-Identifier: MIT OR Apache-2.0
"""Stateful tool and publication checks; no production fault injection."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "support"))
from process import run


def main():
    parser = argparse.ArgumentParser()
    for name in ("compiler", "fixture", "work-dir"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    args.work_dir.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="toolchain-", dir=args.work_dir)).resolve()
    compiler = str(args.compiler.resolve())
    fixture = args.fixture.resolve()
    records = []

    def invoke(arguments, expected, reason="", env=None):
        observed = run([compiler, *map(str, arguments)], root, timeout=30, env=env)
        index = len(records)
        (root / f"{index}.stdout.txt").write_bytes(observed.stdout)
        (root / f"{index}.stderr.txt").write_bytes(observed.stderr)
        records.append({"argv": observed.argv, "returncode": observed.returncode,
                        "failure": observed.failure, "duration": observed.duration})
        if observed.failure or observed.returncode != expected or reason not in observed.stderr.decode("utf-8", "replace"):
            raise AssertionError(f"toolchain case {index}: {records[-1]}: {observed.stderr!r}")
        if list(root.glob(".chtholly-*")):
            raise AssertionError("temporary files escaped invocation cleanup")
        return observed

    def digest(path):
        return hashlib.sha256(path.read_bytes()).hexdigest()

    try:
        source = root / "input.cns"
        source.write_text("fn main() -> i32 { return 42; }", encoding="utf-8")
        output = root / "output.exe"
        invoke([source, "-o", output], 0)
        observed = run([str(output)], root, timeout=5)
        if observed.failure or observed.returncode != 42:
            raise AssertionError("real toolchain positive control failed")
        before = digest(output)
        tools = root / "tools with spaces"
        tools.mkdir()
        environment = dict(os.environ, CHTHOLLY_LLVM_BIN=str(tools))
        invoke([source, "-o", output], 1, "missing tool", environment)
        shutil.copyfile(fixture, tools / "clang-cl.exe")
        invoke([source, "-o", output], 1, "lld-link", environment)
        shutil.copyfile(fixture, tools / "lld-link.exe")
        for runtime in fixture.parent.glob("clang_rt.asan_dynamic*.dll"):
            shutil.copyfile(runtime, tools / runtime.name)
        for mode, message in (("clang-version-mismatch", "must match LLVM"),
                              ("linker-version-mismatch", "must match LLVM"),
                              ("link-failure", "returned 23"),
                              ("link-success-without-output", "without producing")):
            (tools / "mode.txt").write_text(mode, encoding="ascii")
            invoke([source, "-o", output], 1, message, environment)
            if digest(output) != before:
                raise AssertionError("tool failure destroyed previous output")

        source.write_text("fn main() -> i32 { return 0 }", encoding="utf-8")
        invoke([source, "-o", output], 1, "expected ';'")
        if digest(output) != before:
            raise AssertionError("source failure destroyed previous output")
        source.write_text("fn main() -> i32 { return 42; }", encoding="utf-8")
        # Windows refuses replacement of a read-only destination even when its
        # parent permits temporary files. Always restore the fixture attribute.
        output.chmod(stat.S_IREAD)
        try:
            invoke([source, "-o", output], 1, "cannot replace")
            if digest(output) != before:
                raise AssertionError("unwritable output lost the successful artifact")
        finally:
            output.chmod(stat.S_IREAD | stat.S_IWRITE)
        invoke([source, "-o", root], 1, "regular file")
        invoke([source, "-o", source], 1, "same file")
        alias = root / "alias.exe"
        os.link(source, alias)
        original = digest(source)
        invoke([source, "-o", alias], 1, "same file")
        if digest(source) != original:
            raise AssertionError("hard-link alias overwrote source")
        invoke([root / "missing.cns", "-o", output], 1, "cannot read")
        bad_source = root / "bad.cns"
        for content, reason in (
            (b"", "expected 'fn'"),
            (b"fn main() -> i32 { return -1; }", "unsupported character"),
            (b"fn main() -> i32 { return 1 + 1; }", "unsupported character"),
            (b"fn main() -> i32 { return 1u32; }", "expected ';'"),
            (b"fn main() -> i32 { return 0x10; }", "expected ';'"),
            (b"fn main() -> i32 { return 0; } // \xff", "invalid UTF-8"),
        ):
            bad_source.write_bytes(content)
            invoke([bad_source, "-o", output], 1, reason)
            if digest(output) != before:
                raise AssertionError("invalid input changed successful artifact")
        for arguments in ([], ["--unknown"], [str(source)], [str(source), "-o"],
                          [str(source), "-o", "a.exe", "-o", "b.exe"]):
            invoke(arguments, 2, "Usage:")
        if b"Usage:" not in invoke(["--help"], 0).stdout:
            raise AssertionError("missing help")
        if b"18.1.8" not in invoke(["--version"], 0).stdout:
            raise AssertionError("missing version baseline")
        special = root / "space \u6d4b\u8bd5.cns"
        special.write_bytes(b"\xef\xbb\xbf// comment\r\nfn main() -> i32 {\r\n return 42; // end\r\n}\r\n")
        native = root / "space \u6d4b\u8bd5.exe"
        invoke([special, "-o", native], 0)
        observed = run([str(native)], root, timeout=5)
        if observed.failure or observed.returncode != 42:
            raise AssertionError("Unicode/CRLF/BOM native case failed")
        return 0
    except (OSError, AssertionError) as error:
        print(f"{error}; artifacts: {root}", file=sys.stderr)
        return 1
    finally:
        (root / "result.json").write_text(json.dumps(records, indent=2), encoding="utf-8")
        if os.environ.get("CHTHOLLY_REPORT_DIR"):
            destination = Path(os.environ["CHTHOLLY_REPORT_DIR"]) / root.name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copytree(root, destination)


if __name__ == "__main__":
    sys.exit(main())
