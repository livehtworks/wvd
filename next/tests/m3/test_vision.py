"""隔离图像、真实 Maa 自定义识别与固定 OpenCV；不连接任何设备。"""

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
from legacy_reference import extract, extract_bobber

ROOT = Path(__file__).resolve().parents[2]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class VisionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = Path(tempfile.mkdtemp(prefix="m3-vision-", dir=ROOT / ".local"))
        cls.sdk = Path(
            json.loads((ROOT / ".local/maafw.json").read_text(encoding="utf-8"))["sdk"]
        )
        cls.exe = ROOT / "build/m3/Release/test_m3.exe"
        cls.exe_hash = sha(cls.exe)
        cls.manifest = json.loads(
            (ROOT / "packs/wvd/manifest.json").read_text(encoding="utf-8")
        )
        print("M3 offline evidence: " + str(cls.folder), flush=True)

    def run_fixture(
        self, name, cases, full=False, pipeline=False, device=False, patches=()
    ):
        root = self.folder / name
        bundle = root / "bundle"
        (bundle / "image").mkdir(parents=True)
        (bundle / "pipeline").mkdir()
        if full:
            shutil.copytree(
                ROOT / "packs/wvd/image", bundle / "image", dirs_exist_ok=True
            )
        rng = np.random.default_rng(623)
        frame = rng.integers(0, 65, (1600, 900, 3), dtype=np.uint8)
        target = rng.integers(0, 255, (40, 80, 3), dtype=np.uint8)
        frame[417:457, 334:414] = target
        for x, y, patch in patches:
            frame[y : y + patch.shape[0], x : x + patch.shape[1]] = patch
        (bundle / "image/scene.png").write_bytes(
            cv2.imencode(".png", frame[80:140, 40:130])[1].tobytes()
        )
        (bundle / "image/fixture.png").write_bytes(
            cv2.imencode(".png", target)[1].tobytes()
        )
        (bundle / "image/bad.png").write_bytes(b"invalid png")
        nodes = {
            "VisionPipeline": {
                "recognition": "Custom",
                "custom_recognition": "WvdVision",
                "custom_recognition_param": {
                    "mode": "template",
                    "image": "fixture.png",
                    "threshold": 0.99,
                },
                "action": "DoNothing",
                "pre_delay": 0,
                "post_delay": 0,
            }
        }
        nodes["VisionCandidate"] = nodes.pop("VisionPipeline")
        nodes["VisionPipeline"] = {
            "next": ["VisionCandidate"],
            "action": "DoNothing",
            "pre_delay": 0,
            "post_delay": 0,
        }
        (bundle / "pipeline/vision.json").write_text(
            json.dumps(nodes), encoding="utf-8"
        )
        image = root / "frame.png"
        image.write_bytes(cv2.imencode(".png", frame)[1].tobytes())
        config = {
            "bundle": str(bundle),
            "revision": self.manifest["revision"] if full else "synthetic-m3-1",
            "aliases": self.manifest["aliases"],
            "frame": str(image),
            "files": [
                {"path": p.relative_to(bundle).as_posix(), "sha256": sha(p)}
                for p in sorted(bundle.rglob("*"))
                if p.is_file()
            ],
            "cases": cases,
            "pipeline": pipeline,
            "output": str(root / "result.json"),
        }
        path = root / "input.json"
        config.update(
            device_checks=device,
            before=str(image),
            after=str(image),
            device_id="m2-offline",
        )
        if name == "custom":
            mod = root / "isolated-mod"
            (mod / "image").mkdir(parents=True)
            for filename in ("fixture.png", "modOnly.png"):
                (mod / "image" / filename).write_bytes(
                    cv2.imencode(".png", np.full((40, 80, 3), 7, np.uint8))[1].tobytes()
                )
            config["mod_fixture"] = {
                "root": str(mod),
                "files": [
                    {"path": p.relative_to(mod).as_posix(), "sha256": sha(p)}
                    for p in sorted(mod.rglob("*.png"))
                ],
            }
        path.write_text(json.dumps(config), encoding="utf-8")
        env = os.environ.copy()
        env["PATH"] = str(self.sdk / "bin") + os.pathsep + env.get("PATH", "")
        self.assertEqual(sha(self.exe), self.exe_hash)
        with (root / "native.log").open("wb") as log:
            done = subprocess.run(
                [str(self.exe), str(path)],
                cwd=root,
                env=env,
                stdout=log,
                stderr=log,
                timeout=120,
            )
        self.assertEqual(
            done.returncode,
            0,
            (root / "native.log").read_text(encoding="utf-8", errors="replace")[-2500:],
        )
        result = json.loads((root / "result.json").read_text(encoding="utf-8"))
        if device:
            for case in result:
                self.assertEqual(case["outcome"], "PASS", case)
            return result
        self.assertTrue(result["quiescent"])
        self.assertEqual(result["backend_inputs"], 0)
        for case in result["cases"]:
            self.assertTrue(case["pass"], case)
        return result

    def test_resource_source_and_aliases(self):
        for file in self.manifest["files"]:
            self.assertEqual(sha(ROOT / "packs/wvd" / file["path"]), file["sha256"])
        self.assertEqual(self.manifest["case_reference_count"], 8)
        self.assertEqual(len(self.manifest["dynamic_references"]), 44)
        for target in self.manifest["aliases"].values():
            self.assertIn(
                "image/" + target, [f["path"] for f in self.manifest["files"]]
            )

    def test_device_faults_and_context_gate(self):
        self.run_fixture("device-contract", [], device=True)

    def test_custom_contract_and_pipeline(self):
        cases = []

        def add(name, mode="template", expected="Hit", **params):
            cases.append(
                {
                    "id": name,
                    "expected": expected,
                    "parameters": {"mode": mode, "image": "fixture.png", **params},
                }
            )

        add("normal", threshold=0.99)
        add("roi", roi=[300, 400, 160, 100], threshold=0.99)
        add(
            "exclude",
            expected="NoHit",
            roi=[300, 400, 160, 100],
            exclude=[[334, 417, 80, 40]],
            threshold=0.99,
        )
        add("bright", mode="bright_mask", threshold=0.99)
        add("multiple", mode="multiple", threshold=0.99)
        add("missing", expected="Error", image="missing.png")
        add("bad", expected="Error", image="bad.png")
        add("escape", expected="Error", image="../fixture.png")
        add("bad-roi", expected="Error", roi=[890, 1590, 20, 20])
        add("bad-threshold", expected="Error", threshold=1.1)
        add("pause-ocr-unavailable", mode="pause_ocr", expected="Error")
        result = self.run_fixture("custom", cases, pipeline=True)
        self.assertEqual(result["pipeline_status"], 3000)
        hit = result["cases"][0]
        self.assertEqual(hit["evidence"]["box"], [334, 417, 80, 40])
        nohit = result["cases"][2]
        self.assertIsNone(nohit["evidence"]["box"])
        self.assertIn("best_score", nohit["evidence"]["evidence"])
        self.assertTrue(
            any(
                e["outcome"] == "Hit"
                and e["binding"]["implementation_id"] == "wvd.vision"
                for e in result["pipeline_events"]
            )
        )
        self.assertEqual(len(result["pipeline_events"]), 1)
        old = extract(ROOT.parent)
        folder = self.folder / "custom"
        frame = cv2.imdecode(
            np.fromfile(folder / "frame.png", dtype=np.uint8), cv2.IMREAD_COLOR
        )
        template = cv2.imdecode(
            np.fromfile(folder / "bundle/image/fixture.png", dtype=np.uint8),
            cv2.IMREAD_COLOR,
        )
        comparison = []
        for index, roi in (
            (0, None),
            (1, [[300, 400, 160, 100]]),
            (2, [[300, 400, 160, 100], [334, 417, 80, 40]]),
            (3, None),
        ):
            fn = old["_check_bright_mask"] if index == 3 else old["_check"]
            before = frame.copy()
            position, score = fn(frame, template, roi)
            self.assertTrue(np.array_equal(frame, before))
            current = result["cases"][index]["evidence"]["evidence"]
            self.assertAlmostEqual(score, current["best_score"], delta=0.00001)
            box = current["best_box"]
            self.assertEqual(position, [box[0] + box[2] // 2, box[1] + box[3] // 2])
            comparison.append(
                {
                    "id": index,
                    "legacy_score": float(score),
                    "current_score": current["best_score"],
                    "same_position": True,
                }
            )
        (folder / "legacy-comparison.json").write_text(
            json.dumps(comparison, indent=2), encoding="utf-8"
        )

    def test_full_resource_negative_paths(self):
        modes = [
            "next",
            "pause",
            "pause_negative",
            "combat_active",
            "skill_level",
            "reached",
            "through_stair",
        ]
        cases = [
            {
                "id": mode,
                "expected": "Hit" if mode == "through_stair" else "NoHit",
                "parameters": {
                    "mode": mode,
                    "image": "stair_up",
                    "position": [450, 600],
                    "level": 1,
                },
            }
            for mode in modes
        ]
        self.run_fixture("full", cases, full=True)

    def test_fixed_cost_window(self):
        # 固定 5 次预热 + 30 次正式；完整包和小包同一合成帧/参数，不据此宣称真实游戏速度。
        cases = [
            {
                "id": str(i),
                "expected": "Hit",
                "parameters": {
                    "mode": "template",
                    "image": "fixture.png",
                    "roi": [300, 400, 160, 100],
                    "threshold": 0.99,
                },
            }
            for i in range(-5, 30)
        ]
        reports = {
            "small": self.run_fixture("cost-small", cases),
            "full": self.run_fixture("cost-full", cases, full=True),
        }
        (self.folder / "cost-results.json").write_text(
            json.dumps(reports, indent=2), encoding="utf-8"
        )

    def test_synthetic_pause_layout(self):
        # 合成正例只证明布局分支，不冒充真实 Pause 样本。
        root = self.folder / "pause-source"
        root.mkdir()
        old = extract(ROOT.parent)
        roi = np.full((110, 240, 3), 20, np.uint8)
        cv2.putText(
            roi,
            "Pause",
            (40, 55),
            cv2.FONT_HERSHEY_SIMPLEX,
            1.0,
            (190, 190, 190),
            2,
            cv2.LINE_AA,
        )
        expected = old["CheckPauseTextLayout"](roi)
        (root / "legacy-layout.json").write_text(
            json.dumps([bool(expected[0]), *map(int, expected[1:])]), encoding="utf-8"
        )
        self.assertTrue(expected[0])
        result = self.run_fixture(
            "pause-layout",
            [
                {
                    "id": "layout",
                    "expected": "Hit",
                    "parameters": {"mode": "pause_layout", "roi": [330, 740, 240, 110]},
                },
                {"id": "overlay", "expected": "Hit", "parameters": {"mode": "pause"}},
            ],
            full=True,
            patches=[(330, 740, roi)],
        )
        detail = result["cases"][0]["evidence"]["evidence"]
        self.assertEqual(
            [detail["components"], detail["text_width"], detail["text_height"]],
            list(expected[1:]),
        )

    def test_color_preprocessing_and_visual_families(self):
        old = extract(ROOT.parent)
        root = self.folder / "pixel-ops"
        root.mkdir()
        frame = np.random.default_rng(445).integers(0, 256, (8, 8, 3), dtype=np.uint8)
        image = root / "frame.png"
        image.write_bytes(cv2.imencode(".png", frame)[1].tobytes())
        operations = [
            {"subtract": False, "rgb": [2.0, 0.0, 0.75]},
            {"subtract": True, "rgb": [90, 20.5, -12]},
        ]
        config = {
            "frame": str(image),
            "pixel_operations": operations,
            "output": str(root / "result.json"),
        }
        (root / "input.json").write_text(json.dumps(config), encoding="utf-8")
        env = os.environ.copy()
        env["PATH"] = str(self.sdk / "bin") + os.pathsep + env.get("PATH", "")
        done = subprocess.run(
            [str(self.exe), str(root / "input.json")],
            env=env,
            cwd=root,
            capture_output=True,
            timeout=30,
        )
        self.assertEqual(done.returncode, 0, done.stderr)
        for operation, actual in zip(
            operations, json.loads((root / "result.json").read_text())
        ):
            fn = old["MinusImage" if operation["subtract"] else "WrapImage"]
            expected = fn(frame.copy(), *operation["rgb"])
            np.testing.assert_array_equal(
                np.array(actual, dtype=np.uint8).reshape(frame.shape), expected
            )

        def asset(name):
            return cv2.imdecode(
                np.fromfile(ROOT / "packs/wvd/image" / (name + ".png"), dtype=np.uint8),
                cv2.IMREAD_COLOR,
            )

        portrait_path = sorted(
            (ROOT / "packs/wvd/image/spellskill/char").glob("*.png")
        )[0]
        portrait_name = (
            portrait_path.relative_to(ROOT / "packs/wvd/image")
            .as_posix()
            .removesuffix(".png")
        )
        patches = [
            (400, 450, asset("next")),
            (20, 15, asset("combatActive")),
            (24, 55, asset(portrait_name)),
            (380, 650, asset("cursor_0")),
            (300, 1000, asset("spellskill/skillLvl/lv1")),
            (205, 1460, asset("fastforward_off")),
        ]
        cursor = asset("cursor_0")
        center = [380 + cursor.shape[1] // 2, 650 + cursor.shape[0] // 2]
        cases = [
            {"id": "next", "expected": "Hit", "parameters": {"mode": "next"}},
            {
                "id": "next-low",
                "expected": "Hit",
                "parameters": {"mode": "next_low_confidence"},
            },
            {
                "id": "active",
                "expected": "Hit",
                "parameters": {"mode": "combat_active"},
            },
            {
                "id": "portrait",
                "expected": "Hit",
                "parameters": {"mode": "portrait", "image": portrait_name},
            },
            {
                "id": "focus",
                "expected": "Hit",
                "parameters": {"mode": "focus_cursor", "image": "cursor_0"},
            },
            {
                "id": "reached",
                "expected": "Hit",
                "parameters": {"mode": "reached", "position": center},
            },
            {
                "id": "level",
                "expected": "Hit",
                "parameters": {"mode": "skill_level", "level": 1},
            },
            {
                "id": "fast",
                "expected": "Hit",
                "parameters": {"mode": "fast_forward_off"},
            },
            {
                "id": "multiply",
                "expected": "Hit",
                "parameters": {
                    "mode": "template",
                    "image": "fixture",
                    "preprocess": {"operation": "multiply", "rgb": [1, 1, 1]},
                },
            },
            {
                "id": "subtract",
                "expected": "Hit",
                "parameters": {
                    "mode": "template",
                    "image": "fixture",
                    "preprocess": {"operation": "subtract", "rgb": [0, 0, 0]},
                },
            },
        ]
        result = self.run_fixture("families", cases, full=True, patches=patches)
        self.assertEqual(result["cases"][0]["evidence"]["attempts"][0]["image"], "next")
        self.assertEqual(
            result["cases"][7]["evidence"]["evidence"]["legacy_position"], [205, 1460]
        )

    def test_bobber_legacy_parity(self):
        template = cv2.imdecode(
            np.fromfile(ROOT / "packs/wvd/image/fishing/bobber.png", dtype=np.uint8),
            cv2.IMREAD_COLOR,
        )
        legacy = extract_bobber(ROOT.parent, template)
        rng = np.random.default_rng(623)
        frame = rng.integers(0, 65, (1600, 900, 3), dtype=np.uint8)
        frame[417:457, 334:414] = rng.integers(0, 255, (40, 80, 3), dtype=np.uint8)
        expected, _ = legacy(frame.copy())
        result = self.run_fixture(
            "bobber",
            [
                {
                    "id": "bobber",
                    "expected": "Hit" if expected else "NoHit",
                    "parameters": {"mode": "bobber"},
                }
            ],
            full=True,
        )
        actual = result["cases"][0]["evidence"]["evidence"]["detections"]
        self.assertEqual(len(actual), len(expected))
        for current, previous in zip(actual, expected):
            self.assertEqual(current["center"], list(previous[:2]))
            self.assertAlmostEqual(current["score"], previous[2], delta=0.0001)
            self.assertAlmostEqual(current["match_score"], previous[3], delta=0.0001)
        (self.folder / "bobber/legacy-comparison.json").write_text(
            json.dumps({"count": len(expected), "samples": expected}), encoding="utf-8"
        )


if __name__ == "__main__":
    unittest.main()
