"""整理可直接双击启动的 WVD Next 本地目录，不触碰旧 dist/config/mod。"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import shutil


ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parent
OUTPUT = ROOT / "dist" / "wvd-next"


def copy_tree(source: Path, destination: Path) -> None:
    if destination.exists():
        shutil.rmtree(destination)
    shutil.copytree(source, destination)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def package() -> Path:
    executable = ROOT / "build" / "Release" / "automationd.exe"
    web = ROOT / "web" / "dist"
    pack_source = ROOT / "packs" / "wvd"
    quests = REPO / "resources" / "quest" / "quest.json"
    dependency = json.loads((ROOT / ".local" / "maafw.json").read_text(encoding="utf-8"))
    sdk = Path(dependency["sdk"])
    model = Path(dependency["ocr"])
    for required in (executable, web / "index.html", pack_source / "manifest.json", quests,
                     sdk / "bin" / "MaaFramework.dll", model / "det.onnx"):
        if not required.exists():
            raise RuntimeError(f"缺少运行产物: {required}")

    # 交付目录只含可重建产物；用户配置和流程在 LocalAppData，重打包不会触碰它们。
    if OUTPUT.exists():
        shutil.rmtree(OUTPUT)
    OUTPUT.mkdir(parents=True, exist_ok=True)
    # 固定 SDK 会在进程加载时检查相邻 plugins 目录；即使本项目没有插件也要保留空目录，
    # 否则每次正常启动都会先打印误导性的 DLL 加载错误。
    (OUTPUT / "plugins").mkdir(exist_ok=True)
    shutil.copy2(executable, OUTPUT / "automationd.exe")
    copy_tree(web, OUTPUT / "web")
    copy_tree(pack_source, OUTPUT / "pack")
    (OUTPUT / "data").mkdir(exist_ok=True)
    shutil.copy2(quests, OUTPUT / "data" / "quest.json")

    # MaaFramework 的运行依赖保持同一固定 SDK 身份；不复制工具、样例或调试符号。
    for dll in (sdk / "bin").glob("*.dll"):
        shutil.copy2(dll, OUTPUT / dll.name)

    model_target = OUTPUT / "pack" / "model" / "ocr"
    model_target.mkdir(parents=True, exist_ok=True)
    manifest_path = OUTPUT / "pack" / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    indexed = {item["path"] for item in manifest["files"]}
    for name in ("det.onnx", "rec.onnx", "keys.txt"):
        target = model_target / name
        shutil.copy2(model / name, target)
        relative = f"model/ocr/{name}"
        if relative not in indexed:
            manifest["files"].append({"path": relative, "sha256": sha256(target)})
    manifest["files"].sort(key=lambda item: item["path"])
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
                             encoding="utf-8")

    launcher = """@echo off\r
setlocal\r
chcp 65001 >nul\r
set \"ROOT=%~dp0\"\r
set \"DATA=%LOCALAPPDATA%\\WvdNext\"\r
if not exist \"%DATA%\" mkdir \"%DATA%\"\r
if exist \"%ROOT%legacy-config.json\" goto local_config\r
if exist \"%ROOT%..\\..\\..\\dist\\wvd\\config.json\" goto installed_config\r
if exist \"%ROOT%..\\..\\..\\config.json\" goto repo_config\r
\"%ROOT%automationd.exe\" --web-root \"%ROOT%web\" --data-root \"%DATA%\" --pack-root \"%ROOT%pack\" --quests \"%ROOT%data\\quest.json\"\r
goto end\r
:local_config\r
\"%ROOT%automationd.exe\" --web-root \"%ROOT%web\" --data-root \"%DATA%\" --pack-root \"%ROOT%pack\" --quests \"%ROOT%data\\quest.json\" --legacy-config \"%ROOT%legacy-config.json\"\r
goto end\r
:installed_config\r
\"%ROOT%automationd.exe\" --web-root \"%ROOT%web\" --data-root \"%DATA%\" --pack-root \"%ROOT%pack\" --quests \"%ROOT%data\\quest.json\" --legacy-config \"%ROOT%..\\..\\..\\dist\\wvd\\config.json\"\r
goto end\r
:repo_config\r
\"%ROOT%automationd.exe\" --web-root \"%ROOT%web\" --data-root \"%DATA%\" --pack-root \"%ROOT%pack\" --quests \"%ROOT%data\\quest.json\" --legacy-config \"%ROOT%..\\..\\..\\config.json\"\r
:end\r
endlocal\r
"""
    (OUTPUT / "启动WVD新版.bat").write_bytes(launcher.encode("utf-8-sig"))
    (OUTPUT / "使用说明.txt").write_text(
        "双击“启动WVD新版.bat”打开本地工作台。\n"
        "新版配置和流程保存在 %LOCALAPPDATA%\\WvdNext，不会覆盖旧 config.json、mod 或日志。\n"
        "先在模拟器面板确认路径、ADB 地址和实例编号，再连接、截图并由你主动开始任务。\n",
        encoding="utf-8-sig",
    )
    return OUTPUT


if __name__ == "__main__":
    print(package())
