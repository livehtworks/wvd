"""Isolated RunStore/SDK PNG diagnostics; no device discovery or existing run writes."""
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


class DiagnosticTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = Path(tempfile.mkdtemp(prefix="m4-diagnostics-", dir=ROOT / ".local"))
        cls.exe = ROOT / "build/m4/Release/test_m4_diagnostics.exe"
        cls.exe_hash = digest(cls.exe)
        sdk = Path(json.loads((ROOT / ".local/maafw.json").read_text(encoding="utf-8"))["sdk"])
        cls.env = dict(os.environ, PATH=str(sdk / "bin") + os.pathsep + os.environ.get("PATH", ""))
        print("M4 PNG evidence: " + str(cls.root), flush=True)

    def invoke(self, folder, config):
        config["output"] = str(folder / "result.json")
        config["descriptor"] = str(ROOT / "packs/wvd/parameters/legacy-config-fields.json")
        path = folder / "input.json"
        path.write_text(json.dumps(config), encoding="utf-8")
        self.assertEqual(digest(self.exe), self.exe_hash)
        with (folder / "native.log").open("wb") as log:
            process = subprocess.run([str(self.exe), str(path)], cwd=folder, env=self.env,
                                     stdout=log, stderr=log, timeout=180)
        after = digest(self.exe)
        (folder / "execution.json").write_text(json.dumps(
            {"exe_sha256_before": self.exe_hash, "exe_sha256_after": after, "exit": process.returncode}),
            encoding="utf-8")
        self.assertEqual(after, self.exe_hash)
        self.assertEqual(process.returncode, 0,
                         (folder / "native.log").read_text(encoding="utf-8", errors="replace")[-4000:])
        return json.loads((folder / "result.json").read_text(encoding="utf-8"))

    def run_case(self, case):
        folder = self.root / case
        folder.mkdir()
        names = ["scene.png", "target.png", "post.png", "missing.png"]
        if case == "reward":
            description = folder / "description"
            description.mkdir()
            names = self.invoke(description, {"case": "describe"})["images"]
        names = sorted({name if name.endswith(".png") else name + ".png" for name in names})
        bundle = folder / "assets"
        bundle.mkdir()
        rng = np.random.default_rng(48070)
        patterns = {}
        for name in names:
            patterns[name] = rng.integers(30, 255, (24, 40, 3), dtype=np.uint8)
            path = bundle / "image" / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(cv2.imencode(".png", patterns[name])[1].tobytes())
        before = np.zeros((1600, 900, 3), np.uint8)
        after = before.copy()
        if case == "reward":
            before[1200:1224, 400:440] = patterns["fishing/CloseFishInfo.png"]
            after[200:224, 100:140] = patterns["dungFlag.png"]
        else:
            before[200:224, 100:140] = patterns["scene.png"]
            before[500:524, 400:440] = patterns["target.png"]
            after[700:724, 250:290] = patterns["scene.png"]
        for name, pixels in (("before", before), ("after", after)):
            (folder / (name + ".png")).write_bytes(cv2.imencode(".png", pixels)[1].tobytes())
        if case != "reward":
            def recognition(name):
                return {"id": name, "revision": "1", "roi": [0, 0, 900, 1600], "image": name, "threshold": .99}
            action = {"action": "Custom", "custom_action": "RequireRecovery",
                      "custom_action_param": {"reason": "leap.wait_boundary" if case == "wait-boundary" else "original.recovery"}}
            if case.startswith("guard-"):
                action = {"action": "Custom", "custom_action": "GuardedAction", "next": ["Terminal"],
                          "custom_action_param": {"scene": "wvd", "scene_recognition": recognition("scene.png"),
                              "target_recognition": recognition("missing.png" if case == "guard-pre" else "target.png"),
                              "postcondition": recognition("post.png"), "command": {"kind": "Click"},
                              "allowed_area": [0, 0, 900, 1600], "postcondition_timeout_ms": 150}}
            nodes = {"Entry": action, "Terminal": {"action": "Custom", "custom_action": "RootTerminal"}}
            for node in nodes.values():
                node.update(pre_delay=0, post_delay=0, max_hit=1)
            (bundle / "pipeline").mkdir()
            (bundle / "pipeline/workflow.json").write_text(json.dumps(nodes), encoding="utf-8")
        files = [{"path": path.relative_to(bundle).as_posix(), "sha256": digest(path)}
                 for path in sorted(bundle.rglob("*")) if path.is_file()]
        result = self.invoke(folder, {"case": case, "bundle": str(bundle), "files": files,
            "before": str(folder / "before.png"), "after": str(folder / "after.png"),
            "run_root": str(folder / "runs")})
        return folder, result

    def assert_pngs(self, result, expected_pixels):
        directory = Path(result["directory"]).resolve()
        for entry in result["saved"]["diagnostics"]["entries"]:
            if entry["status"] != "saved":
                self.assertNotIn("path", entry)
                continue
            relative = Path(entry["path"])
            self.assertFalse(relative.is_absolute())
            path = (directory / relative).resolve()
            self.assertTrue(path.is_relative_to(directory))
            self.assertEqual(digest(path), entry["sha256"])
            self.assertEqual(path.stat().st_size, entry["bytes"])
            image = cv2.imdecode(np.frombuffer(path.read_bytes(), np.uint8), cv2.IMREAD_COLOR)
            self.assertIsNotNone(image)
            np.testing.assert_array_equal(image, expected_pixels)

    def test_seventy_images_throttle_provenance_and_png_validation(self):
        folder, result = self.run_case("store-basic")
        self.assertFalse(result["native_run_executed"])
        self.assertEqual(result["saved"]["diagnostics"]["reward_attempts"], 70)
        for name in ("initial", "sixty", "pause", "pause_boundary"):
            self.assertEqual(result[name]["status"], "saved")
        for name in ("throttled", "pause_throttled"):
            self.assertEqual(result[name]["status"], "throttled")
        self.assertEqual(result["duplicate"]["status"], "duplicate")
        self.assertEqual(result["wrong_run"]["status"], "invalid_request")
        for name in ("wrong_generation", "not_png", "wrong_size", "oversize", "clock_backwards"):
            self.assertEqual(result[name]["status"], "failed")
        self.assertEqual(result["oversize"]["error"], "DIAGNOSTIC_FRAME_BYTES_EXCEEDED")
        self.assertEqual(result["clock_backwards"]["error"], "DIAGNOSTIC_CLOCK_MOVED_BACKWARD")
        self.assertEqual(result["late"]["status"], "closed")
        self.assertEqual(result["saved"]["reason"], "ORIGINAL_BUSINESS_REASON")
        self.assert_pngs(result, cv2.imread(str(folder / "before.png")))

    def test_reward_and_failure_quotas_are_independent(self):
        _, result = self.run_case("store-quota")
        for kind in ("rewards", "failures"):
            self.assertEqual([r["status"] for r in result[kind]], ["saved", "saved", "quota_exceeded"])
        index = result["saved"]["diagnostics"]
        self.assertEqual(len(index["entries"]), 4)
        self.assertEqual(index["reserved_bytes"], 4 * 8 * 1024 * 1024)
        self.assertFalse(index["complete"])

    def test_write_failure_keeps_tmp_no_saved_receipt_no_retry(self):
        _, result = self.run_case("store-write-fail")
        self.assertEqual(result["first"]["status"], "saved")
        self.assertEqual(result["receipt"]["status"], "failed")
        self.assertEqual(result["receipt"]["error"], "STORAGE_COMMIT_FAILED")
        self.assertEqual(result["replay"]["status"], "duplicate")
        self.assertEqual(result["temporary_files"], 1)
        self.assertFalse(result["saved"]["diagnostics"]["complete"])
        self.assertEqual(result["saved"]["reason"], "ORIGINAL_BUSINESS_REASON")

    def test_reparse_diagnostic_directory_never_writes_outside(self):
        _, result = self.run_case("store-reparse")
        self.assertEqual(result["receipt"]["status"], "failed")
        self.assertTrue(result["outside_empty"])

    def test_guarded_precondition_failure_uses_existing_frame(self):
        folder, result = self.run_case("guard-pre")
        self.assertEqual(result["saved"]["reason"], "TARGET_NOT_FOUND")
        self.assertEqual(result["inputs"], 0)
        entries = result["saved"]["diagnostics"]["entries"]
        self.assertEqual(len(entries), 1)
        self.assertEqual(entries[0]["stage"], "pre_action")
        self.assertEqual(entries[0]["frame"]["frame_id"], result["captures"])
        self.assert_pngs(result, cv2.imread(str(folder / "before.png")))

    def test_guarded_timeout_uses_last_postcondition_frame(self):
        folder, result = self.run_case("guard-post")
        self.assertEqual(result["saved"]["reason"], "POSTCONDITION_TIMEOUT")
        self.assertEqual(result["inputs"], 1)
        entries = result["saved"]["diagnostics"]["entries"]
        self.assertEqual(len(entries), 1)
        self.assertEqual(entries[0]["stage"], "postcondition")
        self.assertEqual(entries[0]["frame"]["frame_id"], result["captures"])
        self.assert_pngs(result, cv2.imread(str(folder / "after.png")))

    def test_recovery_entry_capture_failure_and_time_boundary(self):
        for case in ("recovery", "recovery-capture-fail", "wait-boundary"):
            with self.subTest(case=case):
                folder, result = self.run_case(case)
                saved = result["saved"]
                self.assertEqual(saved["state"], "Interrupted")
                self.assertEqual(saved["reason"], "RECOVERY_REQUIRED")
                self.assertTrue(saved["quiescent"] and saved["result_saved"])
                expected = "leap.wait_boundary" if case == "wait-boundary" else "original.recovery"
                self.assertEqual(saved["sessions"][0]["reason"], expected)
                self.assertEqual(result["inputs"], 0)
                entries = saved["diagnostics"]["entries"]
                self.assertEqual(result["captures"], 1 if case == "wait-boundary" else 2)
                if case == "wait-boundary":
                    self.assertEqual(entries, [])
                else:
                    self.assertEqual(entries[0]["stage"], "recovery_entry")
                    self.assertEqual(entries[0]["status"], "failed" if case.endswith("fail") else "saved")
                    self.assert_pngs(result, cv2.imread(str(folder / "before.png")))

    def test_real_wvd_reward_confirmation_saves_once_before_close(self):
        folder, result = self.run_case("reward")
        self.assertTrue(result["native_run_executed"])
        self.assertEqual(result["saved"]["state"], "Completed")
        self.assertEqual(result["saved"]["business"]["fishing"]["caught"], 1)
        self.assertEqual(result["inputs"], 1)
        entries = result["saved"]["diagnostics"]["entries"]
        self.assertEqual(len(entries), 1)
        self.assertEqual(entries[0]["stage"], "reward")
        self.assertTrue(entries[0]["operation_id"])
        self.assert_pngs(result, cv2.imread(str(folder / "before.png")))


if __name__ == "__main__":
    unittest.main()
