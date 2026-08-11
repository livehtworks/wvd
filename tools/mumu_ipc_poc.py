import argparse
import ctypes
import json
import statistics
import subprocess
import time
from pathlib import Path

import cv2
import numpy as np


GAME_PACKAGE = "jp.co.drecom.wizardry.daphne"
DEFAULT_CONFIG_CANDIDATES = [
    Path("config.json"),
    Path("dist") / "wvd" / "config.json",
]


class MumuIpcPocError(RuntimeError):
    pass


def load_config(path=None):
    candidates = [Path(path)] if path else DEFAULT_CONFIG_CANDIDATES
    for candidate in candidates:
        if candidate.exists():
            with candidate.open("r", encoding="utf-8") as f:
                data = json.load(f)
            general = data.get("GENERAL", {})
            default = data.get("DEFAULT", {})
            merged = {}
            merged.update(general)
            merged.update(default)
            return candidate, merged
    raise MumuIpcPocError("未找到 config.json 或 dist/wvd/config.json")


def resolve_paths(config):
    emu_path = Path(config["EMU_PATH"].replace("/", "\\"))
    if not emu_path.exists():
        raise MumuIpcPocError(f"模拟器路径不存在: {emu_path}")

    if emu_path.name == "MuMuNxDevice.exe":
        mumu_root = emu_path.parents[3]
        dll_candidates = [
            emu_path.parent / "sdk" / "external_renderer_ipc.dll",
            *mumu_root.glob("nx_device/*/shell/sdk/external_renderer_ipc.dll"),
            mumu_root / "nx_main" / "sdk" / "external_renderer_ipc.dll",
        ]
        dll_path = next((path for path in dll_candidates if path.exists()), dll_candidates[0])
        adb_path = emu_path.with_name("adb.exe")
    else:
        raise MumuIpcPocError(f"当前 PoC 只处理 MuMuNxDevice.exe: {emu_path}")

    if not dll_path.exists():
        raise MumuIpcPocError(f"MuMu IPC DLL 不存在: {dll_path}")
    if not adb_path.exists():
        raise MumuIpcPocError(f"ADB 不存在: {adb_path}")

    return emu_path, mumu_root, dll_path, adb_path


def configure_dll(dll_path):
    dll = ctypes.WinDLL(str(dll_path))
    dll.nemu_connect.argtypes = [ctypes.c_wchar_p, ctypes.c_int]
    dll.nemu_connect.restype = ctypes.c_int
    dll.nemu_disconnect.argtypes = [ctypes.c_int]
    dll.nemu_disconnect.restype = None
    dll.nemu_get_display_id.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
    dll.nemu_get_display_id.restype = ctypes.c_int
    dll.nemu_capture_display.argtypes = [
        ctypes.c_int,
        ctypes.c_uint,
        ctypes.c_int,
        ctypes.POINTER(ctypes.c_int),
        ctypes.POINTER(ctypes.c_int),
        ctypes.c_void_p,
    ]
    dll.nemu_capture_display.restype = ctypes.c_int
    return dll


def adb_screenshot(adb_path, serial):
    result = subprocess.run(
        [str(adb_path), "-s", serial, "exec-out", "screencap"],
        capture_output=True,
        timeout=10,
    )
    if result.returncode != 0 or result.stderr:
        raise MumuIpcPocError(
            f"ADB screencap 失败: returncode={result.returncode}, stderr={result.stderr[:200]!r}"
        )

    raw = result.stdout
    if len(raw) < 12:
        raise MumuIpcPocError("ADB screencap 数据不足 12 字节")

    width, height, fmt = np.frombuffer(raw[:12], dtype="<u4", count=3)
    expected = int(width) * int(height) * 4
    pixels = raw[12 : 12 + expected]
    if len(pixels) != expected:
        raise MumuIpcPocError(f"ADB screencap 数据长度异常: expected={expected}, actual={len(pixels)}")

    image = np.frombuffer(pixels, dtype=np.uint8).reshape((int(height), int(width), 4))
    image = cv2.cvtColor(image, cv2.COLOR_RGBA2BGR)
    if image.shape[:2] == (900, 1600):
        image = image.transpose(1, 0, 2)
    return image


