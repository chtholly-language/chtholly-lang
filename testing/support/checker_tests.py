# SPDX-License-Identifier: MIT OR Apache-2.0
"""Checker failure contracts with controlled metadata/tool responses."""
import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET

FORMAT = TIDY = REPORT = None


class CheckerTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="chtholly checker ")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.build = self.root / "out"
        self.build.mkdir()
        self.sources = [self.root / "toolchain/a.cpp", self.root / "testing/b.cpp"]
        for source in self.sources:
            source.parent.mkdir(parents=True)
            source.write_text("int value;\n", encoding="utf-8")

    def database(self, sources=None):
        entries = [{"file": str(source), "directory": str(self.root),
                    "arguments": ["clang-cl", "/c", str(source)]}
                   for source in (self.sources if sources is None else sources)]
        (self.build / "compile_commands.json").write_text(json.dumps(entries), encoding="utf-8")

    def invoke(self, module, response):
        arguments = ["checker", "--root", str(self.root), "--tool", str(self.root / "tool")]
        if module is TIDY:
            arguments += ["--build-dir", str(self.build)]
        with patch.object(sys, "argv", arguments), patch.object(module.subprocess, "run", side_effect=response):
            return module.main()

    def response(self, command, **kwargs):
        output = "tool version 18.1.8\n" if "--version" in command else b""
        return subprocess.CompletedProcess(command, 0, stdout=output)

    def test_format_complete_scope_and_failure(self):
        third_party = self.root / "out/dependencies/external.cpp"
        third_party.parent.mkdir(parents=True)
        third_party.write_text("not project input", encoding="utf-8")
        self.assertEqual(set(FORMAT.project_cpp(self.root)), set(self.sources))
        self.assertEqual(self.invoke(FORMAT, self.response), 0)

        def invalid(command, **kwargs):
            if "--version" in command:
                return self.response(command, **kwargs)
            return subprocess.CompletedProcess(command, 1)

        self.assertEqual(self.invoke(FORMAT, invalid), 1)

    def test_tidy_complete_scope_and_missing_database(self):
        self.assertEqual(self.invoke(TIDY, self.response), 2)
        self.database(self.sources[:1])
        self.assertEqual(self.invoke(TIDY, self.response), 2)
        self.database()
        self.assertEqual(self.invoke(TIDY, self.response), 0)

    def test_tidy_parser_errors_and_warnings(self):
        self.database()
        for status, output in ((0, b"error: cannot parse\n"), (1, b"warning: controlled diagnostic\n")):
            def invalid(command, **kwargs):
                if "--version" in command:
                    return self.response(command, **kwargs)
                return subprocess.CompletedProcess(command, status, stdout=output)

            self.assertEqual(self.invoke(TIDY, invalid), 1)

    def test_wrong_version_and_missing_tool(self):
        for module in (FORMAT, TIDY):
            for version in ("17.0.0", "18.1.80"):
                def wrong_version(command, **kwargs):
                    return subprocess.CompletedProcess(command, 0, stdout="version " + version)

                self.assertEqual(self.invoke(module, wrong_version), 2)
            self.assertEqual(self.invoke(module, OSError("missing tool")), 2)


class ReportTests(unittest.TestCase):
    def setUp(self):
        self.expected = {"unit.source.empty_eof": {"unit", "frontend"},
                         "integration.frontend.checked_pipeline": {"integration", "frontend"},
                         "infrastructure.runner.contracts": {"infrastructure", "frontend"}}
        self.registered = [{"name": name, "properties": [{"name": "LABELS", "value": sorted(labels)}]}
                           for name, labels in self.expected.items()]

    def test_inventory_requires_exact_identities_and_labels(self):
        self.assertEqual(REPORT.validate_inventory(self.expected, self.registered, None, True), self.expected)
        for registered in ([], self.registered[:-1], self.registered + self.registered[:1]):
            with self.assertRaises(ValueError):
                REPORT.validate_inventory(self.expected, registered, None, True)
        self.registered[0]["properties"].append({"name": "DISABLED", "value": True})
        with self.assertRaises(ValueError):
            REPORT.validate_inventory(self.expected, self.registered, None, True)
        self.registered[0]["properties"] = [{"name": "LABELS", "value": ["unit"]}]
        with self.assertRaises(ValueError):
            REPORT.validate_inventory(self.expected, self.registered, None, True)

    def test_selection_is_explicit_and_empty_selection_fails(self):
        self.assertEqual(REPORT.validate_inventory(self.expected, self.registered[:1], "unit", True),
                         {"unit.source.empty_eof": {"unit", "frontend"}})
        with self.assertRaises(ValueError):
            REPORT.validate_inventory(self.expected, [], "native", True)
        with self.assertRaises(ValueError):
            REPORT.validate_inventory(self.expected, self.registered, None, False)

    def test_junit_missing_duplicate_skip_and_failure(self):
        cases = [ET.Element("testcase", name=name, status="run") for name in self.expected]
        self.assertEqual(REPORT.junit_counts(cases, self.expected)["passed"], 3)
        for observed in (cases[:-1], cases + cases[:1]):
            with self.assertRaises(ValueError):
                REPORT.junit_counts(observed, self.expected)
        ET.SubElement(cases[0], "skipped")
        ET.SubElement(cases[1], "failure")
        counts = REPORT.junit_counts(cases, self.expected)
        self.assertEqual((counts["skipped"], counts["failed"], counts["passed"]), (1, 1, 1))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    args, remaining = parser.parse_known_args()
    for name, filename in (("FORMAT", "check-format.py"), ("TIDY", "run-tidy.py"), ("REPORT", "run-ctest.py")):
        spec = importlib.util.spec_from_file_location(name, args.root / "scripts" / filename)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        globals()[name] = module
    unittest.main(argv=[__file__, *remaining])
