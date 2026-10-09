import copy
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from prepare_natural_crash_plan import prepare


class CrashPlanContract(unittest.TestCase):
    def identity(self):
        return {"instance_id": "isolated-instance", "processes": [
            {"role": role, "pid": i + 1, "creation_time_utc": "fixture-created",
             "exe_path": role + ".exe", "exe_version": "fixture", "exe_sha256": "a" * 64}
            for i, role in enumerate(("automationd", "capture_host", "emulator"))],
            "modules": [{"path": "fixture.dll", "version": "fixture", "sha256": "b" * 64}]}

    def test_unarmed_identity_bound_plan(self):
        plan = prepare(self.identity())
        self.assertFalse(plan["armed"])
        self.assertEqual(plan["root_cause"], "UNRESOLVED")
        self.assertFalse(plan["sdk_change_allowed"])
        self.assertEqual(plan["target"]["pid"], 3)
        self.assertEqual(plan["limits"]["total_dump_bytes"], 512 * 1024 * 1024)

    def test_missing_or_duplicate_owner_rejected(self):
        identity = self.identity()
        identity["processes"][2] = copy.deepcopy(identity["processes"][0])
        with self.assertRaises(ValueError):
            prepare(identity)
        identity = self.identity()
        del identity["processes"][1]["creation_time_utc"]
        with self.assertRaises(ValueError):
            prepare(identity)
