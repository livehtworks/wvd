"""Explicit-file audit export; no evidence discovery, native calls, or uploads."""

import argparse
import json
from pathlib import Path
import re
import sys
import zipfile

import audit_snapshot as snap

SENSITIVE = {"profile", "profile_source", "profile_store", "legacy_document", "legacy_source",
             "legacy_passthrough", "config", "configuration", "environment", "env", "values",
             "input", "effective_profile", "resolved_profile", "legacy_config", "export",
             "adb_adress", "serial", "device_id", "emu_path", "emu_index",
             "credentials", "authorization", "cookie", "set_cookie", "private_key", "password",
             "passwd", "secret", "token", "api_key", "access_key", "connection_string"}
FORBIDDEN_PARTS = {"bundle", "assets", "run-resources", "active-snapshots", "compiled",
                   "mod", "isolated-mod", "sdk", "models", "image", "images", "config"}
SECRET_LITERAL = re.compile(r"(?i)(?:\b(?:gh[pousr]_[A-Za-z0-9]{20,}|AKIA[A-Z0-9]{16}|sk-[A-Za-z0-9_-]{20,})\b|-----BEGIN [A-Z ]*PRIVATE KEY-----|Bearer\s+[A-Za-z0-9._~-]{12,}|https?://[^\s/@:]+:[^\s/@]+@)")
ABSOLUTE = re.compile(r"(?i)(?:\b[A-Z]:[\\/]|\\\\[^\s\\/]+[\\/]|(?<![:\w./])//[^\s/]+/|(?<![\w./:])/(?:home|users|tmp|mnt|etc|var|opt|usr|run|root|data|workspace|volumes)(?:/|\b))")


def sensitive(key):
    key = re.sub(r"(?<=[a-z])(?=[A-Z])", "_", key).lower().replace("-", "_")
    return key in SENSITIVE or any(key.endswith("_" + x) for x in ("password", "secret", "token", "api_key", "private_key", "connection_string"))


class Redactor:
    def __init__(self, spec):
        self.rules = []
        aliases = dict(spec.get("path_aliases", {}))
        aliases[str(Path(spec["repo"]))] = "<REPO>"
        for raw, placeholder in aliases.items():
            if not raw or not re.fullmatch(r"<[A-Z][A-Z0-9_]*>", placeholder):
                raise snap.AuditError("INVALID_PATH_ALIAS")
            for variant in {raw, raw.replace("\\", "/"), raw.replace("\\", "\\\\")}:
                self.rules.append((variant, placeholder))
        self.rules.sort(key=lambda pair: len(pair[0]), reverse=True)
        self.secrets = spec.get("secret_values", [])
        if any(not isinstance(x, str) or len(x) < 4 for x in self.secrets):
            raise snap.AuditError("SECRET_VALUE_TOO_SHORT")

    def text(self, value, strict=False):
        for raw, replacement in self.rules:
            value = re.sub(re.escape(raw), lambda _: replacement, value, flags=re.IGNORECASE)
        for secret in sorted(self.secrets, key=len, reverse=True):
            value = value.replace(secret, "<REDACTED>")
        # Quoted assignments in source/log text; JSON uses the structural path below.
        assignment = r'''(?i)((?:["']?)(?:password|passwd|secret|token|api[_-]?key|authorization|connection[_-]?string)["']?\s*[:=]\s*)(["'])([^\r\n]*?)\2'''
        value = re.sub(assignment, lambda m: m[1] + m[2] + "<REDACTED>" + m[2], value)
        if strict:
            if re.search(r'''(?i)["'](?:profile|config|environment|env|values|legacy_passthrough)["']\s*:\s*[\[{]''', value):
                raise snap.AuditError("PRIVATE_STRUCTURE_IN_TEXT_USE_JSON_PROJECTION")
            value = re.sub(r"(?i)((?:--)?(?:password|passwd|secret|token|api[_-]?key|_authToken)\s*(?:=|:|\s)\s*)([^\s\"'<>]+)",
                           lambda m: m[1] + "<REDACTED>", value)
        return value

    def value(self, value):
        if isinstance(value, dict):
            out = {}
            for key, child in value.items():
                new_key = self.text(key)
                if new_key in out:
                    raise snap.AuditError("REDACTION_KEY_COLLISION")
                out[new_key] = "<REDACTED>" if sensitive(key) else self.value(child)
            return out
        if isinstance(value, list):
            out = []
            hide_next = False
            for item in value:
                out.append("<REDACTED>" if hide_next else self.value(item))
                hide_next = isinstance(item, str) and (sensitive(item.lstrip("-")) or item == "-s")
            return out
        if isinstance(value, str):
            if value.lstrip().startswith(("{", "[")):
                try:
                    return json.dumps(self.value(json.loads(value)), ensure_ascii=True)
                except json.JSONDecodeError:
                    pass
            return self.text(value, strict=True)
        return value

    def check(self, data):
        text = data.decode("utf-8")
        # Public URLs are not local absolute paths. Secret patterns still apply to them.
        without_urls = re.sub(r"https?://[^\s\"<>]+", "<URL>", text)
        if ABSOLUTE.search(without_urls) or SECRET_LITERAL.search(text):
            raise snap.AuditError("UNREDACTED_PATH_OR_SECRET")
        if any(secret in text for secret in self.secrets):
            raise snap.AuditError("UNREDACTED_SECRET_VALUE")

    def encode(self, value):
        data = snap.encoded(self.value(value))
        self.check(data)
        return data


