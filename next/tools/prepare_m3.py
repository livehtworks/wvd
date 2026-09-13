"""固定源码的视觉资源和官方同 ABI OpenCV；不读取生产配置，不连接设备。"""

import hashlib
import json
from pathlib import Path
import subprocess
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parent
BASELINE = "6585f4075f5714ab522aa582993860c09af912c1"
DEPS_SHA = "f67031e183735fad3faa6f9280f509d3ff1e25ae0e7e33a9185f275ef0f79cf7"
DEPS_URL = "https://github.com/MaaXYZ/MaaDeps/releases/download/v2.12.6/MaaDeps-x64-windows-devel.tar.xz"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def save(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )


def prepare_assets():
    report = json.loads(
        (ROOT / "docs/migration/asset_case_report.json").read_text(encoding="utf-8")
    )
    names = (
        subprocess.check_output(
            ["git", "ls-tree", "-r", "--name-only", "-z", BASELINE, "resources/images"],
            cwd=REPO,
        )
        .decode()
        .split("\0")
    )
    names = sorted(p for p in names if p.lower().endswith(".png"))
    files, aliases, dynamic = [], {}, []
    pack = ROOT / "packs/wvd"
    for source in names:
        data = subprocess.check_output(
            ["git", "show", f"{BASELINE}:{source}"], cwd=REPO
        )
        relative = "image/" + source.removeprefix("resources/images/")
        path = pack / relative
        if path.exists() and path.read_bytes() != data:
            raise RuntimeError("拒绝覆盖不同的作者资产: " + relative)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        refs = [r for r in report["references"] if source in r["matches"]]
        files.append(
            {
                "path": relative,
                "source": source,
                "sha256": sha(data),
                "bytes": len(data),
                "source_refs": [r["source_ref"] for r in refs],
            }
        )
    for ref in report["references"]:
        if ref["status"] == "CASE_MISMATCH":
            if len(ref["matches"]) != 1:
                raise RuntimeError("资源别名歧义")
            alias = ref["reference"] + ".png"
            target = ref["matches"][0].removeprefix("resources/images/")
            if alias in aliases and aliases[alias] != target:
                raise RuntimeError("资源别名冲突")
            aliases[alias] = target
        if ref["status"] != "DYNAMIC_REVIEW":
            continue
        expression = ref["reference"]
        candidates, category, explanation = (
            [],
            "NEEDS_BUSINESS_DATA",
            "通用参数或任务字段：M4 调用方须提供具体资源；不据此批准输入。",
        )
        if expression in (
            "f'fastforward_off'",
            "f'spellskill/skillLvl/lv1'",
            "f'spellskill/skillLvl/s_lv1'",
        ):
            candidates = [expression[2:-1] + ".png"]
            category, explanation = "RESOLVED", "无插值的 f-string，等价于字面量。"
        elif expression == "combat":
            candidates = [
                "combatActive" + suffix + ".png" for suffix in ("", "_2", "_3", "_4")
            ]
        elif "skillLvl/" in expression:
            prefix = "s_lv" if "/s_lv" in expression else "lv"
            candidates = [
                p.removeprefix("resources/images/")
                for p in names
                if p.startswith("resources/images/spellskill/skillLvl/" + prefix)
            ]
        elif expression == "f'cursor_{i}'":
            candidates = [
                p.removeprefix("resources/images/")
                for p in names
                if p.startswith("resources/images/cursor_")
            ]
        elif expression == "correctStair":
            candidates = [
                p.removeprefix("resources/images/")
                for p in names
                if p.startswith("resources/images/stair_")
            ]
        elif "spellskill/char/" in expression:
            candidates = [
                p.removeprefix("resources/images/")
                for p in names
                if p.startswith("resources/images/spellskill/char/")
            ]
        elif expression == "'dialogueChoices/' + op":
            candidates = [
                p.removeprefix("resources/images/")
                for p in names
                if p.startswith("resources/images/dialogueChoices/")
            ]
        if candidates and category != "RESOLVED":
            category, explanation = (
                "BOUNDED_CANDIDATES",
                "候选集来自固定源码/资源；具体选择仍由业务参数确定。",
            )
        for name in candidates:
            if "resources/images/" + name not in names:
                raise RuntimeError("动态资源候选不存在: " + name)
        dynamic.append(
            {
                **ref,
                "classification": category,
                "candidates": candidates,
                "reason": explanation,
                "blocks": "M4 调用参数绑定" if category != "RESOLVED" else None,
            }
        )
    revision = sha(json.dumps(files, sort_keys=True, ensure_ascii=False).encode())
    manifest = {
        "schema_version": 1,
        "source_commit": BASELINE,
        "revision": revision,
        "files": files,
        "aliases": aliases,
        "case_reference_count": sum(
            r["status"] == "CASE_MISMATCH" for r in report["references"]
        ),
        "dynamic_references": dynamic,
    }
    save(pack / "manifest.json", manifest)
    return manifest


def prepare_opencv():
    cache = ROOT / ".local/maadeps-2.12.6"
    archive = cache / "maadeps-devel.tar.xz"
    cache.mkdir(parents=True, exist_ok=True)
    if not archive.exists():
        # 可复用经过哈希核对的本轮下载；不是个人路径或安装路径依赖。
        candidates = list((ROOT / ".local").glob("m2fix-m3-*/maadeps-devel.tar.xz"))
        if candidates:
            archive.write_bytes(candidates[0].read_bytes())
        else:
            urllib.request.urlretrieve(DEPS_URL, archive)
    if sha(archive.read_bytes()) != DEPS_SHA:
        raise RuntimeError("MAADEPS_ARCHIVE_HASH_MISMATCH")
    prefix = "vcpkg/installed/maa-x64-windows/"
    with tarfile.open(archive) as tar:
        for entry in tar:
            name = entry.name.removeprefix("./")
            if not entry.isfile() or not (
                name.startswith(prefix + "include/opencv4/")
                or name
                in (
                    prefix + "lib/opencv_world4.lib",
                    prefix + "bin/opencv_world4_maa.dll",
                )
            ):
                continue
            path = cache / name
            if not path.resolve().is_relative_to(cache.resolve()):
                raise RuntimeError("MAADEPS_PATH_ESCAPE")
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(tar.extractfile(entry).read())
    sdk = Path(
        json.loads((ROOT / ".local/maafw.json").read_text(encoding="utf-8"))["sdk"]
    )
    base = cache / prefix
    dll_sha = sha((base / "bin/opencv_world4_maa.dll").read_bytes())
    if dll_sha != sha((sdk / "bin/opencv_world4_maa.dll").read_bytes()):
        raise RuntimeError("OPENCV_SDK_ABI_BINARY_MISMATCH")
    lock = {
        "version": "4.12.0",
        "source": DEPS_URL,
        "archive_sha256": DEPS_SHA,
        "dll_sha256": dll_sha,
        "files": {
            p.relative_to(base).as_posix(): sha(p.read_bytes())
            for p in sorted(base.rglob("*"))
            if p.is_file() and ("include" in p.parts or p.suffix in (".lib", ".dll"))
        },
    }
    save(ROOT / "opencv.lock.json", lock)
    save(ROOT / ".local/opencv.json", {"root": str(base.resolve())})


if __name__ == "__main__":
    manifest = prepare_assets()
    prepare_opencv()
    print(
        json.dumps(
            {
                "images": len(manifest["files"]),
                "bytes": sum(f["bytes"] for f in manifest["files"]),
                "revision": manifest["revision"],
                "dynamic": len(manifest["dynamic_references"]),
            }
        )
    )
