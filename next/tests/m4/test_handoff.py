"""专属M4.6离线入口；固定Maa/正式hook/同一RunCoordinator，不代表完整7000G验收。"""
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


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class HandoffTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = Path(tempfile.mkdtemp(prefix="m4-handoff-", dir=ROOT / ".local"))
        cls.exe = ROOT / "build/m4/Release/test_m4_handoff.exe"
        # 未接入/未构建必须报错，不跳过，也不退回旧workflow产物。
        cls.exe_hash = digest(cls.exe)
        sdk = Path(json.loads((ROOT / ".local/maafw.json").read_text(encoding="utf-8"))["sdk"])
        cls.env = dict(os.environ, PATH=str(sdk / "bin") + os.pathsep + os.environ.get("PATH", ""))
        print("M4 handoff evidence: " + str(cls.root), flush=True)

    def invoke(self, folder, config):
        path = folder / "input.json"
        config["output"] = str(folder / "result.json")
        config["descriptor"] = str(ROOT / "packs/wvd/parameters/legacy-config-fields.json")
        config["quest_catalog"] = str(ROOT / "packs/wvd/parameters/legacy-quests.json")
        path.write_text(json.dumps(config, ensure_ascii=False), encoding="utf-8")
        self.assertEqual(digest(self.exe), self.exe_hash)
        with (folder / "native.log").open("wb") as log:
            # 此看护只会报失败；7300秒由注入的MonotonicClock推进，不靠外部超时取消。
            process = subprocess.run([str(self.exe), str(path)], cwd=folder, env=self.env,
                                     stdout=log, stderr=log, timeout=180)
        self.assertEqual(digest(self.exe), self.exe_hash)
        self.assertEqual(process.returncode, 0,
                         (folder / "native.log").read_text(encoding="utf-8", errors="replace")[-4000:])
        output = json.loads((folder / "result.json").read_text(encoding="utf-8"))
        (folder / "exe-sha256.txt").write_text(self.exe_hash + "\n", encoding="utf-8")
        return output

    @staticmethod
    def profile(money):
        return {"GENERAL": {"FARM_TARGET": "fortress-B8F_trap", "ACTIVE_BEG_MONEY": money,
                            "TASK_SPECIFIC_CONFIG": False, "REST_INTERVEL": 7},
                "DEFAULT": {"MAX_CRASH_LIMIT": 11},
                "7000G": {"REST_INTERVEL": 999, "MAX_CRASH_LIMIT": 99}}

    def run_case(self, case):
        folder = self.root / case
        folder.mkdir()
        profile = self.profile(case not in ("wait", "stop-wait", "wait-budget"))
        if case in ("state", "extensions"):
            return self.invoke(folder, {"case": case, "profile_source": profile})
        description = folder / "description"
        description.mkdir()
        names = self.invoke(description, {"case": "describe"})["images"]
        # 资源清单来自编译器只用于准备合成资产；业务预期在以下断言中独立写定。
        names = sorted({name if name.endswith(".png") else name + ".png" for name in names})
        bundle = folder / "assets"
        bundle.mkdir()
        rng = np.random.default_rng(9467300)
        patterns = {}
        for name in names:
            canonical = "returntotown.png" if name.lower() == "returntotown.png" else name
            if canonical in patterns:
                continue
            patterns[canonical] = rng.integers(30, 255, (24, 40, 3), dtype=np.uint8)
            path = bundle / "image" / canonical
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(cv2.imencode(".png", patterns[canonical])[1].tobytes())
        before = np.zeros((1600, 900, 3), dtype=np.uint8)
        # 同一画面包含旧分流标记和实际7000G时间轮入口，不按capture次数换图。
        for name, x, y in (("cursedWheel_timeLeap.png", 180, 600), ("cursedWheel.png", 420, 900)):
            before[y:y + 24, x:x + 40] = patterns[name]
        after = np.zeros_like(before)
        next_image = "dungFlag.png" if case in ("wait", "stop-wait", "wait-budget") else "cursedWheelTitle.png"
        after[500:524, 300:340] = patterns[next_image]
        for name, frame in (("before.png", before), ("after.png", after)):
            (folder / name).write_bytes(cv2.imencode(".png", frame)[1].tobytes())
        files = [{"path": path.relative_to(bundle).as_posix(), "sha256": digest(path)}
                 for path in sorted(bundle.rglob("*.png"))]
        result = self.invoke(folder, {"case": case, "profile_source": profile, "bundle": str(bundle),
            "files": files, "before": str(folder / "before.png"), "after": str(folder / "after.png"),
            "run_root": str(folder / "runs")})
        self.assertTrue(result["native_run_executed"])
        self.assertTrue(result["source"]["quiescent"])
        if case != "storage-fail":
            self.assertTrue(result["source"]["result_saved"])
            self.assertEqual(result["source_saved"]["business"], result["source"]["business"])
        return result

    def test_fifth_unknown_provenance_replay_and_exact_clock_boundaries(self):
        result = self.run_case("state")
        intent = result["state"]["handoff_intent"]
        self.assertEqual(intent["kind"], "turn_to_7000G")
        self.assertEqual(intent["unknown_samples"], 5)
        self.assertNotIn("request_id", intent)
        self.assertEqual(result["stale_error"], "LEAP_OBSERVATION_STALE")
        self.assertEqual(result["source_error"], "HANDOFF_SOURCE_INVALID")
        self.assertEqual(result["clock_error"], "WVD_CLOCK_MOVED_BACKWARD")
        self.assertEqual([s["elapsed_ms"] for s in result["slices"]],
                         [1460000, 2920000, 4380000, 5840000, 7300000])

    def test_saved_source_starts_real_gold_pipeline_once(self):
        result = self.run_case("money")
        source = result["source"]
        self.assertEqual(source["state"], "Interrupted")
        self.assertEqual(source["inputs"]["backend_called"], 0)
        self.assertEqual(source["sessions"][-1]["reason"], "leap.unknown")
        self.assertEqual(source["business"]["handoff_intent"]["unknown_samples"], 5)
        self.assertEqual(result["next"]["run_id"], source["run_id"] + 1)
        self.assertEqual(result["replay_run_id"], result["next"]["run_id"])
        self.assertFalse(result["replay_created_directory"])
        self.assertGreater(result["next"]["inputs"]["backend_called"], 0)
        self.assertTrue(result["next"]["quiescent"] and result["next"]["result_saved"])
        definition = result["next_record"]["definition"]
        parameters = definition["state_factory"]["parameters"]
        self.assertEqual(parameters["profile"]["FARM_TARGET"], "7000G")
        self.assertEqual(parameters["profile"]["REST_INTERVEL"], 7)
        self.assertEqual(parameters["profile"]["MAX_CRASH_LIMIT"], 11)
        self.assertEqual(parameters["handoff_parent"]["source"], source["business"]["handoff_source"])
        self.assertEqual(parameters["handoff_parent"]["profile_sources"]["FARM_TARGET"], "HANDOFF:turn_to_7000G")
        self.assertNotIn("profile_store", parameters)
        self.assertEqual(result["next_saved"]["business"]["gold_income"]["active"], True)
        # 下一任务只验证真实启动与首个受控输入，然后用户停止；不宣称完整7000G通过。
        self.assertNotEqual(result["next"]["state"], "Completed")

    def test_fordraig_cos_state_two_cycles_and_pending_recovery_guards(self):
        result = self.run_case("extensions")
        self.assertEqual(result["fordraig"]["fordraig"]["completed_cycles"], 2)
        self.assertEqual(result["CaveOfSeperation"]["cave_of_separation"]["completed_cycles"], 2)
        self.assertEqual(result["fordraig"]["dungeons"], 2)
        self.assertEqual(result["CaveOfSeperation"]["dungeons"], 2)
        self.assertFalse(result["native_run_executed"])

    def test_7300_wait_preserves_run_and_restarts_after_five_bounded_segments(self):
        result = self.run_case("wait")
        source = result["source"]
        self.assertEqual(source["state"], "Completed")
        self.assertEqual(source["generation"], 7)
        self.assertEqual(source["completed_business_units"], 1)
        self.assertEqual(len(source["sessions"]), 7)
        waits = source["sessions"][1:6]
        self.assertEqual([s["definition"]["entry"] for s in waits], ["LeapWait_Entry"] * 5)
        self.assertTrue(all(s["reason"] == "leap.wait_boundary" and s["quiescent"] for s in waits))
        self.assertTrue(all(s["definition"]["time_limit_ms"] == 1500000 for s in waits))
        self.assertTrue(all(s["definition"]["time_limit_ms"] <= 1800000 for s in source["sessions"]))
        self.assertEqual(source["business"]["leap_wait"]["elapsed_ms"], 7300000)
        self.assertFalse(source["business"]["leap_wait"]["active"])
        self.assertEqual(source["business"]["crashes"], 1)
        self.assertEqual(result["lifecycle"], ["StopApplication", "StartApplication"])
        self.assertEqual(result["inputs"], 0)
        self.assertFalse(result["next_directory_exists"])

    def test_cancel_inside_cooperative_wait_does_not_restart(self):
        result = self.run_case("stop-wait")
        self.assertEqual(result["source"]["state"], "UserStopped")
        self.assertTrue(result["source"]["business"]["leap_wait"]["active"])
        self.assertEqual(result["lifecycle"], [])
        self.assertEqual(result["inputs"], 0)
        self.assertFalse(result["next_directory_exists"])

    def test_recovery_budget_exhaustion_is_not_wait_success(self):
        result = self.run_case("wait-budget")
        self.assertEqual(result["source"]["state"], "Interrupted")
        self.assertEqual(result["source"]["generation"], 3)
        self.assertEqual(result["source"]["business"]["leap_wait"]["elapsed_ms"], 2920000)
        self.assertEqual(result["source"]["completed_business_units"], 0)
        self.assertEqual(result["lifecycle"], [])
        self.assertFalse(result["next_directory_exists"])

    def test_storage_failure_never_generates_next_request(self):
        result = self.run_case("storage-fail")
        self.assertFalse(result["source"]["result_saved"])
        self.assertEqual(result["dispatch_error"], "HANDOFF_RESULT_NOT_COMMITTED")
        self.assertEqual(result["inputs"], 0)
        self.assertFalse(result["next_directory_exists"])

    def test_native_connect_stop_timeout_keeps_ownership_until_return(self):
        result = self.run_case("late-connect")
        self.assertFalse(result["held"]["quiescent"])
        self.assertFalse(result["held"]["result_saved"])
        self.assertEqual(result["held"]["reason"], "STOP_TIMEOUT")
        self.assertEqual(result["inputs"], 0)
        self.assertFalse(result["next_directory_exists"])


if __name__ == "__main__":
    unittest.main()
