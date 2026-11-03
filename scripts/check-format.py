# SPDX-License-Identifier: MIT OR Apache-2.0
import argparse
from pathlib import Path
import re
import subprocess
import sys


def project_cpp(root):
    return sorted(path for directory in (root / "toolchain", root / "testing")
                  for path in directory.rglob("*") if path.suffix in (".h", ".cpp"))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--tool", type=Path, required=True)
    args = parser.parse_args()
    try:
        version = subprocess.run([str(args.tool), "--version"], capture_output=True, text=True, check=True)
        if not re.search(r"\bversion 18\.1\.8(?:\s|$)", version.stdout):
            raise ValueError("clang-format 18.1.8 is required")
        files = project_cpp(args.root.resolve())
        if not files:
            raise ValueError("no project C++ files")
        failed = False
        for path in files:
            status = subprocess.run([str(args.tool), "--dry-run", "--Werror", str(path)]).returncode
            failed |= status != 0
        return int(failed)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(error, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
