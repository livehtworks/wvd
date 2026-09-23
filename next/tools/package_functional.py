"""整理可直接双击启动的 WVD Next 本地目录，不触碰旧 dist/config/mod。"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import shutil
import uuid


ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parent
OUTPUT = ROOT / "dist" / "wvd-next"


def copy_tree(source: Path, destination: Path) -> None:
    if destination.exists():
        shutil.rmtree(destination)
    shutil.copytree(source, destination)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _package_into(OUTPUT: Path) -> Path:
    executable = ROOT / "build" / "Release" / "automationd.exe"
    web = ROOT / "web" / "dist"
    pack_source = ROOT / "packs" / "wvd"
    quests = REPO / "resources" / "quest" / "quest.json"
    dependency = json.loads((ROOT / ".local" / "maafw.json").read_text(encoding="utf-8"))
    sdk = Path(dependency["sdk"])
    model = Path(dependency["ocr"])
    for required in (executable, web / "index.html", pack_source / "manifest.json", quests,
                     sdk / "bin" / "MaaFramework.dll", model / "det.onnx", model / "rec.onnx", model / "keys.txt"):
        if not required.exists():
            raise RuntimeError(f"缺少运行产物: {required}")

    # 交付目录只含可重建产物；用户配置和流程在 LocalAppData，重打包不会触碰它们。
    if OUTPUT.exists():
        raise RuntimeError("新候选 staging 已存在，拒绝覆盖")
    OUTPUT.mkdir(parents=True, exist_ok=False)
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
        entry = next((row for row in manifest["files"] if row["path"] == relative), None)
        if entry is None:
            manifest["files"].append({"path": relative, "sha256": sha256(target),
                                      "source": "locked-local-ocr/" + name})
        elif entry["sha256"] != sha256(target):
            raise RuntimeError("OCR 同名清单内容冲突: " + relative)
    manifest["files"].sort(key=lambda item: item["path"])
    # 模型是运行资源，修改成员就必须产生新的 revision；禁止同版本不同内容。
    manifest["source_revision"] = manifest["revision"]
    identity = {"files": manifest["files"], "aliases": manifest.get("aliases", {})}
    manifest["revision"] = hashlib.sha256(json.dumps(identity, sort_keys=True,
        ensure_ascii=False, separators=(",", ":")).encode("utf-8")).hexdigest()
    for entry in manifest["files"]:
        relative = Path(entry["path"])
        if relative.is_absolute() or ".." in relative.parts or "\\" in entry["path"] or ":" in entry["path"]:
            raise RuntimeError("制品成员路径非法")
        if sha256(OUTPUT / "pack" / relative) != entry["sha256"]:
            raise RuntimeError("制品成员哈希不匹配: " + entry["path"])
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


def package() -> Path:
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    staging = OUTPUT.with_name(OUTPUT.name + ".staging-" + uuid.uuid4().hex)
    backup = OUTPUT.with_name(OUTPUT.name + ".previous-" + uuid.uuid4().hex)
    try:
        _package_into(staging)
        # 这是待实机验收的候选，不把打包成功写成真实能力已验证。
        (staging / "DELIVERY_STATUS.json").write_text(json.dumps({
            "state": "LOCAL_ACCEPTANCE_REQUIRED",
            "real_startup_verified": False,
            "source_baseline": "dcc11c18aff724ad9557bbbab13b064ad5734c24",
            "work_package": "WVD_Composable_Workflows_dcc11c18_20260923",
        }, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        if OUTPUT.exists():
            if OUTPUT.is_symlink() or (hasattr(OUTPUT, "is_junction") and OUTPUT.is_junction()):
                raise RuntimeError("拒绝替换链接形式的输出目录")
            OUTPUT.rename(backup)
        try:
            staging.rename(OUTPUT)
        except BaseException:
            if backup.exists() and not OUTPUT.exists(): backup.rename(OUTPUT)
            raise
        # 上一候选保留在 next/dist 的明确 previous 目录；不清理用户数据。
        return OUTPUT
    except BaseException:
        if staging.exists() and not staging.is_symlink(): shutil.rmtree(staging)
        raise


if __name__ == "__main__":
    print(package())
