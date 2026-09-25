"""Stage an independent Maa-free Windows candidate without touching existing installs."""

import hashlib
import json
from datetime import datetime, timezone
import os
from pathlib import Path
import shutil
import subprocess
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "dist/wvd-next-native"
MODEL_HASHES = {
    "det.onnx": "8fe4bf6abfb20402357827f2efc964c8b28cf980e29fe09a99b742ae29725fa9",
    "rec.onnx": "da12c6e863761d774b07d3bd40fbaaa55516f90570e0b1dd9dc112e457301cc9",
    "keys.txt": "5662df9d2d03f0e8ca0d3b0649d6acbab904b6a14b3d3521463c71c37c668ce3",
}


def sync_authoring_resources():
    """Only these two authored JSON files are copied into the generated pack."""
    manifest_path = ROOT / "packs/wvd/manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    members = {row["path"]: row for row in manifest["files"]}
    changed = False
    for name in ("semantic-assets.json", "public-flows.json"):
        source = ROOT / "resources/authoring" / name
        target = ROOT / "packs/wvd/parameters" / name
        relative = "parameters/" + name
        if relative not in members or target.is_symlink() or not target.is_file():
            raise RuntimeError("作者资源包成员不安全或缺失: " + relative)
        content = source.read_bytes()
        json.loads(content.decode("utf-8"))
        digest = hashlib.sha256(content).hexdigest()
        if target.read_bytes() != content:
            target.write_bytes(content)
            changed = True
        row = members[relative]
        if row["sha256"] != digest or row.get("bytes") != len(content):
            row["sha256"] = digest
            row["bytes"] = len(content)
            changed = True
    if changed:
        identity = {"files": manifest["files"], "aliases": manifest.get("aliases", {})}
        manifest["revision"] = hashlib.sha256(json.dumps(identity, sort_keys=True,
            ensure_ascii=False, separators=(",", ":")).encode("utf-8")).hexdigest()
        temporary = manifest_path.with_suffix(".json.tmp")
        temporary.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        os.replace(temporary, manifest_path)


def source_identity():
    repo = ROOT.parent
    head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=repo, text=True).strip()
    diff = subprocess.check_output(["git", "diff", "--binary", "--", "next", "docs"], cwd=repo)
    untracked = subprocess.check_output(["git", "ls-files", "--others", "--exclude-standard", "-z",
        "--", "next", "docs"], cwd=repo).split(b"\0")
    digest = hashlib.sha256(diff)
    names = []
    for raw in sorted(item for item in untracked if item):
        name = raw.decode("utf-8", errors="surrogateescape")
        file = repo / name
        if file.is_file():
            digest.update(raw + b"\0")
            digest.update(file.read_bytes())
            names.append(name)
    return {"source_commit": head, "worktree_dirty": bool(diff or names),
            "worktree_diff_sha256": digest.hexdigest(), "untracked_source_count": len(names)}


def check_authoring_assets():
    source = ROOT / "resources/authoring/semantic-assets.json"
    packed = ROOT / "packs/wvd/parameters/semantic-assets.json"
    compiled_hash = ROOT / "build/generated/semantic_catalogue.sha256"
    if not compiled_hash.is_file() or compiled_hash.read_text(encoding="ascii").strip() != sha256(source):
        raise RuntimeError("语义源已变化但原生探针未重新配置构建")
    if source.read_bytes() != packed.read_bytes():
        raise RuntimeError("繁中语义目录与运行资源包不一致，请先同步 semantic-assets.json")
    if (ROOT / "resources/authoring/public-flows.json").read_bytes() != \
            (ROOT / "packs/wvd/parameters/public-flows.json").read_bytes():
        raise RuntimeError("作者流程与运行资源包不一致，请先同步 public-flows.json")
    catalogue = json.loads(source.read_text(encoding="utf-8"))
    referenced = set()

    def collect(value):
        if isinstance(value, dict):
            if value.get("mode") == "template" and isinstance(value.get("image"), str):
                referenced.add(value["image"])
            for child in value.values():
                collect(child)
        elif isinstance(value, list):
            for child in value:
                collect(child)

    collect(catalogue["resources"])
    missing = {path.stem for path in (ROOT / "resources/images").glob("*_zh_hant.png")} - referenced
    if missing:
        raise RuntimeError("繁中素材尚未接入语义目录: " + ", ".join(sorted(missing)))


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
    sync_authoring_resources()
    check_authoring_assets()
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
    # Windows can retain an image handle briefly after exit; never execute inside
    # the staging directory that must be renamed immediately afterward.
    subprocess.run([str(binary / "automationd.exe"), "--version"], check=True,
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
        "migration_baseline": "8f61540eafa61413852c2c3a85cb81c090e2161f",
        "memory_work_package_baseline": "661069f5688253270d1b940e084ce19ceca01373",
        **source_identity(),
        "built_at_utc": datetime.now(timezone.utc).isoformat(),
        "engine": "wvd_native", "status": "BUILT_NOT_GAME_ACCEPTED",
        "exe_sha256": sha256(target / "automationd.exe"),
        "web_index_sha256": sha256(target / "web/index.html"),
        "pack_revision": manifest["revision"],
        "resource_catalog_sha256": sha256(target / "pack/parameters/semantic-assets.json"),
        "game_scope": "NOT_RUN_THIS_BUILD",
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
            for attempt in range(5):
                try:
                    staging.rename(OUTPUT)
                    break
                except PermissionError:
                    if attempt == 4:
                        raise
                    time.sleep(0.5)
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
