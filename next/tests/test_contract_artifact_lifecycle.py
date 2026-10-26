"""Small real-file lifecycle proofs; never create the 65,536-row native fixture."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import run_contracts
import package_functional


class ContractArtifactLifecycle(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="wvd-artifact-lifecycle-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.next = self.root / "next"
        self.output = self.next / ".local/current"
        self.source = self.output / "submission-history/boundaries/real-constant"
        self.source.mkdir(parents=True)
        for request_id in ("history-0", "65536th"):
            name = hashlib.sha256(request_id.encode()).hexdigest() + ".json"
            (self.source / name).write_text(json.dumps({"schema": 1, "request_id": request_id,
                "intent": {"kind": "isolated"}, "receipt": {"state": "completed", "run_id": 7}}), encoding="utf-8")
        self.results = self.source.parent / "results.json"
        self.results.write_text(json.dumps([{"case": "real-constant-65535-65536-next-and-restart",
            "status": {"read_only": False, "committed_count": 2, "count_limit": 65536}}]), encoding="utf-8")
        self.receipt = {"source": {"product_inputs_sha256": "fixture-source"},
            "binaries": {"test_native_application": "fixture-binary"}, "data_root": str(self.output),
            "artifact_owner": {"id": "isolated-fixture", "output": str(self.output),
                "directory_identity": run_contracts.checked_artifact_path(self.output)},
            "passed": [["app", "test_native_application", "--submission-history"]]}
        self.trash = self.root / "trash"
        self.destination = self.trash / "current/submission-history/boundaries/real-constant"
        self.patch_root = patch.object(run_contracts, "ROOT", self.next)
        self.patch_trash = patch.object(run_contracts, "ARTIFACT_TRASH_ROOT", self.trash)
        self.patch_root.start(); self.addCleanup(self.patch_root.stop)
        self.patch_trash.start(); self.addCleanup(self.patch_trash.stop)

    def finish(self):
        run_contracts.finish_contract_artifacts(self.output, self.receipt)
        return self.receipt["artifact_lifecycle"]

    def test_success_moves_real_directory_preserves_evidence_and_restore_identity(self):
        identity = run_contracts.checked_artifact_path(self.source)
        before = {p.name: p.read_bytes() for p in self.source.iterdir()}
        result_bytes = self.results.read_bytes()
        # Unrelated owned/older outputs are outside the sole recognized source.
        sibling = self.next / ".local/old/sentinel"
        sibling.parent.mkdir(); sibling.write_text("untouched", encoding="utf-8")
        lifecycle = self.finish()
        self.assertTrue(lifecycle["complete"])
        self.assertFalse(self.source.exists())
        self.assertEqual(run_contracts.checked_artifact_path(self.destination), identity)
        self.assertEqual({p.name: p.read_bytes() for p in self.destination.iterdir()}, before)
        self.assertEqual(self.results.read_bytes(), result_bytes)
        self.assertEqual(sibling.read_text(encoding="utf-8"), "untouched")
        move = lifecycle["moves"][0]
        self.assertEqual(move["file_count"], 2)
        self.assertEqual(move["logical_bytes"], sum(map(len, before.values())))
        self.assertEqual(len(move["samples"]), 2)
        self.assertEqual(move["restore"], {"from": str(self.destination), "to": str(self.source)})
        self.assertEqual(json.loads((self.output / "artifact-lifecycle.json").read_text(encoding="utf-8")), lifecycle)
        # A documented restore is an actual same-volume rename, preserving FileID.
        self.destination.rename(self.source)
        self.assertEqual(run_contracts.checked_artifact_path(self.source), identity)

    def test_failed_case_retains_complete_first_scene(self):
        self.receipt["passed"] = []
        self.receipt["validation_error"] = "original-validation-error"
        lifecycle = self.finish()
        self.assertTrue(lifecycle["complete"])
        self.assertEqual(lifecycle["moves"], [])
        self.assertTrue(self.source.exists())
        self.assertFalse(self.trash.exists())
        self.assertEqual(self.receipt["validation_error"], "original-validation-error")

    def test_trash_conflict_retains_source_and_existing_destination(self):
        self.destination.mkdir(parents=True)
        sentinel = self.destination / "sentinel"
        sentinel.write_text("existing", encoding="utf-8")
        lifecycle = self.finish()
        self.assertFalse(lifecycle["complete"])
        self.assertIn("TRASH_CONFLICT", lifecycle["errors"][0])
        self.assertTrue(self.source.exists())
        self.assertEqual(sentinel.read_text(encoding="utf-8"), "existing")

    def test_unknown_member_or_missing_samples_stops_without_moving(self):
        (self.source / "diagnostic.tmp").write_text("unowned", encoding="utf-8")
        lifecycle = self.finish()
        self.assertFalse(lifecycle["complete"])
        self.assertIn("UNEXPECTED_MEMBER", lifecycle["errors"][0])
        self.assertTrue(self.source.exists())
        self.assertFalse(self.trash.exists())

    def test_measurement_mismatch_retains_source(self):
        data = json.loads(self.results.read_text(encoding="utf-8"))
        data[0]["status"]["committed_count"] = 3
        self.results.write_text(json.dumps(data), encoding="utf-8")
        lifecycle = self.finish()
        self.assertIn("MEASUREMENT_MISMATCH", lifecycle["errors"][0])
        self.assertTrue(self.source.exists())

    def test_rename_failure_is_separate_from_original_validation_error(self):
        self.receipt["validation_error"] = "original-validation-error"
        # Only fault injection is mocked; successful rename uses the actual filesystem.
        with patch.object(Path, "rename", side_effect=OSError("injected rename failure")):
            lifecycle = self.finish()
        self.assertFalse(lifecycle["complete"])
        self.assertEqual(self.receipt["validation_error"], "original-validation-error")
        self.assertEqual(lifecycle["errors"], ["injected rename failure"])
        self.assertTrue(self.source.exists())
        self.assertEqual(lifecycle["moves"][0]["state"], "prepared")
        self.assertEqual(lifecycle["moves"][0]["residual_path"], str(self.source))

    def test_prepared_journal_survives_postmove_replace_failure(self):
        replace = os.replace
        writes = 0
        def fail_postwrite(source, destination):
            nonlocal writes
            if Path(destination).name == "artifact-lifecycle.json":
                writes += 1
                if writes == 2:
                    raise OSError("injected postmove replacement failure")
            return replace(source, destination)
        with patch.object(os, "replace", side_effect=fail_postwrite):
            with self.assertRaisesRegex(OSError, "postmove replacement failure"):
                self.finish()
        saved = json.loads((self.output / "artifact-lifecycle.json").read_text(encoding="utf-8"))
        self.assertEqual(saved["moves"][0]["state"], "prepared")
        self.assertFalse(self.source.exists())
        self.assertTrue(self.destination.exists())
        self.assertEqual(list(self.output.glob("*.tmp")), [])

    def test_prepared_journal_write_failure_prevents_move(self):
        replace = os.replace
        def fail_journal(source, destination):
            if Path(destination).name == "artifact-lifecycle.json":
                raise OSError("injected prepared replacement failure")
            return replace(source, destination)
        with patch.object(os, "replace", side_effect=fail_journal):
            with self.assertRaisesRegex(OSError, "prepared replacement failure"):
                self.finish()
        self.assertTrue(self.source.exists())
        self.assertFalse(self.destination.exists())
        self.assertEqual(list(self.output.glob("*.tmp")), [])

    def test_enumeration_refuses_the_next_entry_at_small_explicit_helper_limit(self):
        with self.assertRaisesRegex(RuntimeError, "FILE_COUNT_LIMIT"):
            run_contracts.measure_contract_artifacts(self.source, max_files=1)
        self.assertTrue(self.source.exists())

    def test_total_byte_limit_uses_bounded_real_reads(self):
        import builtins
        real_open = builtins.open
        requested = []
        class ReadProbe:
            def __init__(self, stream): self.stream = stream
            def __enter__(self): return self
            def __exit__(self, *args): self.stream.close()
            def read(self, size):
                requested.append(size)
                return self.stream.read(size)
        def probe(*args, **kwargs): return ReadProbe(real_open(*args, **kwargs))
        with patch.object(builtins, "open", side_effect=probe):
            with self.assertRaisesRegex(RuntimeError, "BYTE_LIMIT"):
                run_contracts.measure_contract_artifacts(self.source, max_total_bytes=1)
        self.assertEqual(requested, [2])
        self.assertTrue(self.source.exists())

    def test_atomic_replace_failure_keeps_previous_json_readable(self):
        target = self.output / "retained.json"
        run_contracts.atomic_artifact_json(target, {"state": "prepared"})
        with patch.object(os, "replace", side_effect=OSError("injected replacement failure")):
            with self.assertRaisesRegex(OSError, "replacement failure"):
                run_contracts.atomic_artifact_json(target, {"state": "moved"})
        self.assertEqual(json.loads(target.read_text(encoding="utf-8")), {"state": "prepared"})
        self.assertEqual(list(self.output.glob("*.tmp")), [])

    def test_execute_reports_cleanup_storage_failure(self):
        self.check_execute_cleanup_failure(change_source=False)

    def test_execute_preserves_real_source_change_error_when_cleanup_also_fails(self):
        self.check_execute_cleanup_failure(change_source=True)

    def test_execute_keeps_original_error_when_receipt_path_becomes_unwritable(self):
        self.check_execute_cleanup_failure(change_source=True, receipt_fault=True)

    def test_build_error_is_preserved_when_build_receipt_cannot_replace(self):
        self.check_execute_cleanup_failure(change_source=False, build_fault=True)

    def check_execute_cleanup_failure(self, change_source, receipt_fault=False, build_fault=False):
        native = self.next / "native"
        native.mkdir()
        (native / "CMakeLists.txt").write_text("add_executable(test_native_flow source.cpp)" if build_fault
                                                else "# isolated no-target contract", encoding="utf-8")
        consumed = native / "input.cpp"
        consumed.write_text("original", encoding="utf-8")
        def git(*args):
            subprocess.run(["git", *args], cwd=self.root, check=True,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        (self.root / ".gitignore").write_text("next/.local/\n", encoding="utf-8")
        git("init"); git("config", "user.name", "fixture")
        git("config", "user.email", "fixture@example.invalid")
        git("add", "next/native"); git("commit", "-m", "isolated inputs")
        output = self.next / ".local/execute"
        finish = run_contracts.finish_contract_artifacts
        identity = package_functional.source_identity
        calls = 0
        def actual_identity_with_external_input_change():
            nonlocal calls
            calls += 1
            if change_source and calls == 2:
                consumed.write_text("externally changed", encoding="utf-8")
                if receipt_fault:
                    (output / "receipt.json").unlink()
                    (output / "receipt.json").mkdir()
            return identity()
        def actual_finish_with_storage_fault(path, receipt):
            # Simulate an external filesystem conflict, then call the real finalizer.
            (path / "artifact-lifecycle.json").mkdir()
            return finish(path, receipt)
        def external_build_failure(*args, **kwargs):
            (output / "receipt.json").mkdir()
            raise RuntimeError("original-builderror")
        with patch.object(run_contracts, "run", side_effect=external_build_failure), \
             patch.object(run_contracts, "cmake_path", return_value="cmake"), \
             patch.object(package_functional, "ROOT", self.next), \
             patch.object(run_contracts, "source_identity", side_effect=actual_identity_with_external_input_change), \
             patch.object(run_contracts, "finish_contract_artifacts", side_effect=actual_finish_with_storage_fault):
            expected = ("original-builderror" if build_fault else "CONTRACT_SOURCE_CHANGED_DURING_BUILD"
                        if change_source else "CONTRACT_ARTIFACT_CLEANUP_FAILED")
            with self.assertRaisesRegex(RuntimeError, expected):
                run_contracts.execute({"runtime"} if build_fault else set(), output)
        if receipt_fault or build_fault:
            self.assertTrue((output / "receipt.json").is_dir())
            self.assertFalse((output / "artifact-lifecycle.json").exists())
            return
        receipt = json.loads((output / "receipt.json").read_text(encoding="utf-8"))
        self.assertIn("artifact_cleanup_error", receipt)
        if change_source:
            self.assertEqual(receipt["validation_error"], "CONTRACT_SOURCE_CHANGED_DURING_BUILD")
            self.assertFalse(receipt["complete"])
        else:
            self.assertTrue(receipt["validation_complete"])
            self.assertFalse(receipt["complete"])
            self.assertNotIn("validation_error", receipt)

    def test_reparse_source_ancestor_is_rejected(self):
        original = self.output / "submission-history"
        relocated = self.root / "relocated"
        original.rename(relocated)
        if os.name == "nt":
            subprocess.run(["cmd", "/c", "mklink", "/J", str(original), str(relocated)],
                           check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        else:
            original.symlink_to(relocated, target_is_directory=True)
        lifecycle = self.finish()
        self.assertFalse(lifecycle["complete"])
        self.assertIn("REPARSE", lifecycle["errors"][0])
        self.assertTrue(self.source.exists())
        self.assertFalse(self.trash.exists())


if __name__ == "__main__":
    unittest.main()
