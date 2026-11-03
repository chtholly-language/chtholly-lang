# SPDX-License-Identifier: MIT OR Apache-2.0
"""Capture source evidence before configuration, without discovering tests."""
import argparse
import importlib.util
import json
from pathlib import Path


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--report-dir", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    report = args.report_dir.resolve()
    report.mkdir(parents=True, exist_ok=True)
    spec = importlib.util.spec_from_file_location("ctest_report", root / "scripts/run-ctest.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    identity = module.source_identity(root, report)
    (report / "source-identity.json").write_text(json.dumps(identity, indent=2), encoding="utf-8")
