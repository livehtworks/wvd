"""准备锁定的离线 SDK；只复制到 next/.local，不修改已有 M0 证据或用户环境。"""

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import zipfile

from build import ROOT


def digest(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def prepare(archive, model):
    lock = json.loads((ROOT / "dependencies.lock.json").read_text(encoding="utf-8"))
    if digest(archive) != lock["maafw"]["sdk_sha256"]:
        raise ValueError("SDK 压缩包与固定版本 hash 不符")
    target = ROOT / ".local/maafw-sdk" / lock["maafw"]["sdk_sha256"]
    target.mkdir(parents=True, exist_ok=True)
    hashes = {}
    with zipfile.ZipFile(archive) as sdk:
        for item in sdk.infolist():
            path = PurePosixPath(item.filename)
            if path.is_absolute() or ".." in path.parts or ":" in item.filename:
                raise ValueError("SDK 含越界路径")
            if item.is_dir():
                continue
            output = target.joinpath(*path.parts)
            if not output.resolve().is_relative_to(target.resolve()):
                raise ValueError("SDK 目录存在越界链接")
            data = sdk.read(item)
            expected = hashlib.sha256(data).hexdigest()
            if output.exists():
                if digest(output) != expected:
                    raise ValueError(
                        "已准备的 SDK 内容变化，停止而非覆盖: " + item.filename
                    )
            else:
                output.parent.mkdir(parents=True, exist_ok=True)
                output.write_bytes(data)
            hashes[item.filename] = expected
    model_hashes = {
        "det.onnx": "8fe4bf6abfb20402357827f2efc964c8b28cf980e29fe09a99b742ae29725fa9",
        "rec.onnx": "da12c6e863761d774b07d3bd40fbaaa55516f90570e0b1dd9dc112e457301cc9",
        "keys.txt": "5662df9d2d03f0e8ca0d3b0649d6acbab904b6a14b3d3521463c71c37c668ce3",
    }
    model_target = ROOT / ".local/maafw-model" / lock["ocr"]["commit"]
    model_target.mkdir(parents=True, exist_ok=True)
    for name, expected in model_hashes.items():
        if digest(model / name) != expected:
            raise ValueError("模型文件 hash 不符: " + name)
        output = model_target / name
        if output.exists() and digest(output) != expected:
            raise ValueError("已有隔离模型变化: " + name)
        if not output.exists():
            shutil.copyfile(model / name, output)
    config = {
        "sdk": target.as_posix(),
        "sdk_archive": archive.resolve().as_posix(),
        "sdk_files": hashes,
        "ocr": model_target.as_posix(),
        "ocr_files": model_hashes,
    }
    (ROOT / ".local/maafw.json").write_text(
        json.dumps(config, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    print(
        f"Verified SDK {lock['maafw']['version']}: {len(hashes)} files; OCR: 3 files. No device opened."
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk-archive", type=Path, required=True)
    parser.add_argument("--ocr-source", type=Path, required=True)
    args = parser.parse_args()
    prepare(args.sdk_archive, args.ocr_source)
