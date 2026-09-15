"""Read-only source/artifact capture. Never builds, executes artifacts, or uploads."""

import argparse
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import subprocess
import sys

BASELINE = "4d7c4fabd29bfadf55963bfdaecf79e3b6582bdd"
EXCLUDED = {".git", ".vscode", ".local", "build", "dist", "node_modules",
            "webnode_modules", "__pycache__", ".venv", "mod", "sdk", "models"}
TEXT_SUFFIXES = {".cpp", ".hpp", ".c", ".h", ".inc", ".in", ".rc", ".manifest",
                 ".py", ".md", ".txt", ".json", ".jsonl", ".cmake", ".toml",
                 ".ts", ".tsx", ".js", ".mjs", ".cjs", ".vue", ".css", ".html",
                 ".yml", ".yaml", ".ps1", ".bat", ".cmd", ".sh", ".lock"}
TEXT_NAMES = {"LICENSE", "Dockerfile", ".gitignore", ".gitattributes", ".clang-format",
              ".clang-tidy", ".editorconfig", ".npmrc"}
PRIVATE_NAMES = {"config.json", "input.json", "profile.json", ".env"}
ASSET_SUFFIXES = {".png", ".jpg", ".jpeg", ".gif", ".webp", ".bmp", ".ico", ".svg",
                  ".exe", ".dll", ".lib", ".pdb", ".obj", ".pyc", ".onnx", ".zip",
                  ".ttf", ".woff", ".woff2", ".mp4", ".pdf"}


class AuditError(ValueError):
    pass


def digest(data):
    return hashlib.sha256(data).hexdigest()


def encoded(value):
    return (json.dumps(value, ensure_ascii=True, sort_keys=True, indent=2) + "\n").encode()


def relative_name(name):
    if not isinstance(name, str) or not name or "\\" in name:
        raise AuditError("PATH_NOT_PORTABLE")
    p = PurePosixPath(name)
    parts = name.split("/")
    if p.is_absolute() or any(x in ("", ".", "..") for x in parts):
        raise AuditError("PATH_ESCAPE")
    for part in parts:
        if re.search(r'[<>:"|?*\x00-\x1f]', part) or part.endswith((".", " ")):
            raise AuditError("PATH_UNSAFE_COMPONENT")
        if re.fullmatch(r"(?i)(con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\..*)?", part):
            raise AuditError("PATH_RESERVED_COMPONENT")
    return p


def no_link(path):
    info = path.lstat()
    if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & 0x400:
        raise AuditError("LINK_OR_REPARSE_POINT")


def controlled(repo, name, area="next", must_exist=True):
    rel = relative_name(name)
    if not (str(rel) == area or str(rel).startswith(area + "/")):
        raise AuditError("PATH_OUTSIDE_ALLOWED_ROOT")
    repo = Path(repo)
    if not repo.is_absolute():
        raise AuditError("REPO_MUST_BE_ABSOLUTE")
    for ancestor in [repo, *repo.parents]:
        no_link(ancestor)
    path = repo
    for part in rel.parts:
        path = path / part
        if path.exists() or path.is_symlink():
            no_link(path)
        elif must_exist:
            raise FileNotFoundError(name)
    if not path.resolve().is_relative_to(repo.resolve()):
        raise AuditError("PATH_ESCAPE")
    return path


def read_bytes(path):
    no_link(path)
    before = path.stat()
    data = path.read_bytes()
    after = path.stat()
    if (before.st_size, before.st_mtime_ns, before.st_ino) != (after.st_size, after.st_mtime_ns, after.st_ino):
        raise AuditError("INPUT_CHANGED_DURING_READ")
    return data


def read_json(path):
    return json.loads(read_bytes(path).decode("utf-8-sig"))


def load_spec(name):
    value = json.loads(sys.stdin.read()) if name == "-" else read_json(Path(name).absolute())
    if value.get("schema") != 1 or value.get("baseline") != BASELINE:
        raise AuditError("SPEC_SCHEMA_OR_BASELINE")
    repo = Path(value["repo"])
    controlled(repo, "next")
    if name != "-":
        source = Path(name).absolute()
        try:
            relative = source.relative_to(repo).as_posix()
        except ValueError as exc:
            raise AuditError("SPEC_MUST_BE_PRIVATE_OR_STDIN") from exc
        controlled(repo, relative, "next/.local")
    return value


