# SPDX-License-Identifier: MIT OR Apache-2.0
"""Prepare File API queries before configuration; does not invoke CMake."""
import argparse
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path, required=True)
    args = parser.parse_args()
    query = args.build_dir / ".cmake/api/v1/query"
    query.mkdir(parents=True, exist_ok=True)
    for name in ("codemodel-v2", "cache-v2"):
        (query / name).write_text("", encoding="utf-8")


if __name__ == "__main__":
    main()
