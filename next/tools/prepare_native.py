"""Fetch and verify only the pinned standalone native runtime dependencies."""

import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / ".local/native-deps"


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def verify(path, item):
    if path.stat().st_size != item["size"] or digest(path) != item["sha256"]:
        raise RuntimeError(f"固定依赖校验失败: {item['id']}")


def prepare():
    lock = json.loads((ROOT / "native-dependencies.lock.json").read_text(encoding="utf-8"))
    archives = CACHE / "archives"
    archives.mkdir(parents=True, exist_ok=True)
    for item in lock["binaries"]:
        archive = archives / item["filename"]
        if not archive.is_file():
            partial = archives / (item["filename"] + ".partial")
            if partial.exists():
                raise RuntimeError("未完成的依赖下载需要人工检查: " + str(partial))
            with urllib.request.urlopen(item["url"], timeout=120) as source, partial.open("wb") as target:
                shutil.copyfileobj(source, target)
            verify(partial, item)
            partial.rename(archive)
        verify(archive, item)
        if item["id"] == "opencv":
            marker = CACHE / "opencv-unpacked/opencv/build/include/opencv2/opencv.hpp"
            if not marker.is_file():
                seven_zip = shutil.which("7z") or Path("C:/Program Files/7-Zip/7z.exe")
                if not Path(seven_zip).is_file():
                    raise RuntimeError("解压官方 OpenCV 归档需要 7-Zip")
                target = CACHE / "opencv-unpacked"
                target.mkdir(exist_ok=True)
                subprocess.run([str(seven_zip), "x", str(archive), f"-o{target}", "-y"], check=True,
                               stdout=subprocess.DEVNULL)
                if not marker.is_file():
                    raise RuntimeError("OpenCV 官方归档结构与锁定布局不符")
        elif item["id"] == "onnxruntime":
            marker = CACHE / "ort-unpacked/onnxruntime-win-x64-1.22.1/lib/onnxruntime.dll"
            if not marker.is_file():
                target = CACHE / "ort-unpacked"
                target.mkdir(exist_ok=True)
                with zipfile.ZipFile(archive) as source:
                    for member in source.infolist():
                        destination = (target / member.filename).resolve()
                        if not destination.is_relative_to(target.resolve()):
                            raise RuntimeError("ORT 归档路径越界")
                    source.extractall(target)
                if not marker.is_file():
                    raise RuntimeError("ORT 官方归档结构与锁定布局不符")
    print("Pinned native dependencies ready")


if __name__ == "__main__":
    prepare()