def source_kind(name):
    p = relative_name(name)
    if p.parts[0] != "next" or any(x.lower() in EXCLUDED for x in p.parts[1:-1]):
        return "excluded_directory"
    if p.name.lower() in PRIVATE_NAMES or p.name.startswith(".env"):
        return "private_input"
    if p.suffix.lower() in ASSET_SUFFIXES:
        return "non_source_asset"
    if p.suffix.lower() in TEXT_SUFFIXES or p.name in TEXT_NAMES:
        return "source"
    return "unclassified"


def inventory(repo):
    """Only the controlled next source tree is walked; evidence roots are pruned."""
    start = controlled(repo, "next")
    rows, excluded, names = [], [], set()
    for folder, directories, files in os.walk(start, followlinks=False):
        directories[:] = sorted(d for d in directories if d.lower() not in EXCLUDED)
        for d in directories:
            no_link(Path(folder) / d)
        for filename in sorted(files):
            path = Path(folder) / filename
            name = path.relative_to(repo).as_posix()
            controlled(repo, name)
            if name.casefold() in names:
                raise AuditError("SOURCE_CASE_COLLISION")
            names.add(name.casefold())
            kind = source_kind(name)
            if kind != "source":
                excluded.append({"path": name, "reason": kind})
                continue
            data = read_bytes(path)
            data.decode("utf-8-sig")
            rows.append({"path": name, "raw_sha256": digest(data), "bytes": len(data)})
    return sorted(rows, key=lambda x: x["path"]), sorted(excluded, key=lambda x: x["path"])