class MumuIpcClient:
    def __init__(self, dll, mumu_root, instance_index, package):
        self.dll = dll
        self.mumu_root = mumu_root
        self.instance_index = instance_index
        self.package = package
        self.handle = 0
        self.display_id = None
        self.width = 0
        self.height = 0

    def connect(self):
        self.handle = self.dll.nemu_connect(str(self.mumu_root), int(self.instance_index))
        if self.handle <= 0:
            raise MumuIpcPocError(
                f"nemu_connect 失败: mumu_root={self.mumu_root}, instance_index={self.instance_index}, handle={self.handle}"
            )
        return self.handle

    def disconnect(self):
        if self.handle > 0:
            self.dll.nemu_disconnect(self.handle)
            self.handle = 0

    def get_display_id(self):
        display_id = self.dll.nemu_get_display_id(self.handle, self.package.encode("utf-8"), 0)
        if display_id < 0:
            raise MumuIpcPocError(f"nemu_get_display_id 失败: package={self.package}, display_id={display_id}")
        self.display_id = display_id
        return display_id

    def get_resolution(self):
        width = ctypes.c_int(0)
        height = ctypes.c_int(0)
        ret = self.dll.nemu_capture_display(
            self.handle,
            ctypes.c_uint(self.display_id),
            0,
            ctypes.byref(width),
            ctypes.byref(height),
            None,
        )
        if ret:
            raise MumuIpcPocError(f"nemu_capture_display 获取分辨率失败: ret={ret}")
        self.width = width.value
        self.height = height.value
        return self.width, self.height

    def capture_raw(self):
        if not self.width or not self.height:
            self.get_resolution()
        width = ctypes.c_int(self.width)
        height = ctypes.c_int(self.height)
        length = self.width * self.height * 4
        buffer = (ctypes.c_ubyte * length)()
        ret = self.dll.nemu_capture_display(
            self.handle,
            ctypes.c_uint(self.display_id),
            length,
            ctypes.byref(width),
            ctypes.byref(height),
            ctypes.cast(buffer, ctypes.c_void_p),
        )
        if ret:
            raise MumuIpcPocError(f"nemu_capture_display 截图失败: ret={ret}")
        if width.value != self.width or height.value != self.height:
            raise MumuIpcPocError(
                f"截图分辨率变化: expected={self.width}x{self.height}, actual={width.value}x{height.value}"
            )
        return np.frombuffer(buffer, dtype=np.uint8).reshape((self.height, self.width, 4)).copy()


def make_candidates(raw_rgba):
    base = {
        "rgba_to_bgr": cv2.cvtColor(raw_rgba, cv2.COLOR_RGBA2BGR),
        "bgra_to_bgr": cv2.cvtColor(raw_rgba, cv2.COLOR_BGRA2BGR),
    }
    candidates = {}
    for color_name, image in base.items():
        transforms = {
            "identity": image,
            "flip_vertical": cv2.flip(image, 0),
            "flip_horizontal": cv2.flip(image, 1),
            "rotate_180": cv2.rotate(image, cv2.ROTATE_180),
        }
        for transform_name, transformed in transforms.items():
            candidates[f"{color_name}/{transform_name}"] = transformed
    return candidates


def compare_to_adb(raw_rgba, adb_image):
    candidates = make_candidates(raw_rgba)
    scores = []
    for name, image in candidates.items():
        if image.shape != adb_image.shape:
            continue
        diff = cv2.absdiff(image, adb_image)
        scores.append((float(diff.mean()), name, image))
    if not scores:
        raise MumuIpcPocError("没有任何 IPC 图像候选与 ADB 图像尺寸一致")
    scores.sort(key=lambda item: item[0])
    return scores[0], scores[:5]


