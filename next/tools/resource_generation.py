"""One writer and crash-recoverable swap for the generated resource pack."""

import hashlib
import json
import os
from pathlib import Path
import shutil
import uuid


def validate_pack(pack, *, allow_missing=False):
    if pack.is_symlink() or any(path.is_symlink() for path in pack.rglob("*")):
        raise RuntimeError("RESOURCE_LINK_FORBIDDEN")
    manifest = json.loads((pack / "manifest.json").read_text(encoding="utf-8"))
    seen = set()
    for row in manifest["files"]:
        relative = Path(row["path"])
        path = pack / relative
        if relative.is_absolute() or ".." in relative.parts or row["path"] in seen:
            raise RuntimeError("RESOURCE_MANIFEST_PATH_INVALID")
        seen.add(row["path"])
        if not path.exists() and allow_missing:
            continue
        if path.is_symlink() or not path.is_file():
            raise RuntimeError("RESOURCE_MEMBER_MISSING:" + row["path"])
        with path.open("rb") as stream:
            digest = hashlib.file_digest(stream, "sha256").hexdigest()
        if digest != row["sha256"] or path.stat().st_size != row["bytes"]:
            raise RuntimeError("RESOURCE_MEMBER_CHANGED:" + row["path"])


def generate_transaction(pack, generate, checkpoint=lambda phase: None, *, seed_missing=None):
    pack = Path(pack).resolve()
    parent = pack.parent.parent / ".local/resource-generation" / pack.name
    parent.mkdir(parents=True, exist_ok=True)
    lock = parent / "writer.lock"
    journal = parent / "transaction.json"
    # Exclusive OS file ownership avoids two source generators interleaving swaps.
    with lock.open("a+b") as owner:
        owner.seek(0)
        if os.name == "nt":
            import msvcrt
            if owner.read(1) == b"":
                owner.write(b"0")
                owner.flush()
            owner.seek(0)
            msvcrt.locking(owner.fileno(), msvcrt.LK_NBLCK, 1)
        else:
            import fcntl
            fcntl.flock(owner, fcntl.LOCK_EX | fcntl.LOCK_NB)
        if journal.exists():
            receipt = json.loads(journal.read_text(encoding="utf-8"))
            previous = parent / receipt["previous"]
            prepared = parent / receipt["prepared"]
            for path in (previous, prepared):
                if path.parent != parent or not path.name.startswith(pack.name + ".generation-"):
                    raise RuntimeError("RESOURCE_JOURNAL_PATH_INVALID")
            if not pack.exists():
                # A crash between renames restores the verified previous generation.
                validate_pack(previous, allow_missing=receipt.get("previous_incomplete", False))
                previous.rename(pack)
            validate_pack(pack, allow_missing=seed_missing is not None)
            journal.unlink()
        validate_pack(pack, allow_missing=seed_missing is not None)
        previous_incomplete = any(not (pack / row["path"]).is_file() for row in
            json.loads((pack / "manifest.json").read_text(encoding="utf-8"))["files"])
        nonce = uuid.uuid4().hex
        prepared = parent / (pack.name + ".generation-prepared-" + nonce)
        previous = parent / (pack.name + ".generation-previous-" + nonce)
        shutil.copytree(pack, prepared)
        if seed_missing is not None:
            seed_missing(prepared)
        validate_pack(prepared)
        generate(prepared)
        validate_pack(prepared)
        if not previous_incomplete and (prepared / "manifest.json").read_bytes() == (pack / "manifest.json").read_bytes():
            shutil.rmtree(prepared)  # This call's verified, unchanged generated copy only.
            return
        checkpoint("prepared")
        temporary = journal.with_suffix(".tmp")
        temporary.write_text(json.dumps({"schema": 1, "previous_incomplete": previous_incomplete, "previous": previous.name,
                                        "prepared": prepared.name}) + "\n", encoding="utf-8")
        temporary.replace(journal)
        checkpoint("journal")
        pack.rename(previous)
        checkpoint("previous_saved")
        prepared.rename(pack)
        checkpoint("published")
        validate_pack(pack)
        journal.unlink()
        # Previous generations remain recoverable; this routine never deletes them.
