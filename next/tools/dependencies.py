"""只准备 next 的固定头文件依赖；不安装全局软件，不访问生产配置。"""

import hashlib
import json
import os
from pathlib import Path
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def prepare():
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
    for name in ("boost", "json"):
        item = lock[name]
        archive = cache / (
            "boost-1.90.0.tar.gz" if name == "boost" else "json-3.11.3.hpp"
        )
        if not archive.exists() or sha(archive) != item["sha256"]:
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
            if not (target / ".verified").exists():
                with tarfile.open(archive, "r:gz") as tar:
                    for member in tar:
                        # 只展开编译所需头文件和许可证，不把整套 Boost 工具链放入工程。
                        if not member.isfile() or not (
                            member.name.startswith("boost_1_90_0/boost/")
                            or member.name == "boost_1_90_0/LICENSE_1_0.txt"
                        ):
                            continue
                        destination = (cache / member.name).resolve()
                        if not destination.is_relative_to(target.resolve()):
                            raise RuntimeError("Archive path outside dependency cache")
                        destination.parent.mkdir(parents=True, exist_ok=True)
                        with tar.extractfile(member) as source, destination.open(
                            "wb"
                        ) as out:
                            while chunk := source.read(1024 * 1024):
                                out.write(chunk)
                (target / ".verified").write_text(item["sha256"], encoding="ascii")
            paths[name] = target.as_posix()
        else:
            paths[name] = archive.as_posix()
    (ROOT / ".local").mkdir(exist_ok=True)
    (ROOT / ".local/dependencies.json").write_text(
        json.dumps(paths, indent=2), encoding="utf-8"
    )
    print("Pinned header dependencies ready", flush=True)


if __name__ == "__main__":
    prepare()
