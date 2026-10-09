"""Verify consumed dependency bytes against a fresh pinned-archive extraction."""

import hashlib
import json
from pathlib import Path
import shutil
import tempfile


def tree_identity(root):
    root = Path(root)
    files = {}
    for path in sorted(root.rglob("*")):
        if path.is_symlink():
            raise RuntimeError("DEPENDENCY_LINK_FORBIDDEN: " + str(path))
        if not path.is_file() or path.name in {".verified", ".content-manifest.json"}:
            continue
        with path.open("rb") as stream:
            digest = hashlib.file_digest(stream, "sha256").hexdigest()
        files[path.relative_to(root).as_posix()] = {"bytes": path.stat().st_size, "sha256": digest}
    if not files:
        raise RuntimeError("DEPENDENCY_TREE_EMPTY")
    return files


def require_identity(target, actual, expected):
    if actual == expected:
        return
    missing = sorted(expected.keys()-actual.keys())
    extra = sorted(actual.keys()-expected.keys())
    changed = sorted(key for key in expected.keys() & actual.keys() if expected[key] != actual[key])
    raise RuntimeError("DEPENDENCY_CONSUMED_BYTES_CHANGED:" + str(target) + ":" + json.dumps({
        "missing_count":len(missing),"extra_count":len(extra),"changed_count":len(changed),
        "missing":missing[:8],"extra":extra[:8],"changed":changed[:8]},ensure_ascii=False))


def verify_consumed_tree(archive, archive_hash, target, extract, marker, *, readonly=False, inspect=None, consumed=None):
    """A marker/manifest is not trusted input; derive the expected bytes anew.

    Existing trees are read-only until fully verified. A modified tree is refused,
    never silently repaired while a compiler or another candidate may consume it.
    """
    target = Path(target)
    def identity(root):
        files = tree_identity(root)
        return files if consumed is None else {name: value for name, value in files.items() if consumed(name)}
    if readonly and not target.is_dir():
        raise RuntimeError("DEPENDENCY_CONSUMED_TREE_MISSING:" + str(target))
    target.parent.mkdir(parents=True, exist_ok=True)
    with archive.open("rb") as stream:
        if hashlib.file_digest(stream, "sha256").hexdigest() != archive_hash:
            raise RuntimeError("DEPENDENCY_ARCHIVE_CHANGED")
    if inspect is not None and target.exists():
        expected = inspect(archive)
        if not expected or marker not in expected:
            raise RuntimeError("DEPENDENCY_LAYOUT_INVALID")
        if target.is_symlink():
            raise RuntimeError("DEPENDENCY_LINK_FORBIDDEN")
        if consumed is not None:
            expected = {name: value for name, value in expected.items() if consumed(name)}
        require_identity(target,identity(target),expected)
        return {"schema": 1, "archive_sha256": archive_hash, "files": expected}
    staging = Path(tempfile.mkdtemp(prefix="verified-extract-", dir=target.parent))
    published = False
    try:
        extract(archive, staging)
        if not (staging / marker).is_file():
            raise RuntimeError("DEPENDENCY_LAYOUT_INVALID")
        expected = identity(staging)
        if target.exists():
            if target.is_symlink():
                raise RuntimeError("DEPENDENCY_LINK_FORBIDDEN")
            require_identity(target,identity(target),expected)
        else:
            staging.rename(target)
            published = True
        manifest = {"schema": 1, "archive_sha256": archive_hash, "files": expected}
        if readonly:
            return manifest
        temporary = target / ".content-manifest.json.tmp"
        try:
            temporary.write_text(json.dumps(manifest, sort_keys=True, separators=(",", ":")) + "\n", encoding="utf-8")
            temporary.replace(target / ".content-manifest.json")
        finally:
            if temporary.exists():
                temporary.unlink()
        return manifest
    finally:
        # Only this call's fresh, reconstructible staging tree may be removed.
        if not published and staging.exists():
            shutil.rmtree(staging)
