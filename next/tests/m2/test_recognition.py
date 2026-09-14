"""M2 首个完整离线链：真实 DLL 的模板/OCR 三态，不代替运行生命周期验收。"""

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


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


class NativeRecognitionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.config = json.loads(
            (ROOT / ".local/maafw.json").read_text(encoding="utf-8")
        )
        cls.sdk = Path(cls.config["sdk"])
        for relative, expected in cls.config["sdk_files"].items():
            if sha(cls.sdk / relative) != expected:
                raise AssertionError("SDK file changed: " + relative)
        cls.exe = ROOT / "build/m2/Release/test_recognition.exe"
        if not cls.exe.is_file():
            raise AssertionError("先显式构建 M2 离线目标；不跳过未构建的核心测试")
        folder = ROOT / ".local/m2-runs"
        folder.mkdir(parents=True, exist_ok=True)
        cls.run_root = Path(tempfile.mkdtemp(prefix="recognition-", dir=folder))
        cls.results = {}
        print("M2 evidence: " + str(cls.run_root), flush=True)

    @classmethod
    def tearDownClass(cls):
        (cls.run_root / "results.json").write_text(
            json.dumps(cls.results, indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8",
        )

    def case(self, *, ocr=False, frame_kind="positive", **options):
        case_root = self.run_root / self._testMethodName
        case_root.mkdir()
        bundle = case_root / "资源 包"
        (bundle / "image").mkdir(parents=True)
        (bundle / "pipeline").mkdir()
        (bundle / "pipeline/empty.json").write_text("{}", encoding="utf-8")
        rng = np.random.default_rng(7403)
        template = rng.integers(30, 240, (40, 80, 3), dtype=np.uint8)
        frame = rng.integers(10, 85, (1600, 900, 3), dtype=np.uint8)
        if frame_kind == "positive":
            frame[417:457, 334:414] = template
        if ocr:
            frame[:] = 255
            cv2.putText(
                frame,
                "Pause",
                (280, 690),
                cv2.FONT_HERSHEY_SIMPLEX,
                3.0,
                (0, 0, 0),
                5,
                cv2.LINE_AA,
            )
            model = bundle / "model/ocr"
            model.mkdir(parents=True)
            for name in self.config["ocr_files"]:
                shutil.copyfile(Path(self.config["ocr"]) / name, model / name)
        # imencode + bytes 支持中文目录，避免依赖 OpenCV 的窄字符文件 API。
        (bundle / "image/target.png").write_bytes(
            cv2.imencode(".png", template)[1].tobytes()
        )
        frame_path = case_root / "frame.png"
        frame_path.write_bytes(cv2.imencode(".png", frame)[1].tobytes())
        cfg = {"bundle": str(bundle), "frame": str(frame_path), "ocr": ocr, **options}
        if ocr:
            cfg.setdefault("expected", ["Pause"])
            cfg.setdefault("roi", [230, 550, 500, 220])
        mutate = cfg.pop("mutate", "")
        if mutate == "frame":
            frame_path.write_bytes(b"not an image")
        if mutate == "template":
            (bundle / "image/target.png").write_bytes(b"not an image")
        if mutate == "model":
            (bundle / "model/ocr/rec.onnx").write_bytes(b"not a model")
        if mutate == "missing_model":
            (bundle / "model/ocr/rec.onnx").unlink()
        cfg["files"] = [
            {"path": p.relative_to(bundle).as_posix(), "sha256": sha(p)}
            for p in sorted(bundle.rglob("*"))
            if p.is_file()
        ]
        if mutate == "after_load":
            cfg["mutate_after_load"] = str(bundle / "image/target.png")
        if mutate == "hash":
            cfg["files"][0]["sha256"] = "0" * 64
        if mutate == "unlisted":
            (bundle / "pipeline/unlisted.json").write_text("{}", encoding="utf-8")
        path = case_root / "case.json"
        path.write_text(json.dumps(cfg, ensure_ascii=False), encoding="utf-8")
        env = os.environ.copy()
        env["PATH"] = str(self.sdk / "bin") + os.pathsep + env.get("PATH", "")
        # 超时只算测试失败；这里的测试进程清理不是生产原生取消方案。
        result_path = case_root / "native-results.json"
        result = subprocess.run(
            [str(self.exe), str(path), str(result_path)],
            cwd=case_root,
            env=env,
            capture_output=True,
            timeout=90,
        )
        (case_root / "stdout.log").write_bytes(result.stdout)
        (case_root / "stderr.log").write_bytes(result.stderr)
        self.assertEqual(
            result.returncode,
            0,
            result.stderr.decode("utf-8", errors="replace")[-2000:],
        )
        parsed = json.loads(result_path.read_text(encoding="utf-8"))
        self.assertEqual(parsed["sdk_version"], "v5.13.0")
        self.results[self._testMethodName] = parsed
        return parsed["results"]

    def expect_error(self, results, code):
        for result in results:
            self.assertEqual(result["outcome"], "Error", result)
            self.assertEqual(result["error"], code, result)
            self.assertTrue(result["stage"])
            self.assertIsNone(result["box"])
            self.assertEqual(result["matches"], 0)
            self.assertEqual(result["task_id"], 0, "预检错误不应启动识别任务")

    def test_template_hit(self):
        result = self.case()[0]
        self.assertEqual(result["outcome"], "Hit", result)
        self.assertEqual(result["box"], [334, 417, 80, 40])
        self.assertEqual(result["center"], [374, 437])
        self.assertGreater(result["reco_id"], 0)
        self.assertGreater(result["score"], 0.99)

    def test_template_no_hit(self):
        result = self.case(frame_kind="negative")[0]
        self.assertEqual(result["outcome"], "NoHit", result)
        self.assertEqual(result["engine_status"], 3000)
        self.assertGreater(result["reco_id"], 0)
        self.assertIsNone(result["box"])

    def test_ocr_hit(self):
        result = self.case(ocr=True)[0]
        self.assertEqual(result["outcome"], "Hit", result)
        self.assertEqual(result["text"], "Pause")
        self.assertGreater(result["reco_id"], 0)

    def test_ocr_no_hit(self):
        result = self.case(ocr=True, expected=["NoSuchText"])[0]
        self.assertEqual(result["outcome"], "NoHit", result)
        self.assertEqual(result["engine_status"], 3000)
        self.assertGreater(result["reco_id"], 0)

    def test_ocr_literal_not_regex(self):
        self.assertEqual(self.case(ocr=True, expected=[".*"])[0]["outcome"], "NoHit")

    def test_ocr_empty_expected(self):
        self.expect_error(self.case(ocr=True, expected=[]), "OCR_EXPECTED_INVALID")

    def test_ocr_bad_model(self):
        self.expect_error(self.case(ocr=True, mutate="model"), "OCR_MODEL_NOT_LOCKED")

    def test_ocr_missing_model(self):
        self.expect_error(
            self.case(ocr=True, mutate="missing_model"), "RESOURCE_NOT_IN_MANIFEST"
        )

    def test_bad_frame(self):
        self.expect_error(self.case(mutate="frame"), "FRAME_DECODE_INVALID")

    def test_bad_template(self):
        self.expect_error(self.case(mutate="template"), "TEMPLATE_DECODE_INVALID")

    def test_missing_template(self):
        self.expect_error(self.case(template="absent.png"), "RESOURCE_NOT_IN_MANIFEST")

    def test_path_traversal(self):
        self.expect_error(
            self.case(template="../../outside.png"), "RESOURCE_PATH_INVALID"
        )

    def test_manifest_mismatch(self):
        self.expect_error(self.case(mutate="hash"), "RESOURCE_HASH_MISMATCH")

    def test_resource_changed_after_load(self):
        # PERF-INTEGRITY 指定等价断言：活动文件改写必须被共享锁阻止，原图仍能识别。
        self.assertEqual(self.case(mutate="after_load")[0]["outcome"], "Hit")

    def test_unlisted_resource(self):
        self.expect_error(self.case(mutate="unlisted"), "RESOURCE_NOT_IN_MANIFEST")

    def test_invalid_roi(self):
        self.expect_error(self.case(roi=[0, 0, 901, 1600]), "ROI_INVALID")

    def test_template_exceeds_roi(self):
        self.expect_error(self.case(roi=[0, 0, 20, 20]), "TEMPLATE_EXCEEDS_ROI")

    def test_invalid_threshold(self):
        self.expect_error(self.case(threshold=1.1), "THRESHOLD_INVALID")

    def test_old_generation(self):
        self.expect_error(self.case(mismatch="generation"), "FRAME_CONTEXT_MISMATCH")

    def test_old_epoch(self):
        self.expect_error(self.case(mismatch="epoch"), "FRAME_STALE")

    def test_old_frame(self):
        self.expect_error(self.case(mismatch="frame"), "FRAME_STALE")

    def test_future_frame(self):
        self.expect_error(self.case(mismatch="future"), "FRAME_TIME_INVALID")

    def test_decoded_size_mismatch(self):
        self.expect_error(self.case(mismatch="decoded_size"), "FRAME_DECODE_INVALID")

    def test_other_device(self):
        self.expect_error(self.case(mismatch="device"), "FRAME_CONTEXT_MISMATCH")

    def test_other_pack(self):
        self.expect_error(self.case(mismatch="pack"), "FRAME_CONTEXT_MISMATCH")

    def test_other_viewport(self):
        self.expect_error(self.case(mismatch="viewport"), "FRAME_VIEWPORT_MISMATCH")

    def test_other_color(self):
        self.expect_error(self.case(mismatch="color"), "FRAME_COLOR_INVALID")

    def test_wrong_image_size(self):
        self.expect_error(self.case(mismatch="size"), "FRAME_ASPECT_MISMATCH")

    def test_repeated_actual_recognition(self):
        results = self.case(repeat=5)
        self.assertEqual([r["outcome"] for r in results], ["Hit"] * 5)
        self.assertEqual(len({r["reco_id"] for r in results}), 5)
        self.assertEqual(len({r["task_id"] for r in results}), 5)


if __name__ == "__main__":
    unittest.main()
