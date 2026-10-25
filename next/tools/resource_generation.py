"""The shared, bounded directory transaction for generated packs and candidates."""

from contextlib import contextmanager
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import uuid

RUN_RESERVE_BYTES = 2 * 1024 ** 3
TRANSACTION_MARGIN_BYTES = 4 * 1024 ** 2
MAX_INVENTORY_ENTRIES = 100000
MAX_JOURNAL_BYTES = 1024 ** 2


def _children(root):
    with os.scandir(root) as scan:
        for count, entry in enumerate(scan, 1):
            if count > MAX_INVENTORY_ENTRIES:
                raise RuntimeError("DIRECTORY_TRANSACTION_INVENTORY_LIMIT")
            yield Path(entry.path)


def _identity(path):
    """Do not resolve away a junction before checking its identity."""
    path = Path(path).absolute()
    for ancestor in reversed((path, *path.parents)):
        info = ancestor.lstat()
        if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & 0x400:
            raise RuntimeError("DIRECTORY_TRANSACTION_REPARSE:" + str(ancestor))
    info = path.stat()
    if not path.is_dir() or not info.st_ino:
        raise RuntimeError("DIRECTORY_TRANSACTION_IDENTITY_INVALID:" + str(path))
    return [info.st_dev, info.st_ino]


def _require_identity(path, expected):
    if expected is None or _identity(path) != expected:
        raise RuntimeError("DIRECTORY_TRANSACTION_IDENTITY_CHANGED:" + str(path))


def _mkdir_plain(path):
    path = Path(path).absolute()
    missing, ancestor = [], path
    while True:
        try:
            ancestor.lstat()
            break
        except FileNotFoundError:
            missing.append(ancestor)
            ancestor = ancestor.parent
    _identity(ancestor)  # Verify all existing ancestors before the first mkdir.
    for directory in reversed(missing):
        directory.mkdir()
        _identity(directory)


def _inventory(paths):
    """Count every entry; report logical bytes separately from unique file bytes."""
    count = logical = file_logical = 0
    seen = set()
    entries = []
    for root in paths:
        if not root.exists():
            continue
        identity = _identity(root)
        root_bytes = 0
        waiting = [root]
        while waiting:
            parent = waiting.pop()
            _identity(parent)
            for path in _children(parent):
                count += 1
                if count > MAX_INVENTORY_ENTRIES:
                    raise RuntimeError("DIRECTORY_TRANSACTION_INVENTORY_LIMIT")
                info = path.lstat()
                if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & 0x400:
                    raise RuntimeError("DIRECTORY_TRANSACTION_REPARSE:" + str(path))
                if not stat.S_ISREG(info.st_mode) and not stat.S_ISDIR(info.st_mode):
                    raise RuntimeError("DIRECTORY_TRANSACTION_ENTRY_INVALID:" + str(path))
                if stat.S_ISDIR(info.st_mode):
                    waiting.append(path)
                if stat.S_ISREG(info.st_mode):
                    logical += info.st_size
                    root_bytes += info.st_size
                    key = (info.st_dev, info.st_ino)
                    if key not in seen:
                        file_logical += info.st_size
                        seen.add(key)
        _require_identity(root, identity)
        entries.append({"path": str(root), "identity": identity, "logical_bytes": root_bytes})
    return {"entries": entries, "entry_count": count, "logical_bytes": logical,
            "unique_file_logical_bytes": file_logical,
            "accounting": "File-ID deduplication of file lengths; not exclusive allocated disk bytes"}


def _allocation_unit(parent):
    """Target-volume allocation unit for conservative copy/metadata admission."""
    if os.name == "nt":
        import ctypes
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.GetVolumePathNameW.argtypes = [ctypes.c_wchar_p, ctypes.c_wchar_p, ctypes.c_uint32]
        kernel.GetDiskFreeSpaceW.argtypes = [ctypes.c_wchar_p, *([ctypes.POINTER(ctypes.c_uint32)] * 4)]
        volume = ctypes.create_unicode_buffer(32768)
        values = [ctypes.c_uint32() for _ in range(4)]
        if not kernel.GetVolumePathNameW(str(Path(parent).absolute()), volume, len(volume)) or not \
                kernel.GetDiskFreeSpaceW(volume.value, *[ctypes.byref(v) for v in values]):
            raise RuntimeError("DIRECTORY_TRANSACTION_ALLOCATION_UNKNOWN")
        unit = values[0].value * values[1].value
    else:
        unit = os.statvfs(parent).f_frsize
    if unit <= 0 or unit > 2 ** 32:
        raise RuntimeError("DIRECTORY_TRANSACTION_ALLOCATION_UNKNOWN")
    return max(unit, 4096)  # Includes a conservative per-entry metadata allowance.


