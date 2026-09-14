"""M3 收尾的真实 SDK/受控离线动作验证；不连接设备。"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

import cv2
import numpy as np

ROOT = Path(__file__).resolve().parents[2]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def reco(image, mode="template", **parameters):
    return {"id": image, "revision": "1", "type": "custom", "binding": "WvdVision",
            "roi": [0, 0, 900, 1600],
            "parameters": {"mode": mode, "image": image, "threshold": 0.99, **parameters}}


class FixTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = Path(tempfile.mkdtemp(prefix="m3-fixes-", dir=ROOT / ".local"))
        cls.sdk = Path(json.loads((ROOT / ".local/maafw.json").read_text(encoding="utf-8"))["sdk"])
        cls.env = dict(os.environ, PATH=str(cls.sdk / "bin") + os.pathsep + os.environ.get("PATH", ""))
        print("M3 fixes evidence: " + str(cls.root), flush=True)

    def execute(self, folder, config, nodes):
        bundle = folder / "bundle"
        (bundle / "pipeline").mkdir()
        for node in nodes.values():
            node.update(pre_delay=0, post_delay=0)
        (bundle / "pipeline/main.json").write_text(json.dumps(nodes), encoding="utf-8")
        config.update(bundle=str(bundle), files=[{"path": f.relative_to(bundle).as_posix(), "sha256": digest(f)}
                                                  for f in sorted(bundle.rglob("*")) if f.is_file()],
                      before=str(folder / "before.png"), after=str(folder / "after.png"),
                      run_root=str(folder / "run-data"), output=str(folder / "output.json"))
        if config.get("scenario") == "junction":
            destination = folder / "junction-destination"
            destination.mkdir()
            # Junction 不需要符号链接特权；目标和链接都属于本用例目录。
            created = subprocess.run(["cmd.exe", "/c", "mklink", "/J", str(bundle / "linked"), str(destination)],
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
            self.assertEqual(created.returncode, 0, created.stdout.decode(errors="replace"))
        (folder / "input.json").write_text(json.dumps(config), encoding="utf-8")
        exe = ROOT / "build/m3/Release/test_m3_fixes.exe"
        with (folder / "native.log").open("wb") as log:
            done = subprocess.run([str(exe), str(folder / "input.json")], cwd=folder,
                                  env=self.env, stdout=log, stderr=log, timeout=45)
        (folder / "execution.json").write_text(json.dumps({"exe_sha256": digest(exe), "exit": done.returncode}), encoding="utf-8")
        self.assertEqual(done.returncode, 0, (folder / "native.log").read_text(encoding="utf-8", errors="replace")[-2500:])
        result = json.loads((folder / "output.json").read_text(encoding="utf-8"))
        for module in result["loaded_modules"]:
            self.assertEqual(Path(module["path"]).resolve(), (self.sdk / "bin" / module["name"]).resolve())
            self.assertEqual(module["sha256"], digest(self.sdk / "bin" / module["name"]))
        return result

    def images(self, name, position=(334, 417)):
        folder = self.root / name
        (folder / "bundle/image").mkdir(parents=True)
        rng = np.random.default_rng(783)
        frame = rng.integers(0, 60, (1600, 900, 3), dtype=np.uint8)
        target = rng.integers(0, 255, (40, 80, 3), dtype=np.uint8)
        post = rng.integers(0, 255, (40, 80, 3), dtype=np.uint8)
        x, y = position
        frame[y:y+40, x:x+80] = target
        after = frame.copy()
        after[y:y+40, x:x+80] = post
        for key, image in {"before": frame, "after": after, "bundle/image/scene": frame[80:140, 40:130],
                           "bundle/image/target": target, "bundle/image/next": target,
                           "bundle/image/post": post}.items():
            (folder / (key + ".png")).write_bytes(cv2.imencode(".png", image)[1].tobytes())
        return folder

    def test_roi_direct_and_pipeline(self):
        for position, expected in [((334, 417), "Hit"), ((334, 1000), "NoHit")]:
            with self.subTest(position=position):
                folder = self.images("roi-" + str(position[1]), position)
                cases = [
                    ("default", reco("next", default_roi=True), expected),
                    ("explicit", reco("next", default_roi=True, roi=[300, position[1]-10, 200, 100]), "Hit"),
                    ("exclude", reco("next", roi=[300, position[1]-10, 200, 100],
                                      exclude=[[334, position[1], 80, 40]]), "NoHit"),
                    ("scope", {**reco("next"), "roi": [300, position[1]-10, 200, 100]}, "Hit"),
                    ("outside-scope", {**reco("next", roi=[0, 0, 900, 1600]), "roi": [300, 400, 200, 100]}, "Error"),
                    ("null", reco("next", roi=None, default_roi=True), "Error"),
                    ("empty", reco("next", roi=[], default_roi=True), "Error"),
                    ("outside", reco("next", roi=[899, 1599, 30, 30]), "Error"),
                ]
                nodes = {}
                for name, request, _ in cases:
                    nodes[name] = {"next": [name + "_candidate"], "timeout": 50}
                    nodes[name + "_candidate"] = {"recognition": "Custom", "custom_recognition": "WvdVision",
                        "roi": request["roi"], "custom_recognition_param": request["parameters"], "action": "DoNothing"}
                result = self.execute(folder, {"mode": "roi", "cases": [{"id": n, "request": r} for n,r,_ in cases]}, nodes)
                for (_, _, outcome), direct, pipeline in zip(cases, result["cases"], result["pipelines"]):
                    self.assertEqual(direct["outcome"], {"Hit": 0, "NoHit": 1, "Error": 2}[outcome], direct)
                    self.assertTrue(pipeline["events"], pipeline)
                    self.assertTrue(all(e["outcome"] == outcome for e in pipeline["events"]), pipeline)
                self.assertEqual(result["cases"][0]["evidence"]["effective_roi"], [80, 220, 819, 680])
                self.assertEqual(result["cases"][0]["evidence"]["roi_source"], "default")
                self.assertEqual(result["backend_calls"], 0)

    def test_guarded_custom_causal_chain(self):
        for name in ("normal", "boolean-target", "fixed", "key", "missing-binding", "nohit", "post-failed", "low-confidence", "offset", "fractional-offset", "composite-low-confidence"):
            with self.subTest(name=name):
                folder = self.images("guarded-" + name)
                params = {"scene": "battle", "scene_recognition": reco("scene", "focus_cursor"),
                          "target_recognition": reco("target"), "postcondition": reco("post", "focus_cursor"),
                          "command": {"kind": "Click"}, "allowed_area": [0, 0, 900, 1600],
                          "postcondition_timeout_ms": 150}
                if name in ("boolean-target", "fixed", "key"):
                    params["target_recognition"] = reco("target", "focus_cursor")
                if name == "fixed":
                    params.update(use_target_center=False, command={"kind": "Click", "x": 374, "y": 437})
                if name == "key":
                    params["command"] = {"kind": "ClickKey", "key": 32}
                if name == "missing-binding":
                    params["scene_recognition"]["binding"] = "Unknown"
                if name == "nohit":
                    params["target_recognition"] = reco("post")
                if name == "low-confidence":
                    params["target_recognition"] = reco("next", "next_low_confidence")
                if name == "offset":
                    params["target_offset"] = [-35, -35]
                if name == "fractional-offset":
                    params["target_offset"] = [1.5, 2]
                if name == "composite-low-confidence":
                    params["target_recognition"] = {**reco("wrapped"), "parameters": {
                        "mode": "all", "conditions": [reco("next", "next_low_confidence")["parameters"]]}}
                    params.update(use_target_center=False, command={"kind": "Click", "x": 374, "y": 437})
                nodes = {"Entry": {"action": "Custom", "custom_action": "GuardedAction", "custom_action_param": params,
                                   "next": ["Terminal"]},
                         "Terminal": {"action": "Custom", "custom_action": "RootTerminal"}}
                result = self.execute(folder, {"mode": "guarded", "expected_input":
                        {"kind": 5 if name == "key" else 0, "x": 0 if name == "key" else 339 if name == "offset" else 374,
                         "y": 0 if name == "key" else 402 if name == "offset" else 437}, "change_frame": name != "post-failed"}, nodes)
                success = name in ("normal", "fixed", "key", "offset")
                self.assertEqual(result["snapshot"]["state"], "Completed" if success else "Failed", result)
                self.assertEqual(result["backend_calls"], int(success or name == "post-failed"), result)
                self.assertFalse(result["mismatch"], result)
                self.assertTrue(result["snapshot"]["quiescent"])
                if name == "boolean-target":
                    self.assertEqual(result["snapshot"]["reason"], "TARGET_NOT_POSITIONAL")
                if name in ("low-confidence", "composite-low-confidence"):
                    self.assertEqual(result["snapshot"]["reason"], "TARGET_REQUIRES_CONFIRMATION")
                if name == "fractional-offset":
                    self.assertEqual(result["snapshot"]["reason"], "TARGET_OFFSET_INVALID")

    def test_composite_conditions_preserve_errors_and_boolean_contract(self):
        folder = self.images("conditions")
        hit = reco("target")["parameters"]
        nohit = reco("post")["parameters"]
        invalid = reco("missing")["parameters"]
        cases = [("all-hit", {"mode": "all", "conditions": [hit, hit]}, "Hit"),
                 ("all-nohit", {"mode": "all", "conditions": [hit, nohit]}, "NoHit"),
                 ("any-hit", {"mode": "any", "conditions": [nohit, hit]}, "Hit"),
                 ("not-hit", {"mode": "not", "conditions": [hit]}, "NoHit"),
                 ("not-nohit", {"mode": "not", "conditions": [nohit]}, "Hit"),
                 ("error-not-hidden", {"mode": "any", "conditions": [hit, invalid]}, "Error"),
                 ("empty", {"mode": "all", "conditions": []}, "Error"),
                 ("parent-roi", {"mode": "all", "conditions": [hit], "roi": [0, 0, 1, 1]}, "Error")]
        deep = hit
        for _ in range(10):
            deep = {"mode": "not", "conditions": [deep]}
        cases.append(("depth", deep, "Error"))
        nodes, requests = {}, []
        for name, parameters, _ in cases:
            request = {**reco(name), "parameters": parameters}
            requests.append({"id": name, "request": request})
            nodes[name] = {"next": [name + "_candidate"], "timeout": 50}
            nodes[name + "_candidate"] = {"recognition": "Custom", "custom_recognition": "WvdVision",
                "roi": request["roi"], "custom_recognition_param": parameters, "action": "DoNothing"}
        result = self.execute(folder, {"mode": "roi", "cases": requests}, nodes)
        for (_, _, outcome), direct, pipeline in zip(cases, result["cases"], result["pipelines"]):
            self.assertEqual(direct["outcome"], {"Hit": 0, "NoHit": 1, "Error": 2}[outcome], direct)
            self.assertFalse(direct["center"], direct)
            self.assertTrue(pipeline["events"], pipeline)
            self.assertTrue(all(e["outcome"] == outcome for e in pipeline["events"]), pipeline)
        self.assertEqual(result["backend_calls"], 0)

    def test_metadata_helper_only(self):
        folder = self.root / "metadata-中文 空格"
        folder.mkdir()
        helper = folder / "wvd_metadata_helper.exe"
        shutil.copy2(ROOT / "build/m3/Release/wvd_metadata_helper.exe", helper)
        exe = ROOT / "build/m3/Release/test_metadata.exe"
        with (folder / "control.log").open("wb") as log:
            control_process = subprocess.run([str(exe), str(helper), str(folder / "control.json"), "--creation-control"],
                                  cwd=folder, env=self.env, stdout=log, stderr=log, timeout=10)
        self.assertEqual(control_process.returncode, 0)
        control = json.loads((folder / "control.json").read_text(encoding="utf-8"))
        self.assertFalse(control["watchdog"])
        with (folder / "native.log").open("wb") as log:
            done = subprocess.run([str(exe), str(helper), str(folder / "results.json")],
                                  cwd=folder, env=self.env, stdout=log, stderr=log, timeout=30)
        self.assertEqual(done.returncode, 0)
        for index, case in enumerate(json.loads((folder / "results.json").read_text(encoding="utf-8"))):
            self.assertTrue(case["pass"], case)
            # 首次进程创建的系统增量必须与独立对照一致；其余每次仍要求进程总句柄回到本次基线。
            self.assertEqual(case["handles_after"] - case["handles_before"], control["delta"] if index == 0 else 0, case)

    def test_integrity_snapshot(self):
        folder = self.images("integrity")
        result = self.execute(folder, {"mode": "integrity", "request": reco("target")}, {"Entry": {"action": "DoNothing"}})
        for key in ("write_blocked", "delete_blocked", "replace_blocked", "one_boundary", "author_change_ignored", "released_write_succeeded"):
            self.assertTrue(result[key], result)
        self.assertEqual(result["matched"], 0)
        self.assertEqual(result["member_change_error"], "RESOURCE_NOT_IN_MANIFEST")
        self.assertEqual(result["old_manifest_error"], "RESOURCE_HASH_MISMATCH")
        self.assertEqual(result["reused_revision_error"], "BUNDLE_REVISION_REUSED")
        self.assertFalse(result["closed"]["active"])
        self.assertEqual(result["backend_calls"], 0)

    def test_integrity_negative_matrix_and_failed_initialization_release(self):
        expected = {"valid": None, "writer-held": "INTEGRITY_SHARING_CONFLICT",
                    "bad-hash": "RESOURCE_HASH_MISMATCH", "bad-manifest": "BUNDLE_MANIFEST_INVALID",
                    "case-collision": "BUNDLE_CASE_COLLISION", "missing-member": "INTEGRITY_SHARING_CONFLICT",
                    "directory-rename": None, "directory-added": "BUNDLE_DIRECTORY_CHANGED", "junction": "BUNDLE_LINK_REJECTED"}
        for case, error in expected.items():
            with self.subTest(case=case):
                folder = self.images("lease-" + case)
                result = self.execute(folder, {"mode": "lease-matrix", "scenario": case}, {"Entry": {"action": "DoNothing"}})
                self.assertEqual(result.get("error"), error, result)
                self.assertTrue(result["all_locks_released"], result)
                self.assertTrue(result["target_unchanged"], result)
                self.assertEqual(result["backend_calls"], 0)
                if case == "directory-rename":
                    self.assertTrue(result["rename_blocked"])


if __name__ == "__main__":
    unittest.main()
