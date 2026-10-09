"""Fetch and verify only the pinned standalone native runtime dependencies."""

import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import urllib.request
import zipfile
from dependency_tree import verify_consumed_tree

ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / ".local/native-deps"


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def verify(path, item):
    if path.stat().st_size != item["size"] or digest(path) != item["sha256"]:
        raise RuntimeError(f"固定依赖校验失败: {item['id']}")


def prepare_ocr_models(readonly=False):
    """模型只在依赖准备阶段下载；发布和运行阶段只校验，不联网补洞。"""
    lock = json.loads((ROOT / "resources/recognition/ocr-models.json").read_text(encoding="utf-8"))
    for model in lock["models"].values():
        for item in model["files"].values():
            target = (ROOT / item["source"]).resolve()
            if not target.is_relative_to(ROOT.resolve()) or target.is_symlink():
                raise RuntimeError("OCR依赖路径非法")
            if not target.is_file():
                if readonly:
                    raise RuntimeError("OCR固定模型缺失: " + item["source"])
                if "url" not in item or not target.is_relative_to(CACHE.resolve()):
                    raise RuntimeError("OCR固定模型缺失: " + item["source"])
                target.parent.mkdir(parents=True, exist_ok=True)
                partial = target.with_suffix(target.suffix + ".partial")
                if partial.exists():
                    raise RuntimeError("OCR未完成下载需检查: " + str(partial))
                with urllib.request.urlopen(item["url"], timeout=120) as source, partial.open("xb") as output:
                    shutil.copyfileobj(source, output)
                if digest(partial) != item["sha256"]:
                    raise RuntimeError("OCR下载哈希不符: " + item["source"])
                partial.rename(target)
            if digest(target) != item["sha256"]:
                raise RuntimeError("OCR固定模型哈希不符: " + item["source"])


def prepare(readonly=False):
    lock = json.loads((ROOT / "native-dependencies.lock.json").read_text(encoding="utf-8"))
    archives = CACHE / "archives"
    archives.mkdir(parents=True, exist_ok=True)
    consumed = {}
    for item in lock["binaries"]:
        archive = archives / item["filename"]
        if not archive.is_file():
            if readonly:
                raise RuntimeError("DEPENDENCY_ARCHIVE_MISSING:" + str(archive))
            partial = archives / (item["filename"] + ".partial")
            if partial.exists():
                raise RuntimeError("未完成的依赖下载需要人工检查: " + str(partial))
            with urllib.request.urlopen(item["url"], timeout=120) as source, partial.open("wb") as target:
                shutil.copyfileobj(source, target)
            verify(partial, item)
            partial.rename(archive)
        verify(archive, item)
        if item["id"] == "opencv":
            def extract_opencv(archive, target):
                seven_zip = shutil.which("7z") or Path("C:/Program Files/7-Zip/7z.exe")
                if not Path(seven_zip).is_file():
                    raise RuntimeError("解压官方 OpenCV 归档需要 7-Zip")
                subprocess.run([str(seven_zip), "x", str(archive), f"-o{target}", "-y",
                                "opencv/build/include/*",
                                "opencv/build/x64/vc16/lib/opencv_world4120.lib",
                                "opencv/build/x64/vc16/bin/opencv_world4120.dll"], check=True,
                               stdout=subprocess.DEVNULL)
            consumed[item["id"]] = verify_consumed_tree(archive, item["sha256"], CACHE / "opencv-unpacked",
                                 extract_opencv, "opencv/build/include/opencv2/opencv.hpp", readonly=readonly,
                                 consumed=lambda name: name.startswith("opencv/build/include/") or name in {
                                     "opencv/build/x64/vc16/lib/opencv_world4120.lib",
                                     "opencv/build/x64/vc16/bin/opencv_world4120.dll"})
        elif item["id"] == "onnxruntime":
            def extract_ort(archive, target):
                with zipfile.ZipFile(archive) as source:
                    for member in source.infolist():
                        destination = (target / member.filename).resolve()
                        if not destination.is_relative_to(target.resolve()):
                            raise RuntimeError("ORT 归档路径越界")
                    source.extractall(target)
            consumed[item["id"]] = verify_consumed_tree(archive, item["sha256"], CACHE / "ort-unpacked",
                                 extract_ort, "onnxruntime-win-x64-1.22.1/lib/onnxruntime.dll", readonly=readonly)
        else:
            consumed[item["id"]] = {"archive_sha256": item["sha256"]}
    prepare_ocr_models(readonly)
    print("Pinned native dependencies ready")
    return consumed


if __name__ == "__main__":
    prepare()