def _durable_json(path, record):
    data = (json.dumps(record, ensure_ascii=False, sort_keys=True) + "\n").encode("utf-8")
    if len(data) > MAX_JOURNAL_BYTES:
        raise RuntimeError("DIRECTORY_TRANSACTION_JOURNAL_LIMIT")
    pending = path.with_name(path.name + ".pending-" + uuid.uuid4().hex)
    # A failed pending record remains visible and blocks the next transaction.
    try:
        with pending.open("xb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(pending, path)
    except Exception as error:
        raise RuntimeError("DIRECTORY_TRANSACTION_JOURNAL_WRITE_FAILED:pending=" + str(pending) +
                           ";primary_error=" + str(error)) from error


@contextmanager
def _writer(root):
    root_id = _identity(root)
    path = root / "writer.lock"
    def plain_lock():
        info = path.lstat()
        if not stat.S_ISREG(info.st_mode) or getattr(info, "st_file_attributes", 0) & 0x400 or info.st_nlink != 1:
            raise RuntimeError("DIRECTORY_TRANSACTION_LOCK_INVALID:" + str(path))
        return [info.st_dev, info.st_ino]
    if path.exists() or path.is_symlink():
        plain_lock()
    with path.open("a+b") as owner:
        expected = plain_lock()
        held = os.fstat(owner.fileno())
        _require_identity(root, root_id)
        if expected != [held.st_dev, held.st_ino]:
            raise RuntimeError("DIRECTORY_TRANSACTION_LOCK_CHANGED")
        owner.seek(0)
        if os.name == "nt":
            import msvcrt
            msvcrt.locking(owner.fileno(), msvcrt.LK_NBLCK, 1)
            if owner.read(1) == b"":
                owner.write(b"0")
                owner.flush()
        else:
            import fcntl
            fcntl.flock(owner, fcntl.LOCK_EX | fcntl.LOCK_NB)
        yield


def _remove_owned(path, identity):
    if path.exists():
        _require_identity(path, identity)
        shutil.rmtree(path)


def _transaction_record(journal, current, root, kind):
    if journal.stat().st_size > MAX_JOURNAL_BYTES or journal.is_symlink():
        raise RuntimeError("DIRECTORY_TRANSACTION_JOURNAL_INVALID")
    record = json.loads(journal.read_text(encoding="utf-8"))
    if record.get("schema") != 2 or record.get("kind") != kind or record.get("current") != str(current):
        raise RuntimeError("DIRECTORY_TRANSACTION_JOURNAL_INVALID")
    if record.get("root_identity") != _identity(root):
        raise RuntimeError("DIRECTORY_TRANSACTION_ROOT_CHANGED")
    for key, prefix in (("prepared", "prepared-"), ("previous", "previous-")):
        name = record.get(key, "")
        if Path(name).name != name or name != prefix + record.get("transaction", ""):
            raise RuntimeError("DIRECTORY_TRANSACTION_JOURNAL_PATH_INVALID")
    return record


def _recover(record, current, root, journal, validate, *, failed=False):
    """Recover only the indexed transaction, and end its index after cleanup."""
    prepared, previous = root / record["prepared"], root / record["previous"]
    old_id, new_id = record.get("current_identity"), record.get("prepared_identity")
    if not current.exists():
        if previous.exists():
            _require_identity(previous, old_id)
            validate(previous, previous=True)
            previous.rename(current)
        elif old_id is not None:
            raise RuntimeError("DIRECTORY_TRANSACTION_AUTHORITY_MISSING")
        elif prepared.exists() and record.get("phase") in ("published", "verified") and not failed:
            raise RuntimeError("DIRECTORY_TRANSACTION_CURRENT_MISSING")
    if current.exists():
        actual = _identity(current)
        if actual == new_id:
            try:
                validate(current, previous=False)
            except BaseException:
                if old_id is None:
                    current.rename(prepared)
                else:
                    if not previous.exists():
                        raise
                    _require_identity(previous, old_id)
                    validate(previous, previous=True)
                    current.rename(prepared)
                    previous.rename(current)
        elif actual != old_id:
            raise RuntimeError("DIRECTORY_TRANSACTION_AUTHORITY_CHANGED")
        else:
            validate(current, previous=True)
    if prepared.exists():
        _remove_owned(prepared, new_id)
    if previous.exists():
        _require_identity(previous, old_id)
        validate(previous, previous=True)
    journal.unlink()


def directory_transaction(current, prepare, validate, *, root, kind, required_bytes,
                          checkpoint=lambda phase: None, allow_initial=False, unchanged=None):
    """Internal pack/candidate replacement; never deploys or scans outside its roots."""
    if kind not in ("resource", "candidate"):
        raise RuntimeError("DIRECTORY_TRANSACTION_KIND_INVALID")
    current, root = Path(current).absolute(), Path(root).absolute()
    _identity(current.parent)
    _mkdir_plain(root)
    _identity(root)
    if current.parent.stat().st_dev != root.stat().st_dev:
        raise RuntimeError("DIRECTORY_TRANSACTION_VOLUME_MISMATCH")
    journal = root / "transaction.json"
    with _writer(root):
        pending = [p.name for p in _children(root) if p.name.startswith("transaction.json.pending-")]
        if pending:
            raise RuntimeError("DIRECTORY_TRANSACTION_JOURNAL_PENDING:" + ",".join(pending[:4]))
        if journal.exists():
            record = _transaction_record(journal, current, root, kind)
            if record.get("cleanup_error"):
                raise RuntimeError("DIRECTORY_TRANSACTION_CLEANUP_PENDING:" + record["cleanup_error"])
            _recover(record, current, root, journal, validate)
        old_id = _identity(current) if current.exists() else None
        if old_id is None and not allow_initial:
            raise RuntimeError("DIRECTORY_TRANSACTION_CURRENT_MISSING")
        historical = []
        for path in _children(root):
            if path.name == "writer.lock":
                continue
            if path.is_dir() and path.name.startswith(("previous-", current.name + ".generation-previous-")):
                historical.append(path)
            else:
                raise RuntimeError("DIRECTORY_TRANSACTION_UNKNOWN_ENTRY:" + str(path))
        if kind == "candidate":
            for path in _children(current.parent):
                if path.name.startswith((current.name + ".previous-", current.name + ".staging-")):
                    historical.append(path)
        inventory = _inventory(([current] if old_id is not None else []) + historical)
        required_bytes = required_bytes() if callable(required_bytes) else required_bytes
        if not isinstance(required_bytes, int) or isinstance(required_bytes, bool) or required_bytes < 0:
            raise RuntimeError("DIRECTORY_TRANSACTION_SIZE_UNKNOWN")
        needed = required_bytes + TRANSACTION_MARGIN_BYTES + RUN_RESERVE_BYTES
        if needed > 2 ** 64 - 1:
            raise RuntimeError("DIRECTORY_TRANSACTION_BUDGET_OVERFLOW")
        available = shutil.disk_usage(current.parent).free
        if not isinstance(available, int) or available < needed:
            raise RuntimeError("DIRECTORY_TRANSACTION_SPACE_LOW:" + json.dumps({
                "available": available, "required": needed, "inventory": inventory}))
        # Bound the whole inventory and reject insufficient space before hashing
        # retained generations. A per-directory bound alone is not a global bound.
        if old_id is not None:
            validate(current, previous=True)
        for path in historical:
            validate(path, previous=True)
        nonce = uuid.uuid4().hex
        record = {"schema": 2, "kind": kind, "transaction": nonce, "current": str(current),
                  "root_identity": _identity(root), "current_identity": old_id,
                  "prepared": "prepared-" + nonce, "previous": "previous-" + nonce,
                  "prepared_identity": None, "phase": "planned", "inventory": inventory,
                  "required_stage_bytes": required_bytes}
        prepared, previous = root / record["prepared"], root / record["previous"]
        _durable_json(journal, record)
        try:
            prepared.mkdir()
            record["prepared_identity"] = _identity(prepared)
            record["phase"] = "created"
            _durable_json(journal, record)
            checkpoint("journal")
            prepare(prepared)
            _require_identity(prepared, record["prepared_identity"])
            validate(prepared, previous=False)
            actual = _inventory([prepared])["logical_bytes"]
            if actual > required_bytes:
                raise RuntimeError("DIRECTORY_TRANSACTION_STAGE_BUDGET_EXCEEDED")
            record["phase"] = "prepared"
            _durable_json(journal, record)
            checkpoint("prepared")
            if unchanged is not None and old_id is not None and unchanged(prepared, current):
                _remove_owned(prepared, record["prepared_identity"])
                journal.unlink()
                return current
            if old_id is not None:
                _require_identity(current, old_id)
                current.rename(previous)
            record["phase"] = "previous_saved"
            _durable_json(journal, record)
            checkpoint("previous_saved")
            _require_identity(prepared, record["prepared_identity"])
            prepared.rename(current)
            record["phase"] = "published"
            _durable_json(journal, record)
            checkpoint("published")
            validate(current, previous=False)
            record["phase"] = "verified"
            _durable_json(journal, record)
            journal.unlink()
            return current
        except BaseException as primary:
            try:
                _recover(record, current, root, journal, validate, failed=True)
            except BaseException as cleanup:
                record["primary_error"], record["cleanup_error"] = str(primary), str(cleanup)
                try:
                    _durable_json(journal, record)
                except BaseException as log_error:
                    raise RuntimeError("DIRECTORY_TRANSACTION_CLEANUP_PENDING:primary=" + str(primary) +
                                       ";cleanup=" + str(cleanup) + ";journal=" + str(log_error)) from primary
                raise RuntimeError("DIRECTORY_TRANSACTION_CLEANUP_PENDING:primary=" + str(primary) +
                                   ";cleanup=" + str(cleanup)) from primary
            raise


def validate_pack(pack, *, allow_missing=False):
    _identity(pack)
    _inventory([pack])
    manifest = json.loads((pack / "manifest.json").read_text(encoding="utf-8"))
    seen = set()
    for row in manifest["files"]:
        relative = Path(row["path"])
        path = pack / relative
        if relative.is_absolute() or ".." in relative.parts or "\\" in row["path"] or ":" in row["path"] or row["path"] in seen:
            raise RuntimeError("RESOURCE_MANIFEST_PATH_INVALID")
        seen.add(row["path"])
        if not path.exists() and allow_missing:
            continue
        if not path.is_file():
            raise RuntimeError("RESOURCE_MEMBER_MISSING:" + row["path"])
        with path.open("rb") as stream:
            digest = hashlib.file_digest(stream, "sha256").hexdigest()
        if digest != row["sha256"] or path.stat().st_size != row["bytes"]:
            raise RuntimeError("RESOURCE_MEMBER_CHANGED:" + row["path"])


def generate_transaction(pack, generate, checkpoint=lambda phase: None, *, seed_missing=None,
                         additional_bytes=0, additional_entries=0):
    pack = Path(pack).absolute()
    state = {}
    def required():
        manifest = json.loads((pack / "manifest.json").read_text(encoding="utf-8"))
        missing = 0
        seen = set()
        for row in manifest["files"]:
            name = row["path"]; relative = Path(name)
            if relative.is_absolute() or ".." in relative.parts or "\\" in name or ":" in name or name in seen:
                raise RuntimeError("RESOURCE_MANIFEST_PATH_INVALID")
            seen.add(name)
            if not isinstance(row.get("bytes"), int) or isinstance(row["bytes"], bool) or row["bytes"] < 0:
                raise RuntimeError("DIRECTORY_TRANSACTION_SIZE_UNKNOWN")
            if not (pack / relative).is_file():
                missing += row["bytes"]
        if not isinstance(additional_bytes, int) or additional_bytes < 0 or \
                not isinstance(additional_entries, int) or additional_entries < 0:
            raise RuntimeError("DIRECTORY_TRANSACTION_SIZE_UNKNOWN")
        state["incomplete"] = bool(missing)
        inventory = _inventory([pack])
        entries = inventory["entry_count"] + len(manifest["files"]) + additional_entries + 8
        return inventory["logical_bytes"] + missing + additional_bytes + \
            entries * _allocation_unit(pack.parent) + TRANSACTION_MARGIN_BYTES
    def validate(path, *, previous):
        validate_pack(path, allow_missing=previous and seed_missing is not None)
    def prepare(path):
        shutil.copytree(pack, path, dirs_exist_ok=True)
        if seed_missing is not None:
            seed_missing(path)
        validate_pack(path)
        generate(path)
    return directory_transaction(pack, prepare, validate,
        root=pack.parent.parent / ".local/resource-generation" / pack.name,
        kind="resource", required_bytes=required, checkpoint=checkpoint,
        unchanged=lambda ready, old: not state["incomplete"] and
            (ready / "manifest.json").read_bytes() == (old / "manifest.json").read_bytes())
