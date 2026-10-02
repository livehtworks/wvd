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
OCR_MODELS = json.loads((ROOT / "resources/recognition/ocr-models.json").read_text(encoding="utf-8"))["models"]


def sync_authoring_resources():
    """同步两份作者JSON及语义配方实际引用的扩展素材，不扫描日志或用户mod。"""
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
    catalogue = json.loads((ROOT / "resources/authoring/semantic-assets.json").read_text(encoding="utf-8"))

    def image_references(value):
        if isinstance(value, dict):
            if value.get("mode") == "template" and isinstance(value.get("image"), str):
                yield value["image"]
            for child in value.values():
                yield from image_references(child)
        elif isinstance(value, list):
            for child in value:
                yield from image_references(child)

    for image in sorted(set(image_references(catalogue["resources"]))):
        name = Path(image + ".png")
        if name.is_absolute() or ".." in name.parts or "\\" in image or ":" in image:
            raise RuntimeError("作者素材路径非法: " + image)
        source = ROOT / "resources/images" / name
        if not source.is_file():
            continue  # 旧素材来自既有清单，不借此扫描/替换其权威来源。
        target = ROOT / "packs/wvd/image" / name
        if source.is_symlink() or target.is_symlink():
            raise RuntimeError("作者素材链接不安全: " + image)
        content = source.read_bytes()
        relative = "image/" + name.as_posix()
        digest = hashlib.sha256(content).hexdigest()
        if not target.is_file() or target.read_bytes() != content:
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(content)
            changed = True
        row = members.get(relative)
        if row is None:
            row = {"path": relative, "source": "next/resources/images/" + name.as_posix()}
            manifest["files"].append(row)
            members[relative] = row
        if row.get("sha256") != digest or row.get("bytes") != len(content):
            row["sha256"] = digest
            row["bytes"] = len(content)
            changed = True
    if changed:
        manifest["files"].sort(key=lambda row: row["path"])
        identity = {"files": manifest["files"], "aliases": manifest.get("aliases", {})}
        manifest["revision"] = hashlib.sha256(json.dumps(identity, sort_keys=True,
            ensure_ascii=False, separators=(",", ":")).encode("utf-8")).hexdigest()
        temporary = manifest_path.with_suffix(".json.tmp")
        temporary.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        os.replace(temporary, manifest_path)


def source_identity():
    repo = ROOT.parent
    head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=repo, text=True).strip()
    # 比较实际工作树与 HEAD：已暂存、未暂存和部分暂存的净源码变化都进入身份。
    # 禁用展示型外部差异/转换，不修改用户 Git 配置；构建并不读取暂存区内容。
    diff = subprocess.check_output(["git", "diff", "--binary", "--no-ext-diff", "--no-textconv",
        "HEAD", "--", "next", "docs"], cwd=repo)
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
    model_lock = ROOT / "resources/recognition/ocr-models.json"
    compiled_models = ROOT / "build/generated/ocr_models.sha256"
    if not compiled_models.is_file() or compiled_models.read_text(encoding="ascii").strip() != sha256(model_lock):
        raise RuntimeError("OCR模型锁与编译产物不一致，请重新配置构建")
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


BUILD_RECEIPT = ROOT / ".local/build-input.json"


