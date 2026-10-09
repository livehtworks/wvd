"""有限隔离日志验证；不连接服务，不读取默认生产路径。"""
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

TOOL = Path(__file__).parents[1] / "tools/analyze_run_timing.py"
sys.path.insert(0,str(TOOL.parent))
spec = importlib.util.spec_from_file_location("run_timing", TOOL)
analysis = importlib.util.module_from_spec(spec)
spec.loader.exec_module(analysis)


class TimingAnalysisTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="wvd-timing-analysis-")
        self.root = Path(self.temp.name) / "runs"
        self.root.mkdir()

    def tearDown(self):
        self.temp.cleanup()

    def fixture(self, run=1, revision="candidate-one"):
        directory = self.root / str(run)
        directory.mkdir()
        info = {"instance": "test-instance", "run_id": run,
                "definition": {"program_revision": revision, "pack_revision": "test-pack"}}
        result = {"run_id": run, "state": "Failed", "reason": "TEST_FAULT", "secondary_errors": [],
                  "business": {"elapsed_seconds": 2}, "sessions": [], "events": {"events": []},
                  "diagnostics": {"action_timing": {"complete": True, "rows": 1}}}
        row = {"run_id": run, "instance_id": "test-instance", "type": "input.result",
               "payload": {"source_path": "test-input", "result_wait_ns": 1000000, "attempts": 1,
                           "delivery_unknown": True, "outcome": "unconfirmed"}}
        for name, value in (("run.json", info), ("result.json", result)):
            (directory / name).write_text(json.dumps(value), encoding="utf-8")
        (directory / "action-timing.jsonl").write_text(json.dumps(row) + "\n", encoding="utf-8")
        (directory / "recognition-memory.log").write_text("private=1048576\n", encoding="utf-8")
        return directory

    def test_same_inputs_same_counts_and_no_mutation(self):
        self.fixture()
        before = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in self.root.rglob("*") if p.is_file()}
        first = analysis.analyze(self.root)
        self.assertEqual(first, analysis.analyze(self.root))
        self.assertEqual(first["retry_groups"][0]["cases"], 1)
        self.assertEqual(first["faults"][0]["state"], "Failed")
        self.assertEqual(before, {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in before})
        self.assertEqual(first["provenance"]["binary_identity"], "NOT_RECORDED_IN_RUN_LOGS")

    def test_no_data_cli_rejects_without_output(self):
        output = Path(self.temp.name) / "output"
        result = subprocess.run([sys.executable, str(TOOL), "--runs-root", str(self.root),
                                 "--output-dir", str(output)], capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"NO_RUN_DATA", result.stderr)
        self.assertFalse(output.exists())

    def test_missing_file(self):
        directory = self.fixture()
        (directory / "result.json").unlink()
        with self.assertRaisesRegex(ValueError, "MISSING_LOG:1/result.json"):
            analysis.analyze(self.root)

    def test_full_history_and_post_worker_memory(self):
        directory = self.fixture()
        path = directory / "result.json"
        result = json.loads(path.read_text(encoding="utf-8"))
        result["events"].update(resync_required=True, last_seq=2)
        result["diagnostics"]["event_history"] = {"rows": 1, "complete": True}
        result["diagnostics"]["post_terminal_memory"] = {"collected": True}
        path.write_text(json.dumps(result), encoding="utf-8")
        event = {"run_id": 1, "server_instance_id": "test-instance", "seq": 1,
                 "type": "observation.recovery", "payload": {"code": "EARLY_RECOVERY", "started_at_ns": 1}}
        (directory / "execution-events.jsonl").write_text(json.dumps(event) + "\n", encoding="utf-8")
        boundary = {"run_id": 1, "instance_id": "test-instance", "failed": 0,
                    "samples": {"worker_joined": {"private_bytes": 100}}}
        (directory / "memory-lifecycle.json").write_text(json.dumps(boundary), encoding="utf-8")
        report = analysis.analyze(self.root)
        self.assertTrue(report["event_coverage"][0]["complete"])
        self.assertIn("EARLY_RECOVERY", [row.get("code") for row in report["faults"]])
        self.assertEqual(report["lifecycle_memory"], [boundary])
        event["seq"] = 2
        (directory / "execution-events.jsonl").write_text(json.dumps(event) + "\n", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "EVENT_HISTORY_IDENTITY_OR_SEQUENCE"):
            analysis.analyze(self.root)

    def test_legacy_tail_not_reported_complete(self):
        path = self.fixture() / "result.json"
        result = json.loads(path.read_text(encoding="utf-8"))
        result["events"]["resync_required"] = True
        path.write_text(json.dumps(result), encoding="utf-8")
        self.assertFalse(analysis.analyze(self.root)["event_coverage"][0]["complete"])

    def test_truncated(self):
        path = self.fixture() / "action-timing.jsonl"
        path.write_bytes(path.read_bytes().rstrip(b"\n"))
        with self.assertRaisesRegex(ValueError, "TRUNCATED_TIMING_LOG"):
            analysis.analyze(self.root)

    def test_mixed_identity(self):
        self.fixture()
        self.fixture(2, "candidate-two")
        with self.assertRaisesRegex(ValueError, "MIXED_RUN_IDENTITIES"):
            analysis.analyze(self.root)

    def test_output_in_input_rejected(self):
        self.fixture()
        result = subprocess.run([sys.executable, str(TOOL), "--runs-root", str(self.root),
                                "--output-dir", str(self.root / "output")], capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"OUTPUT_MUST_BE_SEPARATE_FROM_INPUT", result.stderr)


if __name__ == "__main__":
    unittest.main()
