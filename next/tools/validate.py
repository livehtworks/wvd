"""Bounded native product acceptance; never connects to a real device."""

import subprocess
import sys

from build import ROOT, cmake_path, run


def validate():
    cmake = cmake_path()
    targets = [
        "automationd", "test_native_flow", "test_native_devices", "capture_stall_helper",
        "test_native_capture", "test_native_author",
        "test_native_coordinator",
        "test_native_application", "test_native_recognition", "test_native_ocr",
    ]
    run("native-acceptance-build", [
        cmake, "--build", "--preset", "windows-release", "--target", *targets,
    ])
    native = ROOT / "build/native/Release"
    product = ROOT / "dist/wvd-next-native"
    if not (product / "pack/manifest.json").is_file():
        raise RuntimeError("原生候选缺少封存资源，请先运行 tools/build.py")
    cases = [
        ("native-flow", [native / "test_native_flow.exe"]),
        ("native-devices", [native / "test_native_devices.exe"]),
        ("native-capture", [native / "test_native_capture.exe",
                            native / "capture_stall_helper.exe"]),
        ("native-author", [native / "test_native_author.exe"]),
        ("native-coordinator", [native / "test_native_coordinator.exe"]),
        ("native-application", [native / "test_native_application.exe",
                                product / "pack", product / "data/quest.json"]),
        ("native-recognition", [native / "test_native_recognition.exe",
                                  product / "pack/image/Inn.png"]),
        ("native-ocr", [native / "test_native_ocr.exe",
                          product / "pack/model/ocr"]),
    ]
    for name, command in cases:
        run(name, [str(item) for item in command])
    print("Native offline product acceptance finished; real device and game NOT_RUN.")


if __name__ == "__main__":
    try:
        validate()
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