def evidence_path(repo, name):
    rel = snap.relative_name(name)
    if (any(x.lower() in FORBIDDEN_PARTS or x.lower().startswith("maafw-sdk") for x in rel.parts)
            or rel.name.lower() in snap.PRIVATE_NAMES or rel.name.startswith(".env")
            or rel.suffix.lower() not in {".json", ".jsonl", ".log", ".txt", ".md", ".hpp"}):
        raise snap.AuditError("FORBIDDEN_EVIDENCE_FILE")
    return snap.controlled(repo, name, "next/.local", must_exist=False)


def case_summary(item, values, artifacts):
    execution = values.get(item.get("execution"))
    result = values.get(item.get("result"))
    execution = execution if isinstance(execution, dict) else {}
    result = result if isinstance(result, dict) else {}
    matching = [a["id"] for a in artifacts if a["sha256"] == execution.get("exe_sha256")]
    states = {}
    for key in ("snapshot", "source", "next"):
        value = result.get(key)
        if isinstance(value, dict):
            states[key] = {k: value.get(k) for k in ("state", "reason", "quiescent", "result_saved", "storage_error", "generation", "completed_business_units", "inputs")}
    return {"id": item["id"], "batch": item.get("batch"),
            "reported_status": item.get("reported_status", "UNVERIFIED"),
            "report_evidence": item.get("report"), "execution_evidence": item.get("execution"),
            "result_evidence": item.get("result"), "exit": execution.get("exit"),
            "observed": {k: result.get(k) for k in ("outcome", "error", "publish_error", "backend_calls", "connections", "real_inputs", "real_connections", "mismatch", "workflow_executed", "execution_available")},
            "snapshots": states, "matching_artifact_ids": matching,
            "execution_identity": "EXE_HASH_MATCH" if matching else "UNVERIFIED",
            "historical_source_binding": "UNVERIFIED",
            "coverage": "RESULT_PRESENT" if result else "UNVERIFIED",
            "note": "Reported test status and observed business state are separate; no PASS is inferred."}


