"""Exercise the production diagnostic reader/writer with disposable inputs."""
import hashlib
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from diagnostic_io import InputBudget, write_report


class DiagnosticIOTests(unittest.TestCase):
    def test_read_limits_and_provenance(self):
        cases = [(b"0123456789\n", {"maximum_line": 4}, "LINE_BUDGET"),
                 (b"{}\n{}\n{}\n", {"maximum_records": 2}, "RECORD_BUDGET"),
                 (b"{}\n{}\n", {"maximum_bytes": 4}, "TOTAL_INPUT_BUDGET"),
                 (b"{}", {}, "TRUNCATED_LINE")]
        with tempfile.TemporaryDirectory(prefix="wvd-diagnostic-") as directory:
            path = Path(directory) / "input.jsonl"
            for content, limits, code in cases:
                with self.subTest(code=code):
                    path.write_bytes(content)
                    with self.assertRaisesRegex(ValueError, code):
                        list(InputBudget(**limits).lines(path))
                    self.assertEqual(path.read_bytes(), content)
            path.write_bytes(b"{}\n")
            provenance = {}
            self.assertEqual(list(InputBudget().lines(path, provenance)), ["{}\n"])
            self.assertEqual(provenance, {"bytes": 3, "sha256": hashlib.sha256(b"{}\n").hexdigest()})
            path.write_bytes(b"broken-json")
            with self.assertRaises(ValueError):
                InputBudget().json(path)

    def test_output_failure_is_partial_and_no_overwrite(self):
        with tempfile.TemporaryDirectory(prefix="wvd-diagnostic-") as directory:
            path = Path(directory) / "report.json"
            with self.assertRaisesRegex(ValueError, "OUTPUT_BUDGET"):
                write_report(path, {"large": "x" * 100}, 16)
            self.assertFalse(path.exists())
            self.assertTrue(path.with_suffix(".json.partial").exists())
            existing = Path(directory) / "existing.json"
            existing.write_bytes(b"authority")
            with self.assertRaisesRegex(ValueError, "OUTPUT_ALREADY_EXISTS"):
                write_report(existing, {})
            self.assertEqual(existing.read_bytes(), b"authority")


if __name__ == "__main__":
    unittest.main()
