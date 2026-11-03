# SPDX-License-Identifier: MIT OR Apache-2.0
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

from process import install_cancellation_handlers, run
from run_case import ConfigurationError, execute_case, unique_object, validate

FIXTURE = None


class RunnerTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="chtholly runner \u6d4b\u8bd5 ")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.manifest = self.root / "case.json"
        (self.root / "input.cns").write_bytes(b"// UTF-8 \xc3\xa9\r\nfn main() -> i32 { return 42; }")
        self.data = {"schema_version": 1, "name": "e2e.runner.contract", "kind": "run",
                     "source": "input.cns", "basis": "independent fixture contract",
                     "compile": {"exit_code": 0}, "run": {"exit_code": 42}}

    def execute(self, mode="success", data=None, report=None):
        self.manifest.write_text(json.dumps(data or self.data), encoding="utf-8")
        with patch.dict(os.environ, {"CHTHOLLY_FIXTURE_MODE": mode}):
            return execute_case(FIXTURE, self.manifest, self.root / "work",
                                report or self.root / "reports")

    def test_success_and_wrong_return(self):
        self.assertEqual(self.execute(), 0)
        self.data["run"]["exit_code"] = 0
        self.assertEqual(self.execute(), 1)

    def test_missing_output_and_no_old_executable(self):
        self.assertEqual(self.execute(), 0)
        self.assertEqual(self.execute("no-output"), 1)

    def test_source_name_cannot_alias_output(self):
        shutil.copyfile(FIXTURE, self.root / "program.exe")
        self.data["source"] = "program.exe"
        self.assertEqual(self.execute("no-output"), 1)

    def test_failure_never_runs(self):
        marker = self.root / "ran"
        with patch.dict(os.environ, {"CHTHOLLY_RUN_MARKER": str(marker)}):
            self.assertEqual(self.execute("reject"), 1)
        self.assertFalse(marker.exists())

    def test_expected_rejection_requires_diagnostic(self):
        self.data.update(kind="compile_fail", compile={"exit_code": 1, "stderr": {
            "contains": ["input.cns:1:1: error: rejected"]}})
        del self.data["run"]
        self.assertEqual(self.execute("reject"), 0)
        self.assertEqual(self.execute("bad-diagnostic"), 1)
        self.assertEqual(self.execute("tool-error"), 1)
        self.assertEqual(self.execute("crash"), 1)
        self.data["compile"]["timeout_seconds"] = 0.2
        self.assertEqual(self.execute("hang"), 1)

    def test_missing_tool_is_infrastructure(self):
        self.manifest.write_text(json.dumps(self.data), encoding="utf-8")
        self.assertEqual(execute_case(self.root / "missing", self.manifest,
                                     self.root / "work", self.root / "reports"), 2)

    def test_manifest_and_report_errors(self):
        bad = dict(self.data, unknown=True)
        with self.assertRaises(ConfigurationError):
            validate(bad)
        with self.assertRaises(ConfigurationError):
            json.loads('{"kind":"run","kind":"compile"}', object_pairs_hook=unique_object)
        self.assertEqual(self.execute(data=bad), 2)
        for field, value in (("schema_version", True), ("kind", []), ("source", "")):
            with self.subTest(field=field), self.assertRaises(ConfigurationError):
                validate(dict(self.data, **{field: value}))
        rejection = dict(self.data, kind="compile_fail", compile={
            "exit_code": 1, "stderr": {"contains": ["input.cns:1:1: error:"]}})
        rejection.pop("run")
        with self.assertRaises(ConfigurationError):
            validate(rejection)
        blocked = self.root / "blocked"
        blocked.write_text("not a directory", encoding="utf-8")
        self.assertEqual(self.execute(report=blocked), 2)
        self.manifest.write_text(json.dumps(self.data), encoding="utf-8")
        with patch("pathlib.Path.write_text", side_effect=OSError("report failure")):
            self.assertEqual(execute_case(FIXTURE, self.manifest, self.root / "work",
                                         self.root / "reports"), 2)

    def test_output_limit_and_start_failure(self):
        observed = run([str(FIXTURE), "--bytes", "16384"], self.root, output_limit=16384)
        self.assertIsNone(observed.failure)
        self.assertEqual(len(observed.stdout) + len(observed.stderr), 16384)
        observed = run([str(FIXTURE), "--flood"], self.root, output_limit=16384)
        self.assertEqual(observed.failure, "output_limit")
        self.assertLessEqual(len(observed.stdout) + len(observed.stderr), 16384)
        self.assertTrue(run([str(self.root / "absent")], self.root).failure.startswith("start_error"))

    def test_timeout_reaps_descendant(self):
        heartbeat = self.root / "heartbeat"
        observed = run([str(FIXTURE), "--tree", str(heartbeat)], self.root, timeout=1)
        self.assertEqual(observed.failure, "timeout")
        self.assertTrue(heartbeat.is_file(), "fixture must reach descendant creation")
        size = heartbeat.stat().st_size
        time.sleep(0.2)
        self.assertEqual(heartbeat.stat().st_size, size)

    def test_parent_exit_reaps_orphan(self):
        heartbeat = self.root / "orphan-heartbeat"
        observed = run([str(FIXTURE), "--orphan", str(heartbeat)], self.root, timeout=5)
        self.assertIsNone(observed.failure)
        self.assertEqual(observed.returncode, 0)
        self.assertTrue(heartbeat.is_file())
        time.sleep(0.2)
        size = heartbeat.stat().st_size
        time.sleep(0.2)
        self.assertEqual(heartbeat.stat().st_size, size)

    def test_cancellation_reaps_tree(self):
        heartbeat = self.root / "cancel-heartbeat"
        helper = Path(__file__).parent / "fixtures/process_parent.py"
        parent = subprocess.Popen([sys.executable, str(helper), str(FIXTURE), str(heartbeat)],
                                  stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                  stderr=subprocess.DEVNULL)
        try:
            deadline = time.monotonic() + 5
            while not heartbeat.is_file() and parent.poll() is None and time.monotonic() < deadline:
                time.sleep(0.01)
            self.assertTrue(heartbeat.is_file(), "fixture must reach child creation")
        finally:
            if parent.poll() is None:
                parent.terminate()
            parent.wait(timeout=5)
        time.sleep(0.2)
        size = heartbeat.stat().st_size
        time.sleep(0.2)
        self.assertEqual(heartbeat.stat().st_size, size)

    def test_parallel_work_isolation(self):
        self.manifest.write_text(json.dumps(self.data), encoding="utf-8")
        with ThreadPoolExecutor(max_workers=2) as pool:
            futures = [pool.submit(execute_case, FIXTURE, self.manifest,
                                   self.root / "work", self.root / "reports") for _ in range(2)]
            self.assertEqual([future.result() for future in futures], [0, 0])
        self.assertEqual(len(list((self.root / "work").iterdir())), 2)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--fixture", type=Path, required=True)
    args, remaining = parser.parse_known_args()
    FIXTURE = args.fixture.resolve()
    install_cancellation_handlers()
    unittest.main(argv=[__file__, *remaining])
