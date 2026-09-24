"""Stage an independent Maa-free Windows candidate without touching existing installs."""

import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import uuid

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "dist/wvd-next-native"
MODEL_HASHES = {
    "det.onnx": "8fe4bf6abfb20402357827f2efc964c8b28cf980e29fe09a99b742ae29725fa9",
    "rec.onnx": "da12c6e863761d774b07d3bd40fbaaa55516f90570e0b1dd9dc112e457301cc9",
    "keys.txt": "5662df9d2d03f0e8ca0d3b0649d6acbab904b6a14b3d3521463c71c37c668ce3",
}


def sha256(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def checked_copy(source, target, expected=None):
    if not source.is_file():
        raise RuntimeError("缺少打包输入: " + str(source))
    if expected and sha256(source) != expected:
        raise RuntimeError("打包输入哈希不符: " + str(source))
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)
    if sha256(target) != sha256(source):
        raise RuntimeError("打包复制校验失败: " + str(target))


def stage(target):
    binary = ROOT / "build/Release"
    native = ROOT / ".local/native-deps"
    model = ROOT / "resources/ocr/en_us"
    lock = json.loads((ROOT / "native-dependencies.lock.json").read_text(encoding="utf-8"))
    for item in lock["binaries"]:
        archive = native / "archives" / item["filename"]
        if not archive.is_file() or archive.stat().st_size != item["size"] or sha256(archive) != item["sha256"]:
            raise RuntimeError("原生依赖未完成固定哈希校验: " + item["id"])
    target.mkdir(parents=True, exist_ok=False)
    for name in ("automationd.exe", "wvd-capture-host.exe", "opencv_world4120.dll",
                 "onnxruntime.dll", "scrcpy-server-v3.3.4"):
        checked_copy(binary / name, target / name)
    checked_copy(ROOT / "third_party/rapidocr_core/LICENSE",
                 target / "licenses/Apache-2.0.txt")
    checked_copy(native / "ort-unpacked/onnxruntime-win-x64-1.22.1/LICENSE",
                 target / "licenses/ONNX-Runtime-LICENSE.txt")
    checked_copy(ROOT / "resources/ocr/LICENSE-MaaCommonAssets",
                 target / "licenses/MaaCommonAssets-MIT.txt")
    checked_copy(ROOT / "docs/third-party-notices.md",
                 target / "licenses/THIRD_PARTY.md")
    subprocess.run([str(target / "automationd.exe"), "--version"], check=True,
                   capture_output=True, timeout=10)
    web = ROOT / "web/dist"
    pack = ROOT / "packs/wvd"
    if not (web / "index.html").is_file() or not (pack / "manifest.json").is_file():
        raise RuntimeError("前端或资源包尚未构建")
    shutil.copytree(web, target / "web")
    shutil.copytree(pack, target / "pack")
    manifest_path = target / "pack/manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    entries = {row["path"]: row for row in manifest["files"]}
    for name, expected in MODEL_HASHES.items():
        relative = "model/ocr/" + name
        checked_copy(model / name, target / "pack" / relative, expected)
        if relative in entries and entries[relative]["sha256"] != expected:
            raise RuntimeError("OCR 成员与资源清单冲突: " + relative)
        if relative not in entries:
            manifest["files"].append({"path": relative, "sha256": expected,
                                      "source": "resources/ocr/en_us/" + name})
    manifest["files"].sort(key=lambda row: row["path"])
    manifest["source_revision"] = manifest["revision"]
    identity = {"files": manifest["files"], "aliases": manifest.get("aliases", {})}
    manifest["revision"] = hashlib.sha256(json.dumps(identity, sort_keys=True,
        ensure_ascii=False, separators=(",", ":")).encode("utf-8")).hexdigest()
    for row in manifest["files"]:
        relative = Path(row["path"])
        if relative.is_absolute() or ".." in relative.parts or "\\" in row["path"] or ":" in row["path"]:
            raise RuntimeError("资源成员路径非法")
        if sha256(target / "pack" / relative) != row["sha256"]:
            raise RuntimeError("资源成员哈希不符: " + row["path"])
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    checked_copy(target / "pack/parameters/legacy-quests.json", target / "data/quest.json")
    launcher = (
        "@echo off\r\nsetlocal\r\nchcp 65001 >nul\r\n"
        "set \"ROOT=%~dp0\"\r\nset \"DATA=%LOCALAPPDATA%\\WvdNext\"\r\n"
        "if not exist \"%DATA%\" mkdir \"%DATA%\"\r\n"
        "\"%ROOT%automationd.exe\" --web-root \"%ROOT%web\" --data-root \"%DATA%\" "
        "--pack-root \"%ROOT%pack\" --quests \"%ROOT%data\\quest.json\"\r\n"
        "endlocal\r\n"
    )
    (target / "启动WVD原生版.bat").write_bytes(launcher.encode("utf-8-sig"))
    (target / "DELIVERY_STATUS.json").write_text(json.dumps({
        "source_baseline": "376ca780fa7432afb48031b048d340531ae8d5c7",
        "engine": "wvd_native", "status": "OFFLINE_ACCEPTANCE_REQUIRED",
        "device_readonly": "NOT_RUN", "game_scope": "NOT_RUN",
    }, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def package():
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    staging = OUTPUT.with_name(OUTPUT.name + ".staging-" + uuid.uuid4().hex)
    previous = OUTPUT.with_name(OUTPUT.name + ".previous-" + uuid.uuid4().hex)
    try:
        stage(staging)
        if OUTPUT.exists():
            if OUTPUT.is_symlink() or OUTPUT.is_junction() or not OUTPUT.is_dir():
                raise RuntimeError("拒绝替换链接或非目录形式的候选")
            marker = OUTPUT / "DELIVERY_STATUS.json"
            if not marker.is_file() or json.loads(marker.read_text(encoding="utf-8")).get("engine") != "wvd_native":
                raise RuntimeError("现有目录不是本脚本生成的原生候选，拒绝替换")
            OUTPUT.rename(previous)
        try:
            staging.rename(OUTPUT)
        except BaseException:
            if previous.exists() and not OUTPUT.exists():
                previous.rename(OUTPUT)
            raise
    except BaseException:
        if staging.is_dir() and not staging.is_symlink() and staging.parent.resolve() == OUTPUT.parent.resolve():
            shutil.rmtree(staging)
        raise
    return OUTPUT


if __name__ == "__main__":
    print(package())