class Collection:
    def __init__(self, spec):
        self.spec = spec
        self.repo = Path(spec["repo"])
        self.redactor = Redactor(spec)
        self.items, self.records, self.watched = {}, [], {}
        self.values, self.issues = {}, []

    def read(self, path):
        raw = snap.read_bytes(path)
        if path in self.watched and self.watched[path] != snap.digest(raw):
            raise snap.AuditError("INPUT_CHANGED_DURING_COLLECTION")
        self.watched[path] = snap.digest(raw)
        return raw

    def add(self, name, raw, data, origin, transform):
        snap.relative_name(name)
        if name.casefold() in {n.casefold() for n in self.items} or name.casefold() == "index.json":
            raise snap.AuditError("ZIP_MEMBER_COLLISION")
        self.redactor.check(name.encode())
        self.redactor.check(data)
        self.items[name] = data
        self.records.append({"path": name, "origin": origin, "raw_sha256": snap.digest(raw) if raw is not None else None,
                             "redacted_sha256": snap.digest(data), "bytes": len(data), "transformation": transform})

    def generated(self, name, value):
        self.add(name, None, self.redactor.encode(value), "derived", "structured_projection")

    def evidence(self):
        seen = set()
        for item in self.spec.get("evidence", []):
            ident = item["id"]
            if ident in seen:
                raise snap.AuditError("DUPLICATE_EVIDENCE_ID")
            seen.add(ident)
            path = evidence_path(self.repo, item["path"])
            if not path.is_file():
                self.issues.append({"id": ident, "status": "UNVERIFIED", "reason": "MISSING_EVIDENCE"})
                continue
            raw = self.read(path)
            if item.get("sha256") and snap.digest(raw) != item["sha256"]:
                raise snap.AuditError("EVIDENCE_HASH_MISMATCH")
            fmt = item["format"]
            try:
                text = raw.decode("utf-8-sig")
                if fmt == "json":
                    value = json.loads(text)
                    self.values[ident] = self.redactor.value(value)
                    data = self.redactor.encode(value)
                elif fmt == "jsonl":
                    values = [json.loads(line) for line in text.splitlines() if line.strip()]
                    data = b"".join((json.dumps(self.redactor.value(v), ensure_ascii=True, sort_keys=True) + "\n").encode() for v in values)
                elif fmt == "text":
                    data = self.redactor.text(text, strict=True).encode()
                else:
                    raise snap.AuditError("UNSUPPORTED_EVIDENCE_FORMAT")
            except (UnicodeError, json.JSONDecodeError):
                self.issues.append({"id": ident, "status": "UNVERIFIED", "reason": "INVALID_ENCODING_OR_JSON", "raw_sha256": snap.digest(raw)})
                continue
            self.add("evidence/" + str(snap.relative_name(item["member"])), raw, data, ident, "redacted_" + fmt)

    def source(self, snapshot_path):
        raw = self.read(snap.controlled(self.repo, snapshot_path, "next/.local"))
        snapshot = json.loads(raw)
        snap.check_source(self.repo, snapshot)
        self.add("identity/source.json", raw, self.redactor.encode(snapshot), "source_snapshot", "redacted_json")
        for row in snapshot["sources"]:
            # Full identity, delta payload; locks/build descriptions remain directly available.
            name = row["path"]
            if row["change"] == "unchanged" and not (name.endswith("lock.json") or Path(name).name in ("CMakeLists.txt", "CMakePresets.json")):
                continue
            raw = self.read(snap.controlled(self.repo, name))
            if snap.digest(raw) != row["raw_sha256"]:
                raise snap.AuditError("SOURCE_HASH_MISMATCH")
            text = raw.decode("utf-8-sig")
            data = self.redactor.encode(json.loads(text)) if name.endswith(".json") else self.redactor.text(text).encode()
            self.add("source/" + name, raw, data, name, "redacted_source")
        unknown = [x for x in snapshot["excluded"] if x["reason"] == "unclassified"]
        if unknown:
            self.issues.append({"status": "UNVERIFIED", "reason": "UNCLASSIFIED_SOURCE_FILES", "files": unknown})
        return snapshot

    def artifact_identity(self):
        name = self.spec.get("artifact_snapshot")
        if not name:
            self.issues.append({"status": "UNVERIFIED", "reason": "NO_CURRENT_ARTIFACT_SNAPSHOT"})
            return []
        raw = self.read(snap.controlled(self.repo, name, "next/.local"))
        value = json.loads(raw)
        if value.get("kind") != "artifact_snapshot":
            raise snap.AuditError("ARTIFACT_SNAPSHOT_INVALID")
        for item in [*value["artifacts"], value["generated_header"]]:
            path = snap.controlled(self.repo, item["path"], "next/build")
            if snap.digest(self.read(path)) != item["sha256"]:
                raise snap.AuditError("CURRENT_ARTIFACT_CHANGED")
        self.add("identity/artifacts.json", raw, self.redactor.encode(value), "artifact_snapshot", "redacted_json")
        return value["artifacts"]

    def protection(self):
        summary = {"status": "UNVERIFIED", "note": "Existing records only; protected user files are never opened."}
        for phase, name in self.spec.get("protection", {}).items():
            if phase not in ("before", "after"):
                raise snap.AuditError("PROTECTION_PHASE_INVALID")
            path = evidence_path(self.repo, name)
            if not path.is_file():
                summary[phase] = {"status": "UNVERIFIED"}
                continue
            raw = self.read(path)
            value = json.loads(raw)
            summary[phase] = {"raw_sha256": snap.digest(raw), "count": len(value) if isinstance(value, list) else value.get("count"),
                              "reported": {k: value.get(k) for k in ("changed", "production_writes")} if isinstance(value, dict) else None}
        self.generated("protection-summary.json", summary)

    def finish(self, snapshot):
        snap.check_source(self.repo, snapshot)
        for path, sha in self.watched.items():
            if snap.digest(snap.read_bytes(path)) != sha:
                raise snap.AuditError("INPUT_CHANGED_AFTER_COLLECTION")


