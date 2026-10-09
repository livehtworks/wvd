"""Bounded native product acceptance; never connects to a real device."""

import subprocess
import sys
import argparse
from pathlib import Path

from build import ROOT, cmake_path, run


def validate(selected):
    cmake = cmake_path()
    targets = [
        "automationd", "test_native_flow", "test_native_devices", "capture_stall_helper",
        "test_native_capture", "test_native_author",
        "test_native_coordinator",
        "test_native_application", "test_native_recognition", "test_native_ocr",
    ]
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
    cases = [(name, command) for name, command in cases if selected == "all" or name in selected]
    chosen = {Path(command[0]).stem for _, command in cases}
    if "test_native_capture" in chosen:
        chosen.add("capture_stall_helper")
    run("native-acceptance-build", [cmake, "--build", "--preset", "windows-release", "--target", *sorted(chosen)])
    for name, command in cases:
        run(name, [str(item) for item in command])
    print("Native offline product acceptance finished; real device and game NOT_RUN.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Explicitly scoped native offline validation; no game actions.")
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--all", action="store_true", help="Run the complete existing offline acceptance suite")
    group.add_argument("--case", action="append", choices=["native-" + name for name in
        ("flow", "devices", "capture", "author", "coordinator", "application", "recognition", "ocr")])
    options = parser.parse_args()
    if not options.all and not options.case:
        parser.print_help()
        sys.exit(0)
    try:
        validate("all" if options.all else options.case)
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