def git_baseline(repo):
    def git(*args, data=None):
        result = subprocess.run(["git", "-C", str(repo), *args], input=data,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
        if result.returncode:
            raise AuditError("GIT_READ_FAILED")
        return result.stdout
    tree = git("ls-tree", "-r", "-z", BASELINE, "--", "next")
    members = []
    for item in tree.split(b"\0"):
        if not item:
            continue
        meta, raw_name = item.split(b"\t", 1)
        mode, kind, oid = meta.split()
        name = raw_name.decode("utf-8")
        if source_kind(name) != "source":
            continue
        if kind != b"blob" or mode not in (b"100644", b"100755"):
            raise AuditError("BASELINE_NON_REGULAR_SOURCE")
        members.append((name, oid))
    stream = io.BytesIO(git("cat-file", "--batch", data=b"".join(oid + b"\n" for _, oid in members)))
    baseline = {}
    for name, oid in members:
        header = stream.readline().split()
        if len(header) != 3 or header[:2] != [oid, b"blob"]:
            raise AuditError("BASELINE_BATCH_PROTOCOL")
        size = int(header[2])
        data = stream.read(size)
        if len(data) != size or stream.read(1) != b"\n":
            raise AuditError("BASELINE_BATCH_TRUNCATED")
        baseline[name] = digest(data)
    tracked = set(git("ls-files", "-z", "--cached", "--", "next").decode().strip("\0").split("\0"))
    return baseline, tracked


def core_id(rows):
    selected = []
    locks = {"dependencies.lock.json", "opencv.lock.json", "native/maafw/CMakeLists.txt"}
    for row in rows:
        name = row["path"].removeprefix("next/")
        if name in locks or (name.startswith(("native/", "tests/native/")) and Path(name).suffix in (".cpp", ".hpp")):
            selected.append((name, row["raw_sha256"]))
    return digest("".join(f"{name}:{sha}\n" for name, sha in sorted(selected)).encode())


def source_snapshot(repo, baseline, tracked):
    rows, excluded = inventory(repo)
    for row in rows:
        old = baseline.get(row["path"])
        row.update(baseline_raw_sha256=old, tracked=row["path"] in tracked,
                   change="added" if old is None else "unchanged" if old == row["raw_sha256"] else "modified")
    current = {r["path"]: r["raw_sha256"] for r in rows}
    return {"schema": 1, "kind": "source_snapshot", "baseline": BASELINE,
            "inventory_sha256": digest(encoded(current)), "sources": rows,
            "deleted": sorted(set(baseline) - set(current)), "excluded": excluded,
            "delta_sha256": digest(encoded({"changed": {r["path"]: r["raw_sha256"] for r in rows if r["change"] != "unchanged"},
                                            "deleted": sorted(set(baseline) - set(current))})),
            "current_core_source_id": core_id(rows), "historical_source_binding": "UNVERIFIED"}


def check_source(repo, snapshot):
    if snapshot.get("kind") != "source_snapshot" or snapshot.get("baseline") != BASELINE:
        raise AuditError("SOURCE_SNAPSHOT_INVALID")
    rows, excluded = inventory(repo)
    current = {r["path"]: r["raw_sha256"] for r in rows}
    expected = {r["path"]: r["raw_sha256"] for r in snapshot["sources"]}
    if current != expected or digest(encoded(expected)) != snapshot["inventory_sha256"]:
        raise AuditError("SOURCE_CHANGED_SINCE_SNAPSHOT")
    if excluded != snapshot["excluded"]:
        raise AuditError("SOURCE_MEMBERSHIP_CHANGED")


def artifacts_snapshot(spec, before):
    repo = Path(spec["repo"])
    rows, _ = inventory(repo)
    current = digest(encoded({r["path"]: r["raw_sha256"] for r in rows}))
    artifacts, ids = [], set()
    for item in spec.get("artifacts", []):
        if item["id"] in ids:
            raise AuditError("DUPLICATE_ARTIFACT_ID")
        ids.add(item["id"])
        path = controlled(repo, item["path"], "next/build")
        if path.suffix.lower() != ".exe":
            raise AuditError("ARTIFACT_NOT_EXE")
        raw = read_bytes(path)
        artifacts.append({"id": item["id"], "path": item["path"], "sha256": digest(raw), "bytes": len(raw)})
    header_name = spec["generated_build_id"]
    header = read_bytes(controlled(repo, header_name, "next/build"))
    match = re.fullmatch(rb'\s*#pragma once\s+#define WVD_CORE_BUILD_ID "([0-9a-f]{64})"\s*', header)
    if not match:
        raise AuditError("GENERATED_BUILD_ID_FORMAT")
    generated = match[1].decode()
    result = {"schema": 1, "kind": "artifact_snapshot", "artifacts": artifacts,
              "before_inventory_sha256": before["inventory_sha256"], "current_inventory_sha256": current,
              "source_unchanged_since_before": current == before["inventory_sha256"],
              "generated_header": {"path": header_name, "sha256": digest(header), "build_id": generated},
              "generated_matches_current_core_sources": generated == core_id(rows),
              "source_binding": "UNVERIFIED", "build_record": None,
              "note": "A generated header alone does not prove the EXE was built from these sources."}
    if spec.get("build_record"):
        raw = read_bytes(controlled(repo, spec["build_record"], "next/.local"))
        receipt = json.loads(raw)
        consistent = (receipt.get("source_inventory_sha256") == current == before["inventory_sha256"]
                      and receipt.get("exit") == 0 and receipt.get("build_id") == generated
                      and receipt.get("exe_sha256") == {a["id"]: a["sha256"] for a in artifacts}
                      and result["generated_matches_current_core_sources"])
        result["build_record"] = {"path": spec["build_record"], "raw_sha256": digest(raw), "consistent": consistent}
        if consistent:
            result["source_binding"] = "RECORDED_BUILD_MATCH"
    return result


def new_output(repo, name):
    path = controlled(repo, name, "next/.local", must_exist=False)
    if path.exists():
        raise AuditError("OUTPUT_ALREADY_EXISTS")
    path.mkdir(parents=True, exist_ok=False)
    return path


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("source", "artifacts"))
    parser.add_argument("--spec", required=True, help="private JSON path or - for stdin")
    parser.add_argument("--source", help="repo-relative pre-build source.json")
    parser.add_argument("--out", required=True, help="new repo-relative next/.local directory")
    args = parser.parse_args(argv)
    try:
        spec = load_spec(args.spec)
        repo = Path(spec["repo"])
        if args.action == "source":
            baseline, tracked = git_baseline(repo)
            value = source_snapshot(repo, baseline, tracked)
            check_source(repo, value)
        else:
            if not args.source:
                raise AuditError("SOURCE_SNAPSHOT_REQUIRED")
            value = artifacts_snapshot(spec, read_json(controlled(repo, args.source, "next/.local")))
        out = new_output(repo, args.out)
        target = out / ("source.json" if args.action == "source" else "artifacts.json")
        target.write_bytes(encoded(value))
        print(json.dumps({"path": target.relative_to(repo).as_posix(), "sha256": digest(read_bytes(target))}))
        return 0
    except (AuditError, OSError, ValueError, KeyError) as exc:
        print(json.dumps({"error": type(exc).__name__, "detail": "Capture failed; check the private spec and inputs."}), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
