"""真实 Maa Pipeline/CustomController 的有限生命周期回归；设备实现只存在于测试驱动。"""

import json
import ctypes
import os
import shutil
from pathlib import Path
import subprocess
import tempfile
import time
import unittest
import uuid

import cv2
import numpy as np

from test_recognition import ROOT, sha


def recognition(image, identity):
    return {
        "id": identity,
        "revision": "1",
        "roi": [0, 0, 900, 900],
        "image": image,
        "threshold": 0.99,
    }


def guarded(kind="Click", **command):
    return {
        "scene": "battle",
        "scene_recognition": recognition("scene.png", "scene"),
        "target_recognition": recognition("target.png", "target"),
        "postcondition": recognition("post.png", "post"),
        "allowed_area": [0, 0, 900, 900],
        "command": {"kind": kind, **command},
        "postcondition_timeout_ms": 150,
    }


def custom(action, params=None, following=None):
    result = {
        "action": "Custom",
        "custom_action": action,
        "custom_action_param": params or {},
    }
    if following:
        result["next"] = following
    return result


def pipeline():
    nodes = {
        "Normal": custom("GuardedAction", guarded(), ["Terminal"]),
        "Terminal": custom("RootTerminal"),
        "Wait": {"next": ["Never"], "timeout": -1},
        "Never": {
            "recognition": "TemplateMatch",
            "template": "never.png",
            "threshold": 0.99,
        },
        "Cooperate": custom("TestWait", following=["Terminal"]),
        "ChildWait": custom("TestWait"),
        "NestedWait": custom("RunChild", {"entry": "ChildWait"}, ["Normal"]),
        "CloneWait": custom(
            "RunChild", {"entry": "ChildWait", "clone": True}, ["Normal"]
        ),
        "False": custom("TestFalse"),
        "Throw": custom("TestThrow", following=["Terminal"]),
        "ChildFailure": custom("RunChild", {"entry": "False"}, ["Normal"]),
        "RootNotTerminal": custom("RunChild", {"entry": "Terminal"}),
        "Recover": custom("RequireRecovery", following=["Normal"]),
        "BareClick": {
            "action": "Click",
            "target": [374, 437, 1, 1],
            "next": ["Terminal"],
        },
        "HeldTouch": custom(
            "GuardedAction", guarded("TouchDown", pressure=500), ["Terminal"]
        ),
        "HeldKey": custom("GuardedAction", guarded("KeyDown", key=32), ["Terminal"]),
        "NativeScroll": custom(
            "GuardedAction", guarded("Scroll", x=0, y=120), ["Terminal"]
        ),
        "NativeSwipe": custom(
            "GuardedAction", guarded("Swipe", x2=470, y2=500, duration=30), ["Terminal"]
        ),
        "CloneInspect": custom("TestClone", following=["Terminal"]),
        "ChildOK": {"action": "DoNothing"},
        "Data": {"roi": [1, 2, 3, 4]},
        "InterruptRoot": {"next": ["[JumpBack]Interrupt", "Terminal"]},
        "Interrupt": {**custom("GuardedAction", guarded()), "max_hit": 1},
    }
    for value in nodes.values():
        value.update(pre_delay=0, post_delay=0, rate_limit=10)
        value.setdefault("timeout", 1000)
    return nodes


def native_resize(frame, sdk):
    """使用被测固定 SDK 的图像重采样构建映射夹具，不复制坐标映射公式。"""
    with os.add_dll_directory(str(sdk / "bin")):
        dll = ctypes.CDLL(str(sdk / "bin/MaaFramework.dll"))
    ptr = ctypes.c_void_p
    dll.MaaImageBufferCreate.restype = ptr
    dll.MaaImageBufferSetRawData.argtypes = [
        ptr,
        ptr,
        ctypes.c_int32,
        ctypes.c_int32,
        ctypes.c_int32,
    ]
    dll.MaaImageBufferResize.argtypes = [ptr, ctypes.c_int32, ctypes.c_int32]
    dll.MaaImageBufferGetRawData.argtypes = [ptr]
    dll.MaaImageBufferGetRawData.restype = ptr
    dll.MaaImageBufferDestroy.argtypes = [ptr]
    image = dll.MaaImageBufferCreate()
    try:
        if not dll.MaaImageBufferSetRawData(
            image, frame.ctypes.data, frame.shape[1], frame.shape[0], 16
        ):
            raise AssertionError("native mapping fixture set failed")
        if not dll.MaaImageBufferResize(image, 900, 1600):
            raise AssertionError("native mapping fixture resize failed")
        data = dll.MaaImageBufferGetRawData(image)
        if not data:
            raise AssertionError("native mapping fixture empty")
        return (
            np.frombuffer(ctypes.string_at(data, 900 * 1600 * 3), dtype=np.uint8)
            .reshape(1600, 900, 3)
            .copy()
        )
    finally:
        dll.MaaImageBufferDestroy(image)


class RuntimeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.config = json.loads(
            (ROOT / ".local/maafw.json").read_text(encoding="utf-8")
        )
        cls.sdk = Path(cls.config["sdk"])
        for name, expected in cls.config["sdk_files"].items():
            if sha(cls.sdk / name) != expected:
                raise AssertionError("SDK changed: " + name)
        cls.exe = ROOT / "build/m2/Release/test_runtime.exe"
        if not cls.exe.is_file():
            raise AssertionError("M2 runtime target has not been built")
        cls.exe_hash = sha(cls.exe)
        root = ROOT / ".local/m2-runs"
        root.mkdir(parents=True, exist_ok=True)
        cls.folder = Path(tempfile.mkdtemp(prefix="runtime-", dir=root))
        cls.results = {}
        print("M2 runtime evidence: " + str(cls.folder), flush=True)

    @classmethod
    def tearDownClass(cls):
        (cls.folder / "results.json").write_text(
            json.dumps(cls.results, indent=2) + "\n", encoding="utf-8"
        )

    def execute_case(self, name):
        self.assertEqual(
            sha(self.exe), self.exe_hash, "test executable changed during validation"
        )
        root = self.folder / name
        bundle = root / "独立资源"
        (bundle / "image").mkdir(parents=True)
        (bundle / "pipeline").mkdir()
        (bundle / "pipeline/cases.json").write_text(
            json.dumps(pipeline()), encoding="utf-8"
        )
        rng = np.random.default_rng(17409)
        frame = rng.integers(20, 80, (1600, 900, 3), dtype=np.uint8)
        patterns = {}
        for image, dimensions in (
            ("scene", (60, 90)),
            ("target", (40, 80)),
            ("post", (60, 100)),
            ("never", (40, 60)),
        ):
            # 留出模板外缘，避免两次缩放把随机背景混入模板边界；阈值仍固定 0.99。
            pattern = rng.integers(
                0, 255, (dimensions[0] + 16, dimensions[1] + 16, 3), dtype=np.uint8
            )
            patterns[image] = cv2.GaussianBlur(pattern, (11, 11), 2.5)
            (bundle / f"image/{image}.png").write_bytes(
                cv2.imencode(".png", patterns[image][8:-8, 8:-8])[1].tobytes()
            )
        frame[72:148, 32:138] = patterns["scene"]
        frame[409:465, 326:422] = patterns["target"]
        after = frame.copy()
        after[92:168, 592:708] = patterns["post"]
        if name == "large-frame":
            frame = cv2.resize(frame, (1080, 1920), interpolation=cv2.INTER_LINEAR)
            after = cv2.resize(after, (1080, 1920), interpolation=cv2.INTER_LINEAR)
        if name.startswith("gate-edges-"):
            width = int(name.rsplit("-", 1)[1])
            frame = cv2.resize(frame, (width, width * 16 // 9))
            after = frame.copy()
            # 本例验证坐标，不以降阈值掩盖重采样差异；模板取自实际归一化的合成帧。
            normalized = native_resize(frame, self.sdk)
            for key, crop in (
                ("scene", normalized[80:140, 40:130]),
                ("target", normalized[417:457, 334:414]),
            ):
                (bundle / f"image/{key}.png").write_bytes(
                    cv2.imencode(".png", crop)[1].tobytes()
                )
        for key, image in (("before", frame), ("after", after)):
            (root / f"{key}.png").write_bytes(cv2.imencode(".png", image)[1].tobytes())
        config = {
            "case": name,
            "bundle": str(bundle),
            "before": str(root / "before.png"),
            "after": str(root / "after.png"),
            "output": str(root / "run-data"),
            "device_id": "m2-offline-" + uuid.uuid4().hex,
            "large": name == "large-frame",
            "width": frame.shape[1],
            "files": [
                {"path": p.relative_to(bundle).as_posix(), "sha256": sha(p)}
                for p in sorted(bundle.rglob("*"))
                if p.is_file()
            ],
        }
        if name == "resource-isolation":
            alternate = root / "alternate-resource"
            shutil.copytree(bundle, alternate)
            nodes = pipeline()
            nodes["Normal"] = custom("TestFalse", following=["Terminal"])
            (alternate / "pipeline/cases.json").write_text(
                json.dumps(nodes), encoding="utf-8"
            )
            config["alternate"] = {
                "root": str(alternate),
                "files": [
                    {"path": p.relative_to(alternate).as_posix(), "sha256": sha(p)}
                    for p in sorted(alternate.rglob("*"))
                    if p.is_file()
                ],
            }
        case_file = root / "case.json"
        case_file.write_text(json.dumps(config, ensure_ascii=False), encoding="utf-8")
        result_file = root / "native-result.json"
        env = os.environ.copy()
        env["PATH"] = str(self.sdk / "bin") + os.pathsep + env.get("PATH", "")
        # 仅保护独立测试进程；触发超时即失败，不用强杀结果冒充生产取消通过。
        with (root / "stdout.log").open("wb") as out, (root / "stderr.log").open(
            "wb"
        ) as err:
            result = subprocess.run(
                [str(self.exe), str(case_file), str(result_file)],
                cwd=root,
                stdout=out,
                stderr=err,
                env=env,
                timeout=30,
            )
        self.assertEqual(
            result.returncode,
            0,
            (root / "stderr.log").read_text(encoding="utf-8", errors="replace")[-2000:],
        )
        data = json.loads(result_file.read_text(encoding="utf-8"))
        self.assertTrue(data["pass"])
        self.results[name] = data

    def test_cross_process_lease(self):
        root = self.folder / "cross-process-lease"
        root.mkdir()
        identity = "m2-offline-" + uuid.uuid4().hex
        env = os.environ.copy()
        env["PATH"] = str(self.sdk / "bin") + os.pathsep + env.get("PATH", "")
        args = [str(self.exe), "--lease-probe", identity, str(root)]
        with (root / "holder.log").open("wb") as log:
            holder = subprocess.Popen(
                [str(self.exe), "--lease-hold", identity, str(root)],
                env=env,
                stdout=log,
                stderr=log,
                cwd=root,
            )
            try:
                deadline = time.monotonic() + 5
                while not (root / "ready").exists():
                    self.assertIsNone(holder.poll(), "lease holder exited before ready")
                    self.assertLess(
                        time.monotonic(), deadline, "lease holder did not become ready"
                    )
                    time.sleep(0.01)
                busy = subprocess.run(
                    args, env=env, capture_output=True, text=True, timeout=5, cwd=root
                )
                self.assertEqual((busy.returncode, busy.stdout), (0, "busy"))
            finally:
                (root / "release").write_text("release", encoding="utf-8")
                holder.wait(timeout=15)
        self.assertEqual(holder.returncode, 0)
        free = subprocess.run(
            args, env=env, capture_output=True, text=True, timeout=5, cwd=root
        )
        self.assertEqual((free.returncode, free.stdout), (0, "acquired"))
        self.results["cross-process-lease"] = {
            "pass": True,
            "held": busy.stdout,
            "released": free.stdout,
        }


CASES = [
    "gate-edges-900",
    "gate-edges-1080",
    "gate-edges-720",
    "gate-edges-450",
    "gate-edges-360",
    "recover-throw",
    "recover-invalid",
    "recover-storage",
    "critical-full",
    "identity-action-revision",
    "identity-action-params",
    "identity-recovery-revision",
    "identity-recovery-params",
    "registry-sealed",
    "definition-copy",
    "normal",
    "large-frame",
    "duplicate-start",
    "wait-stop",
    "custom-stop",
    "nested-stop",
    "clone-stop",
    "callback-exception",
    "child-failure",
    "root-not-terminal",
    "wrong-root-node",
    "framework-failure",
    "builtin-unguarded",
    "postcondition-timeout",
    "permission-denied",
    "application-mismatch",
    "initialization-failure",
    "stop-timeout",
    "stop-during-connect",
    "held-touch",
    "held-key",
    "release-timeout",
    "session-time-limit",
    "recovery",
    "unresolved-recovery",
    "clone-isolation",
    "interrupt",
    "native-swipe",
    "real-device-rejected",
    "result-save-failure",
    "gate-normal",
    "gate-bare-all",
    "gate-late",
    "gate-generation",
    "gate-viewport",
    "gate-epoch",
    "gate-expired",
    "gate-permission",
    "gate-package",
    "gate-bounds",
    "gate-nohit",
    "gate-error",
    "gate-held-release",
    "store-events",
    "store-critical-overflow",
    "store-once",
    "store-interrupted",
    "native-scroll",
    "idempotency-conflict",
    "sequential-reset",
    "old-request-replay",
    "resource-isolation",
    "store-terminal-transaction",
    "session-restart-refused",
    "gate-reinitialize",
]
for case in CASES:

    def test(self, name=case):
        self.execute_case(name)

    setattr(RuntimeTests, "test_" + case.replace("-", "_"), test)


if __name__ == "__main__":
    unittest.main()
