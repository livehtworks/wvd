"""Focused validation of recorded heap comparisons, without touching runtime data."""
import copy
import importlib.util
from pathlib import Path
import unittest
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))

spec = importlib.util.spec_from_file_location(
    "memory_owners", Path(__file__).resolve().parents[1] / "tools/analyze_memory_owners.py")
analysis = importlib.util.module_from_spec(spec)
spec.loader.exec_module(analysis)


def summary(extra_address=200, allocated=20):
    entries = [{"address": address, "process_heap": address == 100, "complete": True,
                "win32_error": 0, "elapsed_us": 12, "allocated_bytes": size,
                "committed_bytes": size + 10, "reserved_bytes": size + 20}
               for address, size in ((100, 10), (extra_address, allocated))]
    return {"heaps": 2, "per_heap": entries, **{
        field: sum(row[field] for row in entries)
        for field in ("allocated_bytes", "committed_bytes", "reserved_bytes")}}


class HeapDetailsTests(unittest.TestCase):
    def test_growth_and_elapsed_are_preserved(self):
        result = analysis.compare_heap_details(summary(), summary(allocated=25))
        self.assertTrue(result["complete"])
        self.assertEqual(result["heaps"][1]["change_bytes"]["allocated_bytes"], 5)
        self.assertEqual(result["heaps"][1]["after"]["elapsed_us"], 12)

    def test_missing_side_is_not_zero(self):
        result = analysis.compare_heap_details(summary(), summary(extra_address=300))
        self.assertEqual([row["presence"] for row in result["heaps"]], ["both", "disappeared", "appeared"])
        self.assertIsNone(result["heaps"][1]["change_bytes"])
        self.assertIsNone(result["heaps"][2]["before"])

    def test_missing_failed_duplicate_or_inconsistent_data_is_incomplete(self):
        cases = [summary() for _ in range(6)]
        cases[0].pop("per_heap")
        cases[1]["per_heap"][0]["allocated_bytes"] = None
        cases[2]["per_heap"][1]["address"] = 100
        cases[3]["allocated_bytes"] += 1
        cases[4]["per_heap"][0]["complete"] = False
        cases[5]["per_heap"][0]["process_heap"] = False
        for value in cases:
            with self.subTest(value=value):
                self.assertFalse(analysis.compare_heap_details(summary(), value)["complete"])

    def test_input_is_not_mutated(self):
        value = summary()
        before = copy.deepcopy(value)
        analysis.compare_heap_details(value, summary(allocated=25))
        self.assertEqual(value, before)


if __name__ == "__main__":
    unittest.main()
