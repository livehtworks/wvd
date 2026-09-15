"""Standard-library audit tests. Only disposable tempdirs; no Git/native/device calls."""

import hashlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

TOOLS = Path(__file__).resolve().parents[1] / "tools"


def load(name):
    spec = importlib.util.spec_from_file_location(name, TOOLS / (name + ".py"))
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


snap = load("audit_snapshot")
package = load("audit_package")


class AuditDeliveryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="wvd-audit-test-")
        self.addCleanup(self.temp.cleanup)
        self.repo = Path(self.temp.name).resolve()
        self.put("next/native/a.cpp", b"int a = 1;\n")
        self.put("next/native/new.hpp", b"#pragma once\n")
        self.put("next/tools/example.py", b"print('example')\n")
        self.put("next/docs/readme.md", b"# Audit example\n")
        self.put("next/dependencies.lock.json", b'{"toolchain":"fixed"}\n')
        self.put("next/native/maafw/CMakeLists.txt", b"# fixture\n")
        self.baseline = {"next/native/a.cpp": hashlib.sha256(b"int a = 1;\n").hexdigest(),
                         "next/native/removed.cpp": hashlib.sha256(b"old").hexdigest()}
        self.snapshot = snap.source_snapshot(self.repo, self.baseline, {"next/native/a.cpp"})
        self.put_json("next/.local/before/source.json", self.snapshot)
        self.spec = {"schema": 1, "repo": str(self.repo), "baseline": snap.BASELINE,
                     "source_snapshot": "next/.local/before/source.json",
                     "generated_build_id": "next/build/generated/core_build_id.hpp",
                     "artifacts": [{"id": "fixture", "path": "next/build/fake.exe"}],
                     "evidence": [], "cases": [], "commands": []}
        self.put("next/build/fake.exe", b"not executable; test bytes only")
        self.put("next/build/generated/core_build_id.hpp",
                 ('#pragma once\n#define WVD_CORE_BUILD_ID "' + self.snapshot["current_core_source_id"] + '"\n').encode())

    def put(self, name, data):
        path = self.repo / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return path

    def put_json(self, name, value):
        return self.put(name, json.dumps(value).encode())

    def add_evidence(self, ident, value, fmt="json"):
        name = "next/.local/evidence/" + ident + (".json" if fmt == "json" else ".log")
        self.put(name, json.dumps(value).encode() if fmt == "json" else value)
        self.spec["evidence"].append({"id": ident, "path": name, "member": ident + "." + fmt, "format": fmt})

    def test_full_inventory_not_just_delta_and_untracked_included(self):
        rows = {x["path"]: x for x in self.snapshot["sources"]}
        self.assertEqual(len(rows), 6)
        self.assertEqual(rows["next/native/a.cpp"]["change"], "unchanged")
        self.assertFalse(rows["next/native/new.hpp"]["tracked"])
        self.assertEqual(self.snapshot["deleted"], ["next/native/removed.cpp"])
        expected = hashlib.sha256(b"#pragma once\n").hexdigest()
        self.assertEqual(rows["next/native/new.hpp"]["raw_sha256"], expected)

    def test_core_id_uses_cmake_scope_not_docs_or_python(self):
        self.put("next/docs/readme.md", b"changed docs")
        self.put("next/tools/example.py", b"changed script")
        current = snap.source_snapshot(self.repo, self.baseline, set())
        self.assertEqual(current["current_core_source_id"], self.snapshot["current_core_source_id"])
        self.assertNotEqual(current["inventory_sha256"], self.snapshot["inventory_sha256"])

    def test_excluded_roots_are_not_walked(self):
        for name in (".local", "build", ".vscode", "web/node_modules", "mod"):
            self.put("next/" + name + "/must-not-read.cpp", b"\xff")
        rows, _ = snap.inventory(self.repo)
        self.assertEqual(len(rows), 6)

    def test_new_deleted_or_modified_source_invalidates_freeze(self):
        for action in ("new", "modified", "deleted"):
            with self.subTest(action=action):
                path = self.repo / "next/native/a.cpp"
                original = path.read_bytes()
                extra = self.repo / "next/native/later.hpp"
                if action == "new":
                    extra.write_bytes(b"new")
                elif action == "modified":
                    path.write_bytes(b"different")
                else:
                    path.unlink()
                with self.assertRaises(snap.AuditError):
                    snap.check_source(self.repo, self.snapshot)
                path.write_bytes(original)
                if extra.exists():
                    extra.unlink()

    def test_unsafe_relative_paths(self):
        for value in ("../outside", "/absolute", "C:/Users/private", "next\\file", "next/a:stream",
                      "next/a/../b", "next//file", "next/CON.txt", "next/file.", "next/*.json"):
            with self.subTest(path=value), self.assertRaises(snap.AuditError):
                snap.relative_name(value)

    def test_path_root_is_enforced(self):
        with self.assertRaises(snap.AuditError):
            snap.controlled(self.repo, "next/native/a.cpp", "next/.local")

    def test_link_rejection_without_following_target(self):
        # lstat metadata is the platform boundary; no link privilege is required.
        class LinkStat:
            st_mode = 0o120777
            st_file_attributes = 0
        with patch.object(Path, "lstat", return_value=LinkStat()):
            with self.assertRaises(snap.AuditError):
                snap.no_link(self.repo / "link")

    def test_windows_junction_rejection(self):
        class JunctionStat:
            st_mode = 0o040755
            st_file_attributes = 0x400
        with patch.object(Path, "lstat", return_value=JunctionStat()):
            with self.assertRaises(snap.AuditError):
                snap.no_link(self.repo / "junction")

    def test_stdin_spec(self):
        with patch.object(sys, "stdin", io.StringIO(json.dumps(self.spec))):
            self.assertEqual(snap.load_spec("-")["baseline"], snap.BASELINE)

    def test_private_spec_and_baseline_validation(self):
        p = self.put_json("next/.local/spec.json", self.spec)
        self.assertEqual(snap.load_spec(str(p))["repo"], str(self.repo))
        bad = dict(self.spec, baseline="other")
        with patch.object(sys, "stdin", io.StringIO(json.dumps(bad))), self.assertRaises(snap.AuditError):
            snap.load_spec("-")

    def test_artifact_snapshot_does_not_execute_and_does_not_infer_binding(self):
        result = snap.artifacts_snapshot(self.spec, self.snapshot)
        self.assertEqual(result["artifacts"][0]["sha256"], hashlib.sha256(b"not executable; test bytes only").hexdigest())
        self.assertTrue(result["source_unchanged_since_before"])
        self.assertTrue(result["generated_matches_current_core_sources"])
        self.assertEqual(result["source_binding"], "UNVERIFIED")

    def test_changed_source_keeps_artifact_capture_unverified(self):
        self.put("next/native/a.cpp", b"later source")
        result = snap.artifacts_snapshot(self.spec, self.snapshot)
        self.assertFalse(result["source_unchanged_since_before"])
        self.assertFalse(result["generated_matches_current_core_sources"])
        self.assertEqual(result["source_binding"], "UNVERIFIED")

    def test_explicit_matching_build_record_is_only_recorded_match(self):
        record = {"source_inventory_sha256": self.snapshot["inventory_sha256"], "exit": 0,
                  "build_id": self.snapshot["current_core_source_id"],
                  "exe_sha256": {"fixture": hashlib.sha256(b"not executable; test bytes only").hexdigest()}}
        self.spec["build_record"] = "next/.local/build-record.json"
        self.put_json(self.spec["build_record"], record)
        result = snap.artifacts_snapshot(self.spec, self.snapshot)
        self.assertEqual(result["source_binding"], "RECORDED_BUILD_MATCH")

    def test_structural_redaction_and_escaped_paths(self):
        redactor = package.Redactor(dict(self.spec, path_aliases={"D:\\private": "<PRIVATE>"}))
        value = {"profile": {"unknown_key": "private value"}, "nested": {"accessToken": "hidden"},
                 "path": "D:\\private\\run", "serial": "private device", "state": "Failed"}
        clean = json.loads(redactor.encode(value))
        self.assertEqual(clean["profile"], "<REDACTED>")
        self.assertEqual(clean["nested"]["accessToken"], "<REDACTED>")
        self.assertEqual(clean["path"], "<PRIVATE>\\run")
        self.assertEqual(clean["state"], "Failed")
        self.assertIn("<PRIVATE>", redactor.text(r'D:\\private\\escaped'))

    def test_command_secrets_and_embedded_json_are_redacted(self):
        r = package.Redactor(self.spec)
        value = {"argv": ["tool", "--password", "hidden", "-s", "device"],
                 "stdout": json.dumps({"profile": {"key": "private"}})}
        clean = r.value(value)
        self.assertEqual(clean["argv"], ["tool", "--password", "<REDACTED>", "-s", "<REDACTED>"])
        self.assertNotIn("private", clean["stdout"])
        self.assertNotIn("hidden", r.text("--token hidden", strict=True))

    def test_unknown_absolute_path_or_secret_blocks(self):
        redactor = package.Redactor(self.spec)
        for value in (r"Z:\unknown\private", r"\\host\share", "/home/other/file", "ghp_" + "x" * 25,
                      "https://user:private@example.test/path"):
            with self.subTest(value=value), self.assertRaises(snap.AuditError):
                redactor.encode({"message": value})

    def test_private_python_repr_in_log_blocks(self):
        self.add_evidence("log", b"{'profile': {'unusual_secret': 'hidden'}}", "text")
        with self.assertRaises(snap.AuditError):
            package.collect(self.spec, self.spec["source_snapshot"])

    def test_no_recursive_evidence_discovery(self):
        self.add_evidence("selected", {"state": "Failed"})
        self.put("next/.local/evidence/unlisted.json", b'{"message":"Z:\\private"}')
        collection, _ = package.collect(self.spec, self.spec["source_snapshot"])
        self.assertEqual(set(collection.values), {"selected"})
        self.assertFalse(any("unlisted" in p for p in collection.items))

    def test_forbidden_originals_and_binary_payloads(self):
        for name in ("input.json", "config.json", "profile.json", "mod/result.json", "image/a.png", "fake.exe", "maafw-sdk/log.json"):
            with self.subTest(name=name), self.assertRaises(snap.AuditError):
                package.evidence_path(self.repo, "next/.local/" + name)

    def test_missing_invalid_and_failed_evidence_can_be_packaged(self):
        self.add_evidence("execution", {"exit": 1, "exe_sha256": "old"})
        self.add_evidence("failed", {"snapshot": {"state": "Failed", "quiescent": True}})
        self.add_evidence("broken", {})
        self.put(self.spec["evidence"][-1]["path"], b"not JSON")
        self.spec["evidence"].append({"id": "missing", "path": "next/.local/missing.json", "member": "missing.json", "format": "json"})
        self.spec["cases"] = [{"id": "failed", "execution": "execution", "result": "failed", "reported_status": "FAIL"}, {"id": "not-run"}]
        receipt = package.pack(self.spec, self.spec["source_snapshot"], "next/.local/review/test.zip")
        archive = self.repo / "next/.local/review/test.zip"
        self.assertEqual(package.verify(archive, receipt)["status"], "ARCHIVE_INTEGRITY_OK")
        with zipfile.ZipFile(archive) as z:
            cases = json.loads(z.read("cases.json"))
            self.assertEqual(cases[0]["reported_status"], "FAIL")
            self.assertEqual(cases[0]["exit"], 1)
            self.assertEqual(cases[1]["coverage"], "UNVERIFIED")
            self.assertFalse(any(name.endswith((".exe", ".png")) for name in z.namelist()))

    def test_zero_exit_never_creates_pass(self):
        row = package.case_summary({"id": "case", "execution": "e", "result": "r"},
                                   {"e": {"exit": 0}, "r": {"snapshot": {"state": "Failed"}}}, [])
        self.assertEqual(row["reported_status"], "UNVERIFIED")
        self.assertEqual(row["snapshots"]["snapshot"]["state"], "Failed")

    def test_pinned_evidence_hash_mismatch_blocks(self):
        self.add_evidence("result", {"state": "Failed"})
        self.spec["evidence"][0]["sha256"] = "0" * 64
        with self.assertRaises(snap.AuditError):
            package.collect(self.spec, self.spec["source_snapshot"])

    def test_current_artifact_tampering_blocks(self):
        artifact = snap.artifacts_snapshot(self.spec, self.snapshot)
        self.spec["artifact_snapshot"] = "next/.local/artifacts.json"
        self.put_json(self.spec["artifact_snapshot"], artifact)
        self.put("next/build/fake.exe", b"changed artifact")
        with self.assertRaises(snap.AuditError):
            package.collect(self.spec, self.spec["source_snapshot"])

    def test_raw_and_redacted_hashes_differ(self):
        self.add_evidence("result", {"profile": {"password": "private"}, "state": "Failed"})
        collection, _ = package.collect(self.spec, self.spec["source_snapshot"])
        row = next(x for x in collection.records if x["origin"] == "result")
        self.assertNotEqual(row["raw_sha256"], row["redacted_sha256"])
        self.assertNotIn(b"private", collection.items[row["path"]])

    def test_jsonl_remains_one_object_per_line(self):
        self.add_evidence("lines", b'{"state":"Failed"}\n{"token":"hidden"}\n', "jsonl")
        collection, _ = package.collect(self.spec, self.spec["source_snapshot"])
        lines = collection.items["evidence/lines.jsonl"].splitlines()
        self.assertEqual(len(lines), 2)
        self.assertEqual(json.loads(lines[1])["token"], "<REDACTED>")

    def test_zip_hash_and_index_hash_are_checked(self):
        receipt = package.pack(self.spec, self.spec["source_snapshot"], "next/.local/review/test.zip")
        archive = self.repo / "next/.local/review/test.zip"
        with self.assertRaises(snap.AuditError):
            package.verify(archive, dict(receipt, index_sha256="0" * 64))
        archive.write_bytes(archive.read_bytes() + b"tampered")
        with self.assertRaises(snap.AuditError):
            package.verify(archive, receipt)

    def test_zip_paths_and_case_collisions_block(self):
        collection = package.Collection(self.spec)
        collection.add("evidence/Result.json", b"{}", b"{}", "a", "none")
        with self.assertRaises(snap.AuditError):
            collection.add("evidence/result.json", b"{}", b"{}", "b", "none")
        with self.assertRaises(snap.AuditError):
            collection.add("../escape", b"{}", b"{}", "b", "none")

    def test_output_is_never_overwritten(self):
        snap.new_output(self.repo, "next/.local/new")
        with self.assertRaises(snap.AuditError):
            snap.new_output(self.repo, "next/.local/new")
        package.pack(self.spec, self.spec["source_snapshot"], "next/.local/review/test.zip")
        with self.assertRaises(snap.AuditError):
            package.pack(self.spec, self.spec["source_snapshot"], "next/.local/review/test.zip")

    def test_protection_reads_records_not_user_files(self):
        self.put_json("next/.local/before.json", [{"path": "outside/user/config.json", "sha256": "old"}])
        self.put_json("next/.local/after.json", {"count": 1, "changed": ["outside/user/config.json"]})
        self.spec["protection"] = {"before": "next/.local/before.json", "after": "next/.local/after.json"}
        collection, _ = package.collect(self.spec, self.spec["source_snapshot"])
        value = json.loads(collection.items["protection-summary.json"])
        self.assertEqual(value["status"], "UNVERIFIED")
        self.assertEqual(value["before"]["count"], 1)
        self.assertEqual(value["after"]["reported"]["changed"], ["outside/user/config.json"])


if __name__ == "__main__":
    unittest.main()