def collect(spec, snapshot_path):
    collection = Collection(spec)
    snapshot = collection.source(snapshot_path)
    artifacts = collection.artifact_identity()
    collection.evidence()
    collection.protection()
    cases, ids = [], set()
    for item in spec.get("cases", []):
        if item["id"] in ids:
            raise snap.AuditError("DUPLICATE_CASE_ID")
        ids.add(item["id"])
        cases.append(case_summary(item, collection.values, artifacts))
    collection.generated("cases.json", cases)
    collection.generated("failures-unverified.json", {"issues": collection.issues,
                         "cases": [c for c in cases if c["reported_status"] != "PASS" or c["coverage"] == "UNVERIFIED" or c["historical_source_binding"] == "UNVERIFIED"]})
    collection.generated("commands.json", spec.get("commands", []))
    collection.generated("scope.json", {"baseline": snap.BASELINE, "case_count": len(cases),
                         "source_inventory_sha256": snapshot["inventory_sha256"],
                         "native_executed_by_exporter": False, "production_go": False,
                         "note": "Failure and UNVERIFIED evidence are intentional. Source binding is not retroactive. Assets and private inputs are excluded."})
    collection.finish(snapshot)
    return collection, snapshot


def verify(archive, receipt):
    raw = snap.read_bytes(archive)
    if snap.digest(raw) != receipt["archive_sha256"]:
        raise snap.AuditError("ZIP_HASH_MISMATCH")
    with zipfile.ZipFile(archive) as z:
        names = z.namelist()
        if len(names) != len(set(n.casefold() for n in names)):
            raise snap.AuditError("ZIP_DUPLICATE_MEMBER")
        for name in names:
            snap.relative_name(name)
        index_bytes = z.read("INDEX.json")
        if snap.digest(index_bytes) != receipt["index_sha256"]:
            raise snap.AuditError("INDEX_HASH_MISMATCH")
        rows = json.loads(index_bytes)["files"]
        if len(rows) != len({r["path"] for r in rows}) or set(names) != {"INDEX.json", *(r["path"] for r in rows)}:
            raise snap.AuditError("INDEX_MEMBERSHIP_MISMATCH")
        for row in rows:
            data = z.read(row["path"])
            if len(data) != row["bytes"] or snap.digest(data) != row["redacted_sha256"]:
                raise snap.AuditError("MEMBER_HASH_MISMATCH")
    return {"status": "ARCHIVE_INTEGRITY_OK", "members": len(names)}


def pack(spec, freeze, output):
    collection, snapshot = collect(spec, freeze)
    repo = Path(spec["repo"])
    archive = snap.controlled(repo, output, "next/.local", must_exist=False)
    if archive.suffix != ".zip" or archive.exists() or archive.with_suffix(".receipt.json").exists():
        raise snap.AuditError("OUTPUT_MUST_BE_NEW_ZIP")
    index = snap.encoded({"schema": 1, "files": sorted(collection.records, key=lambda x: x["path"])})
    collection.redactor.check(index)
    archive.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive, "x", compression=zipfile.ZIP_DEFLATED) as z:
        for name, data in sorted({**collection.items, "INDEX.json": index}.items()):
            info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            z.writestr(info, data)
    collection.finish(snapshot)
    receipt = {"schema": 1, "archive": archive.name, "archive_sha256": snap.digest(snap.read_bytes(archive)),
               "index_sha256": snap.digest(index), "bytes": archive.stat().st_size}
    verify(archive, receipt)
    archive.with_suffix(".receipt.json").write_bytes(snap.encoded(receipt))
    return receipt


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("check", "pack", "verify"))
    parser.add_argument("--spec", help="private JSON path or - for stdin")
    parser.add_argument("--freeze", help="repo-relative final source.json; required for pack")
    parser.add_argument("--out", help="new repo-relative next/.local ZIP")
    parser.add_argument("--archive")
    parser.add_argument("--receipt")
    args = parser.parse_args(argv)
    try:
        if args.action == "verify":
            repo = Path(__file__).resolve().parents[2]
            result = verify(snap.controlled(repo, args.archive, "next/.local"),
                            snap.read_json(snap.controlled(repo, args.receipt, "next/.local")))
        else:
            if not args.spec:
                raise snap.AuditError("SPEC_REQUIRED")
            spec = snap.load_spec(args.spec)
            if args.action == "pack":
                if not args.freeze or not args.out:
                    raise snap.AuditError("FINAL_FREEZE_AND_OUTPUT_REQUIRED")
                result = pack(spec, args.freeze, args.out)
            else:
                collection, _ = collect(spec, args.freeze or spec["source_snapshot"])
                result = {"status": "SAFE_TO_PACKAGE_NOT_ACCEPTANCE", "files": len(collection.items), "issues": collection.issues}
        print(json.dumps(result, ensure_ascii=True))
        return 0
    except (snap.AuditError, OSError, ValueError, KeyError, TypeError, zipfile.BadZipFile) as exc:
        print(json.dumps({"error": type(exc).__name__, "detail": "Audit failed; no success receipt issued. Review private inputs."}), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
