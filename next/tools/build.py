"""独立 M1 构建入口，只写 next 构建产物与专用依赖缓存。"""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def cmake_path():
    if tool := shutil.which("cmake"):
        return tool
    installer = (
        Path(os.environ.get("ProgramFiles(x86)", ""))
        / "Microsoft Visual Studio/Installer/vswhere.exe"
    )
    if not installer.is_file():
        raise RuntimeError("找不到 CMake；请安装 VS 2022 C++ 与 CMake 组件或加入 PATH")
    installs = json.loads(
        subprocess.check_output(
            [
                str(installer),
                "-products",
                "*",
                "-version",
                "[17.0,18.0)",
                "-format",
                "json",
                "-utf8",
            ]
        ).decode("utf-8-sig")
    )
    for install in installs:
        path = (
            Path(install["installationPath"])
            / "Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
        )
        if path.is_file():
            return str(path)
    raise RuntimeError("已安装的 VS 2022 中没有 CMake 组件")


def run(name, command, cwd=ROOT):
    folder = ROOT / ".local/logs"
    folder.mkdir(parents=True, exist_ok=True)
    log = folder / (name + ".log")
    print(name, flush=True)
    with log.open("wb") as output:
        result = subprocess.run(
            command, cwd=cwd, stdout=output, stderr=subprocess.STDOUT
        )
    if result.returncode:
        print(log.read_text(encoding="utf-8", errors="replace")[-4000:])
        raise RuntimeError(f"{name} 失败 (exit={result.returncode})，完整日志: {log}")
    print("  OK", flush=True)


def build(m2_offline=False, m3=False, m4=True):
    # 常驻应用已经进入可操作阶段，正式本地构建始终链接WVD核心、视觉和设备层。
    m4 = True
    m3 = True
    npm = shutil.which("npm.cmd")
    if npm is None:
        raise RuntimeError("需要固定 Node/npm 工具链，见 dependencies.lock.json")
    lock = json.loads((ROOT / "dependencies.lock.json").read_text(encoding="utf-8"))
    for name, executable in (("node", shutil.which("node")), ("npm", npm)):
        if not executable:
            raise RuntimeError("找不到 " + name)
        version = (
            subprocess.check_output([executable, "--version"], text=True)
            .strip()
            .removeprefix("v")
        )
        if version != lock["toolchain"][name]:
            raise RuntimeError(f"{name} 需要 {lock['toolchain'][name]}，实际 {version}")
    cmake = cmake_path()
    run("dependencies", [sys.executable, str(ROOT / "tools/dependencies.py")])
    if m3:
        run("m3-prepare", [sys.executable, str(ROOT / "tools/prepare_m3.py")])
    if m4:
        run("m4-prepare", [sys.executable, str(ROOT / "tools/prepare_m4.py")])
    run("inventory", [sys.executable, str(ROOT / "tools/inventory/generate.py")])
    run(
        "npm-ci",
        [npm, "ci", "--ignore-scripts", "--no-fund", "--no-audit"],
        ROOT / "web",
    )
    run("web-build", [npm, "run", "build"], ROOT / "web")
    run(
        "native-configure",
        [
            cmake,
            "--preset",
            "windows-x64",
            "-DWVD_BUILD_M2_OFFLINE=" + ("ON" if m2_offline or m3 else "OFF"),
            "-DWVD_BUILD_M3=" + ("ON" if m3 else "OFF"),
            "-DWVD_BUILD_M4=" + ("ON" if m4 else "OFF"),
        ],
    )
    run("native-build", [cmake, "--build", "--preset", "windows-release"])
    run("functional-package", [sys.executable, str(ROOT / "tools/package_functional.py")])
    print("Windows functional build complete; production wvd.exe/config/mod untouched.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--m2-offline",
        action="store_true",
        help="额外构建已准备固定 SDK 的离线识别目标",
    )
    parser.add_argument(
        "--m3", action="store_true", help="构建 M2/M3 离线目标，不连接设备"
    )
    parser.add_argument("--m4", action="store_true", help="构建 M4 离线迁移目标；不代表全业务迁移完成")
    args = parser.parse_args()
    try:
        build(args.m2_offline, args.m3, args.m4)
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
