"""只准备 next 的固定头文件依赖；不安装全局软件，不访问生产配置。"""

import hashlib
import json
import os
from pathlib import Path
import tarfile
import urllib.request
from dependency_tree import verify_consumed_tree

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def prepare(readonly=False):
    lock = json.loads((ROOT / "dependencies.lock.json").read_text(encoding="utf-8"))
    cache = Path(
        os.environ.get(
            "WVD_NEXT_CACHE",
            Path(os.environ.get("LOCALAPPDATA", Path.home() / ".cache"))
            / "WvdNext/dependencies",
        )
    )
    cache.mkdir(parents=True, exist_ok=True)
    paths = {}
    consumed = {}
    for name in ("boost", "json"):
        item = lock[name]
        archive = cache / (
            "boost-1.90.0.tar.gz" if name == "boost" else "json-3.11.3.hpp"
        )
        if not archive.exists() or sha(archive) != item["sha256"]:
            if readonly:
                raise RuntimeError("DEPENDENCY_ARCHIVE_CHANGED_OR_MISSING:" + name)
            partial = archive.with_suffix(archive.suffix + ".partial")
            print("Downloading", item["url"], flush=True)
            with urllib.request.urlopen(
                item["url"], timeout=120
            ) as source, partial.open("wb") as out:
                while chunk := source.read(1024 * 1024):
                    out.write(chunk)
            if sha(partial) != item["sha256"]:
                raise RuntimeError("Dependency checksum mismatch: " + name)
            partial.replace(archive)
        if name == "boost":
            target = cache / "boost_1_90_0"
            def inspect_boost(archive):
                expected = {}
                with tarfile.open(archive, "r:gz") as tar:
                    for member in tar:
                        if not member.isfile() or not (member.name.startswith("boost_1_90_0/boost/")
                            or member.name == "boost_1_90_0/LICENSE_1_0.txt"):
                            continue
                        relative = Path(member.name).relative_to("boost_1_90_0")
                        if relative.is_absolute() or ".." in relative.parts:
                            raise RuntimeError("Archive path outside dependency cache")
                        with tar.extractfile(member) as source:
                            expected[relative.as_posix()] = {"bytes": member.size,
                                "sha256": hashlib.file_digest(source, "sha256").hexdigest()}
                return expected
            def extract_boost(archive, staging):
                with tarfile.open(archive, "r:gz") as tar:
                    for member in tar:
                        # 只展开编译所需头文件和许可证，不把整套 Boost 工具链放入工程。
                        if not member.isfile() or not (
                            member.name.startswith("boost_1_90_0/boost/")
                            or member.name == "boost_1_90_0/LICENSE_1_0.txt"
                        ):
                            continue
                        relative = Path(member.name).relative_to("boost_1_90_0")
                        destination = (staging / relative).resolve()
                        if not destination.is_relative_to(staging.resolve()):
                            raise RuntimeError("Archive path outside dependency cache")
                        destination.parent.mkdir(parents=True, exist_ok=True)
                        with tar.extractfile(member) as source, destination.open(
                            "wb"
                        ) as out:
                            while chunk := source.read(1024 * 1024):
                                out.write(chunk)
            consumed[name] = verify_consumed_tree(archive, item["sha256"], target, extract_boost,
                "boost/version.hpp", readonly=readonly, inspect=inspect_boost)
            paths[name] = target.as_posix()
        else:
            paths[name] = archive.as_posix()
            consumed[name] = {"sha256": item["sha256"]}
    if readonly:
        configured = json.loads((ROOT / ".local/dependencies.json").read_text(encoding="utf-8"))
        if paths != configured:
            raise RuntimeError("DEPENDENCY_CONFIGURED_PATH_CHANGED")
        return consumed
    (ROOT / ".local").mkdir(exist_ok=True)
    (ROOT / ".local/dependencies.json").write_text(
        json.dumps(paths, indent=2), encoding="utf-8"
    )
    print("Pinned header dependencies ready", flush=True)
    return consumed


if __name__ == "__main__":
    prepare()
