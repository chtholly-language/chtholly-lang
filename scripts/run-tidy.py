# SPDX-License-Identifier: MIT OR Apache-2.0
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--tool", type=Path, required=True)
    args = parser.parse_args()
    root, build = args.root.resolve(), args.build_dir.resolve()
    try:
        version = subprocess.run([str(args.tool), "--version"], capture_output=True, text=True, check=True)
        if not re.search(r"\bversion 18\.1\.8(?:\s|$)", version.stdout):
            raise ValueError("clang-tidy 18.1.8 is required")
        database = json.loads((build / "compile_commands.json").read_text(encoding="utf-8"))
        entries = {}
        for entry in database:
            source = Path(entry["file"])
            if not source.is_absolute():
                source = Path(entry["directory"]) / source
            source = source.resolve()
            if source.is_relative_to(root / "toolchain") or source.is_relative_to(root / "testing"):
                entries[source] = entry
        expected = {path.resolve() for base in (root / "toolchain", root / "testing") for path in base.rglob("*.cpp")}
        if set(entries) != expected:
            raise ValueError(f"translation unit mismatch: missing={expected - set(entries)}, extra={set(entries) - expected}")
        failed = False
        for source in sorted(entries):
            result = subprocess.run([str(args.tool), "-p", str(build), "--warnings-as-errors=*", str(source)],
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            sys.stdout.buffer.write(result.stdout)
            # Parser errors cannot be interpreted as an empty warning set.
            failed |= result.returncode != 0 or b"error:" in result.stdout
        return int(failed)
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print(error, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