def benchmark(client, count):
    durations = []
    failures = 0
    for index in range(count):
        start = time.perf_counter()
        try:
            raw = client.capture_raw()
            if raw.shape != (client.height, client.width, 4):
                raise MumuIpcPocError(f"第 {index + 1} 次截图 shape 异常: {raw.shape}")
        except Exception as exc:
            failures += 1
            print(f"[WARN] 第 {index + 1} 次截图失败: {exc}")
            continue
        durations.append((time.perf_counter() - start) * 1000)

    if not durations:
        raise MumuIpcPocError("benchmark 没有成功截图")

    return {
        "count": count,
        "success": len(durations),
        "failures": failures,
        "min_ms": min(durations),
        "max_ms": max(durations),
        "mean_ms": statistics.mean(durations),
        "median_ms": statistics.median(durations),
        "p95_ms": sorted(durations)[int(len(durations) * 0.95) - 1],
    }


def write_json(path, data):
    with path.open("w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=2)


def main():
    parser = argparse.ArgumentParser(description="MuMu IPC 截图 PoC，不接入主截图链。")
    parser.add_argument("--config", help="config.json 路径，默认优先 config.json，其次 dist/wvd/config.json")
    parser.add_argument("--instance", type=int, help="MuMu 实例号，默认读取 EMU_INDEX")
    parser.add_argument("--package", default=GAME_PACKAGE, help="用于 nemu_get_display_id 的包名")
    parser.add_argument("--count", type=int, default=100, help="benchmark 截图次数")
    parser.add_argument("--out", default="logs/mumu_ipc_poc", help="输出目录")
    args = parser.parse_args()

    config_path, config = load_config(args.config)
    emu_path, mumu_root, dll_path, adb_path = resolve_paths(config)
    instance_index = args.instance if args.instance is not None else int(config.get("EMU_INDEX", 0))
    serial = config.get("ADB_ADRESS", "")
    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    print(f"[INFO] config={config_path}")
    print(f"[INFO] emu_path={emu_path}")
    print(f"[INFO] mumu_root={mumu_root}")
    print(f"[INFO] dll={dll_path}")
    print(f"[INFO] instance={instance_index}")
    print(f"[INFO] adb={adb_path}")
    print(f"[INFO] serial={serial}")

    dll = configure_dll(dll_path)
    client = MumuIpcClient(dll, mumu_root, instance_index, args.package)

    result = {
        "config": str(config_path),
        "emu_path": str(emu_path),
        "mumu_root": str(mumu_root),
        "dll_path": str(dll_path),
        "instance_index": instance_index,
        "serial": serial,
        "package": args.package,
    }

    try:
        handle = client.connect()
        result["handle"] = handle
        print(f"[PASS] nemu_connect handle={handle}")

        display_id = client.get_display_id()
        result["display_id"] = display_id
        print(f"[PASS] display_id={display_id}")

        width, height = client.get_resolution()
        result["resolution"] = {"width": width, "height": height}
        print(f"[PASS] resolution={width}x{height}")

        raw = client.capture_raw()
        adb_image = adb_screenshot(adb_path, serial)
        (best_diff, best_name, best_image), top_scores = compare_to_adb(raw, adb_image)
        result["adb_shape"] = list(adb_image.shape)
        result["ipc_raw_shape"] = list(raw.shape)
        result["best_transform"] = best_name
        result["best_mean_abs_diff"] = best_diff
        result["top_scores"] = [{"name": name, "mean_abs_diff": diff} for diff, name, image in top_scores]

        cv2.imwrite(str(out_dir / "ipc_best.png"), best_image)
        cv2.imwrite(str(out_dir / "adb_reference.png"), adb_image)
        print(f"[PASS] best_transform={best_name}, mean_abs_diff={best_diff:.2f}")
        print(f"[INFO] saved={out_dir / 'ipc_best.png'}")
        print(f"[INFO] saved={out_dir / 'adb_reference.png'}")

        bench = benchmark(client, args.count)
        result["benchmark"] = bench
        print(
            "[PASS] benchmark "
            f"success={bench['success']}/{bench['count']} "
            f"mean={bench['mean_ms']:.2f}ms median={bench['median_ms']:.2f}ms "
            f"p95={bench['p95_ms']:.2f}ms max={bench['max_ms']:.2f}ms"
        )

    finally:
        client.disconnect()
        result["disconnected"] = client.handle == 0
        print(f"[PASS] disconnected={result['disconnected']}")
        write_json(out_dir / "result.json", result)
        print(f"[INFO] saved={out_dir / 'result.json'}")


if __name__ == "__main__":
    main()