def begin_build():
    """资源同步和 CMake 配置结束后冻结输入；不把打包时的 HEAD 当作构建来源。"""
    check_authoring_assets()
    receipt = {"schema": 1, "state": "BUILDING", "source": source_identity(),
               "started_at_utc": datetime.now(timezone.utc).isoformat()}
    BUILD_RECEIPT.parent.mkdir(parents=True, exist_ok=True)
    BUILD_RECEIPT.write_text(json.dumps(receipt, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return receipt


def build_artifacts():
    paths = [ROOT / "build/Release" / name for name in
             ("automationd.exe", "wvd-capture-host.exe", "opencv_world4120.dll", "onnxruntime.dll", "scrcpy-server-v3.3.4")]
    paths += sorted(p for p in (ROOT / "web/dist").rglob("*") if p.is_file())
    if not (ROOT / "web/dist/index.html").is_file():
        raise RuntimeError("BUILD_WEB_MISSING")
    return {p.relative_to(ROOT).as_posix(): sha256(p) for p in paths}


def finish_build():
    receipt = json.loads(BUILD_RECEIPT.read_text(encoding="utf-8"))
    if receipt.get("state") != "BUILDING" or receipt["source"] != source_identity():
        raise RuntimeError("BUILD_SOURCE_CHANGED")
    receipt.update(state="BUILT", artifacts=build_artifacts(), finished_at_utc=datetime.now(timezone.utc).isoformat())
    BUILD_RECEIPT.write_text(json.dumps(receipt, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return receipt


def verified_build():
    receipt = json.loads(BUILD_RECEIPT.read_text(encoding="utf-8"))
    if receipt.get("state") != "BUILT" or receipt.get("source") != source_identity():
        raise RuntimeError("BUILD_SOURCE_CHANGED_OR_NOT_COMPLETED")
    if receipt.get("artifacts") != build_artifacts():
        raise RuntimeError("BUILD_ARTIFACT_CHANGED")
    return receipt


def stage(target):
    sync_authoring_resources()
    check_authoring_assets()
    build = verified_build()
    binary = ROOT / "build/Release"
    native = ROOT / ".local/native-deps"
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
    version = subprocess.run([str(binary / "automationd.exe"), "--version"], check=True,
                             capture_output=True, timeout=10).stdout.decode("utf-8").strip()
    web = ROOT / "web/dist"
    pack = ROOT / "packs/wvd"
    if not (web / "index.html").is_file() or not (pack / "manifest.json").is_file():
        raise RuntimeError("前端或资源包尚未构建")
    shutil.copytree(web, target / "web")
    shutil.copytree(pack, target / "pack")
    manifest_path = target / "pack/manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    entries = {row["path"]: row for row in manifest["files"]}
    for spec in OCR_MODELS.values():
        for name, member in spec["files"].items():
            expected = member["sha256"]
            relative = spec["bundle_directory"] + "/" + name
            checked_copy(ROOT / member["source"], target / "pack" / relative, expected)
            if relative in entries and entries[relative]["sha256"] != expected:
                raise RuntimeError("OCR 成员与资源清单冲突: " + relative)
            if relative not in entries:
                manifest["files"].append({"path": relative, "sha256": expected,
                                          "source": member["source"]})
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
    checked_copy(ROOT / "tools/manage_service.ps1", target / "tools/manage_service.ps1")
    launcher = (
        "@echo off\r\nsetlocal\r\nchcp 65001 >nul\r\n"
        "set \"ROOT=%~dp0\"\r\n"
        "powershell -NoProfile -ExecutionPolicy Bypass -File \"%ROOT%tools\\manage_service.ps1\" "
        "-Action Start -CandidateRoot \"%ROOT%.\" -OpenBrowser\r\n"
        "if errorlevel 1 pause\r\n"
        "endlocal\r\n"
    )
    # 批处理命令均为ASCII，UTF-8 BOM会使cmd把首行@echo识别成未知命令。
    (target / "启动WVD原生版.bat").write_bytes(launcher.encode("utf-8"))
    (target / "退出WVD原生版.bat").write_bytes(launcher.replace(
        "-Action Start", "-Action Stop").replace(" -OpenBrowser", "").encode("utf-8"))
    (target / "DELIVERY_STATUS.json").write_text(json.dumps({
        "migration_baseline": "8f61540eafa61413852c2c3a85cb81c090e2161f",
        "memory_work_package_baseline": "661069f5688253270d1b940e084ce19ceca01373",
        **build["source"],
        "build_input": build,
        "built_at_utc": datetime.now(timezone.utc).isoformat(),
        "engine": "wvd_native", "status": "BUILT_NOT_GAME_ACCEPTED",
        "exe_sha256": sha256(target / "automationd.exe"),
        "version_output": version,
        "web_index_sha256": sha256(target / "web/index.html"),
        "pack_revision": manifest["revision"],
        "resource_catalog_sha256": sha256(target / "pack/parameters/semantic-assets.json"),
        "pack_manifest_sha256": sha256(target / "pack/manifest.json"),
        "files": [{"path": p.relative_to(target).as_posix(), "sha256": sha256(p)}
                  for p in sorted(target.rglob("*")) if p.is_file()],
        "game_scope": "NOT_RUN_THIS_BUILD",
    }, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    # 同一预检入口由部署管理器在任何旧服务退出之前调用。
    validation = subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
        str(target / "tools/manage_service.ps1"), "-Action", "Validate", "-CandidateRoot", str(target)],
        capture_output=True, timeout=60)
    if validation.returncode:
        detail = (validation.stdout + validation.stderr).decode("utf-8", errors="replace")
        raise RuntimeError(f"CANDIDATE_PREFLIGHT_FAILED: exit={validation.returncode}\n{detail}")
    verified_build()  # 复制窗口内的源码/EXE/前端变化同样拒绝，不能记录成完整候选。


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
