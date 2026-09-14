"""Maa 有限会话实际承载 WVD 状态；本组不是战斗/任务操作验收。"""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import cv2
import numpy as np

ROOT = Path(__file__).resolve().parents[2]


class StateTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = Path(tempfile.mkdtemp(prefix="m4-state-tests-", dir=ROOT / ".local"))
        cls.sdk = Path(json.loads((ROOT / ".local/maafw.json").read_text(encoding="utf-8"))["sdk"])
        print("M4 state evidence: " + str(cls.root), flush=True)

    def run_case(self, name, *, all_at_once=False, **options):
        folder = self.root / name
        bundle = folder / "bundle"
        (bundle / "pipeline").mkdir(parents=True)
        profile = {"LANGUAGE": "zh_CN", "DEFAULT_OVERALL_STRATEGY": "Manual", "RELOAD_STRATEGY_WHEN": "不需要",
                   "STRATEGY": [{"group_name": "Manual", "complete_one_as_all": all_at_once, "skill_settings": [
                       {"role_var": role, "skill_var": "左下技能", "target_var": "next", "skill_lvl": 1, "freq_var": "保留原值"}
                       for role in ("A", "B")]}]}
        nodes = {}
        for index in range(2):
            nodes[f"Unit{index}"] = {"action": "Custom", "custom_action": "StateProbe", "custom_action_param": {
                "expected_remaining": 2 if index == 0 else (0 if all_at_once else 1), "consume": index == 0,
                "role": "A", "target_completed": True}, "next": [f"Checkpoint{index}"]}
            nodes[f"Checkpoint{index}"] = {"action": "Custom", "custom_action": "BusinessCheckpoint", "next": [f"Terminal{index}"]}
            nodes[f"Terminal{index}"] = {"action": "Custom", "custom_action": "RootTerminal"}
        if options.get("missing_checkpoint"):
            nodes["Unit0"]["next"] = ["Terminal0"]
        if options.get("cache_check"):
            nodes["Unit0"]["custom_action_param"]["cache_check"] = True
        if options.get("child_checkpoint"):
            nodes["Unit0"]["next"] = ["ChildCall"]
            nodes["ChildCall"] = {"action": "Custom", "custom_action": "RunChild",
                                  "custom_action_param": {"entry": "Checkpoint0"}, "next": ["Terminal0"]}
        if options.get("stop"):
            nodes["Unit0"] = {"action": "Custom", "custom_action": "Wait"}
        if options.get("recovery"):
            nodes["Recovered"] = dict(nodes["Unit0"])
            nodes["Recovered"]["custom_action_param"] = {"expected_remaining": 2, "restart": True, "consume": True, "role": "A"}
            nodes["Unit0"] = {"action": "Custom", "custom_action": "RequireRecovery"}
        for node in nodes.values():
            node.update(pre_delay=0, post_delay=0)
        (bundle / "pipeline/state.json").write_text(json.dumps(nodes), encoding="utf-8")
        frame = folder / "frame.png"
        frame.write_bytes(cv2.imencode(".png", np.zeros((1600, 900, 3), dtype=np.uint8))[1].tobytes())
        config = {"descriptor": str(ROOT / "packs/wvd/parameters/legacy-config-fields.json"),
                  "source": {"GENERAL": profile}, "bundle": str(bundle), "frame": str(frame),
                  "files": [{"path": p.relative_to(bundle).as_posix(), "sha256": hashlib.sha256(p.read_bytes()).hexdigest()}
                            for p in bundle.rglob("*.json")], "run_root": str(folder / "runs"), "output": str(folder / "result.json"), **options}
        path = folder / "input.json"
        path.write_text(json.dumps(config), encoding="utf-8")
        exe = ROOT / "build/m4/Release/test_m4_state.exe"
        with (folder / "native.log").open("wb") as log:
            process = subprocess.run([str(exe), str(path)], cwd=folder,
                env=dict(os.environ, PATH=str(self.sdk / "bin") + os.pathsep + os.environ.get("PATH", "")),
                stdout=log, stderr=log, timeout=45)
        self.assertEqual(process.returncode, 0, (folder / "native.log").read_text(encoding="utf-8", errors="replace")[-2000:])
        result = json.loads((folder / "result.json").read_text(encoding="utf-8"))
        self.assertEqual(result["backend_inputs"], 0)
        return result

    def test_strategy_consumption_and_run_isolation(self):
        r = self.run_case("normal", new_run=True, mutate_definition=True)
        for key in ("snapshot", "second"):
            self.assertEqual(r[key]["state"], "Completed", r)
            self.assertEqual(r[key]["generation"], 2)
            self.assertEqual(r[key]["completed_business_units"], 2)
            self.assertEqual(r[key]["business"]["task_step"], 2)
            self.assertEqual(len(r[key]["business"]["strategy"]["current"]["skill_settings"]), 1)
        self.assertNotEqual(r["snapshot"]["business"]["run_identity"], r["second"]["business"]["run_identity"])
        direct = r["direct"]
        self.assertFalse(direct["below_threshold"])
        self.assertTrue(direct["negative_is_nohit"])
        self.assertEqual(len(direct["after_failures"]["strategy"]["current"]["skill_settings"]), 2)
        self.assertEqual(len(direct["continued"]["strategy"]["current"]["skill_settings"]), 1)
        for name in ("duplicate_rejected", "old_generation_rejected", "other_run_rejected"):
            self.assertTrue(direct[name])
        self.assertEqual(direct["timers"]["combats"], 1)
        self.assertEqual(direct["timers"]["chests"], 1)
        self.assertEqual(direct["timers"]["combat_seconds"], 5)
        self.assertEqual(direct["timers"]["chest_seconds"], 5)
        self.assertFalse(direct["restarted"]["combat_timer_active"])
        self.assertEqual(len(direct["restarted"]["strategy"]["current"]["skill_settings"]), 2)
        self.assertEqual(direct["counting"]["dungeons"], 1)
        self.assertEqual(direct["counting"]["total_seconds"], 13)
        self.assertEqual(direct["task_point"]["strategy"]["current"]["group_name"], "Other")
        self.assertEqual(direct["english_task_point"]["strategy"]["current"]["group_name"], "Other")
        self.assertEqual(len(direct["observing_combat"]["strategy"]["current"]["skill_settings"]), 1)
        self.assertEqual(len(direct["after_combat_reset"]["strategy"]["current"]["skill_settings"]), 2)
        self.assertTrue(direct["after_rez"]["recover_after_rez"])
        self.assertTrue(direct["missing_group_unchanged"])
        self.assertEqual(r["frozen_definition"]["definition_version"], 3)
        self.assertEqual(len(r["frozen_definition"]["state_factory"]["parameters"]["profile"]["STRATEGY"]), 1)
        self.assertEqual(len(r["frozen_definition"]["continuation_units"]), 1)

    def test_complete_one_as_all(self):
        r = self.run_case("all", all_at_once=True)
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertTrue(r["snapshot"]["business"]["strategy"]["automatic"])

    def test_confirmed_auto_consumes_only_prepared_entry(self):
        r = self.run_case("confirmed-auto")["direct"]
        self.assertEqual(len(r["fallback_before_confirmation"]["strategy"]["current"]["skill_settings"]), 2)
        self.assertEqual(len(r["fallback_confirmed"]["strategy"]["current"]["skill_settings"]), 1)
        self.assertFalse(r["fallback_confirmed"]["has_prepared_skill"])
        self.assertTrue(r["prepared_cleared_at_boundary"])

    def test_supply_conditions_keep_forced_rest_separate(self):
        cases, expected = [], []
        # 预期来自旧 IdentifyState/强制补镐子条件，逐项列出而非调用被测函数生成。
        for count, met, active, interval, elapsed, bucket, party, forced, reason in [
            (0, False, True, 1, 0, 0, False, False, 0),
            (0, True, True, 3, 0, 0, False, False, 0),
            (0, True, True, 1, 0, 0, False, False, 1),
            (1, True, True, 3, 0, 0, False, False, 1),
            (2, True, True, 3, 0, 0, False, False, 0),
            (4, True, True, 3, 0, 0, False, False, 1),
            (1, True, False, 1, 0, 0, False, False, 0),
            (2, False, False, 99, 21600, 0, True, False, 2),
            (2, False, False, 99, 21600, 1, True, False, 0),
            (2, False, False, 99, 0, 0, False, True, 3),
            (2, False, False, 99, 21600, 0, True, True, 3),
            (1, True, True, 0, 0, 0, False, False, 1),
        ]:
            cases.append(dict(profile={"ACTIVE_REST": active, "REST_INTERVEL": interval,
                "RE_ASSEMBLE_PARTY": party, "ACTIVE_ROYALSUITE_REST": True},
                facts=dict(dungeons=count, met_encounter=met, total_seconds=elapsed,
                           last_bag_clear=bucket), pickaxes_exhausted=forced))
            expected.append(dict(reason=reason, required=reason != 0,
                                 reassemble=party and elapsed // 21600 != bucket, royal_suite=True))
        result = self.run_case("supply-policy", supply_cases=cases)
        self.assertEqual(result["supply_cases"], expected)
        self.assertEqual(result["forced_state_rest"], 3)

    def test_checkpoint_is_not_child_terminal(self):
        for mode in ("missing_checkpoint", "child_checkpoint"):
            r = self.run_case(mode, **{mode: True})
            self.assertEqual(r["snapshot"]["state"], "Failed", r)
            self.assertEqual(r["snapshot"]["reason"], "BUSINESS_CHECKPOINT_MISSING", r)
            self.assertEqual(r["snapshot"]["completed_business_units"], 0)
            self.assertEqual(r["snapshot"]["generation"], 1)

    def test_same_frame_business_condition_observes_state_change(self):
        r = self.run_case("state-cache", cache_check=True)
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["snapshot"]["business"]["task_step"], 3)
        self.assertEqual(r["backend_inputs"], 0)

    def test_stop_does_not_start_next_unit(self):
        r = self.run_case("stop", stop=True)
        self.assertEqual(r["snapshot"]["state"], "UserStopped", r)
        self.assertEqual(r["snapshot"]["completed_business_units"], 0)
        self.assertEqual(r["snapshot"]["generation"], 1)

    def test_recovery_is_not_normal_continuation(self):
        r = self.run_case("recover", recovery=True)
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["snapshot"]["generation"], 3)
        self.assertEqual(r["snapshot"]["completed_business_units"], 2)
        self.assertEqual(r["snapshot"]["business"]["crashes"], 1)

    def test_frozen_factory_and_unit_budget(self):
        for name, options, error in [("unknown", {"unknown_factory": True}, "STATE_FACTORY_UNKNOWN"),
                                     ("budget", {"max_units": 1}, "BUSINESS_UNIT_BUDGET_INVALID")]:
            r = self.run_case(name, **options)
            self.assertEqual(r["start_error"], error)
            self.assertEqual(r["offline_connections"], 0)


if __name__ == "__main__":
    unittest.main()
