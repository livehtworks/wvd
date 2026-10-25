"""Exercise the actual build/resource helpers only in disposable directories."""

import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile
import subprocess
import os
import uuid
import ctypes
import getpass
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from dependency_tree import verify_consumed_tree
from resource_generation import generate_transaction, validate_pack
import package_functional
import build
import run_contracts
import resource_generation


class BuildResourceContract(unittest.TestCase):
    def test_module_selection_includes_untracked_staged_and_dependency_inputs(self):
        with tempfile.TemporaryDirectory(prefix="wvd-module-proof-") as directory:
            root = Path(directory)
            def git(*arguments):
                return subprocess.run(["git", *arguments], cwd=root, check=True, timeout=10,
                                      stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            git("init")
            git("config", "user.name", "isolated-fixture")
            git("config", "user.email", "fixture@example.invalid")
            (root / "README.md").write_text("baseline", encoding="utf-8")
            git("add", ".")
            git("commit", "-m", "isolated baseline")
            (root / "next/native/runtime").mkdir(parents=True)
            added = root / "next/native/runtime/added.hpp"
            added.write_text("// new consumed source", encoding="utf-8")
            (root / ".vscode").mkdir()
            (root / ".vscode/user.json").write_text("{}", encoding="utf-8")
            with patch.object(run_contracts, "ROOT", root / "next"):
                paths = run_contracts.changed_paths("HEAD")
                self.assertEqual(paths, ["next/native/runtime/added.hpp"])
                self.assertEqual(run_contracts.select_modules(paths), {"runtime", "app"})
                git("add", "next")
                self.assertEqual(run_contracts.changed_paths("HEAD"), paths)
            self.assertEqual(run_contracts.select_modules(["next/tools/prepare_native.py"]), set(run_contracts.CASES))
            self.assertEqual(run_contracts.select_modules(["next/tests/tools/trace.ps1"]), {"tools"})

    def test_documentation_is_provenance_not_product_input(self):
        with tempfile.TemporaryDirectory(prefix="wvd-source-proof-") as directory:
            root = Path(directory)
            def git(*arguments):
                return subprocess.run(["git", *arguments], cwd=root, check=True, timeout=10,
                                      stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            git("init")
            git("config", "user.name", "isolated-fixture")
            git("config", "user.email", "fixture@example.invalid")
            (root / "next/native").mkdir(parents=True)
            (root / "docs").mkdir()
            (root / "next/native/unit.cpp").write_text("int value=1;", encoding="utf-8")
            (root / "docs/report.md").write_text("old review", encoding="utf-8")
            git("add", ".")
            git("commit", "-m", "isolated source")
            with patch.object(package_functional, "ROOT", root / "next"):
                frozen = package_functional.source_identity()
                (root / "docs/report.md").write_text("new review", encoding="utf-8")
                git("add", ".")
                git("commit", "-m", "documentation only")
                self.assertTrue(package_functional.same_product_inputs(frozen))
                self.assertNotEqual(frozen["documentation_sha256"], package_functional.source_identity()["documentation_sha256"])
                (root / "next/native/unit.cpp").write_text("int value=2;", encoding="utf-8")
                self.assertFalse(package_functional.same_product_inputs(frozen))

    def test_build_command_has_owned_bounded_lifetime(self):
        with tempfile.TemporaryDirectory(prefix="wvd-build-owner-") as directory:
            isolated = Path(directory)
            with patch.object(build, "ROOT", isolated):
                # Only the trusted tool module is read from this repository.
                with patch.object(build, "__file__", str(Path(__file__).resolve())):
                    # build.run resolves its module relative to ROOT, so copy
                    # only these checked-in helpers into the disposable root.
                    tools = isolated / "tools"
                    tools.mkdir()
                    source = Path(__file__).resolve().parents[1] / "tools"
                    for name in ("memory_trace_support.psm1", "bounded_trace_output.cs", "owned_trace_process.cs"):
                        (tools / name).write_bytes((source / name).read_bytes())
                    build.run("short-owned", [sys.executable, "-c", "print('owned')"], cwd=isolated, timeout_seconds=5)
                    with self.assertRaisesRegex(RuntimeError, "exit="):
                        build.run("expired-owned", [sys.executable, "-c", "import time;time.sleep(60)"],
                                  cwd=isolated, timeout_seconds=1)
    def test_consumed_bytes_not_marker(self):
        with tempfile.TemporaryDirectory(prefix="wvd-dependency-") as directory:
            root = Path(directory)
            archive = root / "locked.zip"
            with zipfile.ZipFile(archive, "w") as output:
                output.writestr("include/header.h", "locked-header")
                output.writestr("lib/runtime.dll", "locked-runtime")
            expected = hashlib.sha256(archive.read_bytes()).hexdigest()
            target = root / "consumed"
            def extract(archive, destination):
                with zipfile.ZipFile(archive) as source:
                    source.extractall(destination)
            original = verify_consumed_tree(archive, expected, target, extract, "include/header.h")
            self.assertEqual(original, verify_consumed_tree(archive, expected, target, extract,
                                                           "include/header.h", readonly=True))
            for name, changed in (("lib/runtime.dll", b"stale-dll"), ("include/header.h", b"wrong-header"),
                                  ("lib/runtime.dll", None)):
                file = target / name
                before = file.read_bytes()
                (target / ".verified").write_text("ready", encoding="utf-8")
                if changed is None:
                    file.unlink()
                else:
                    file.write_bytes(changed)
                with self.assertRaisesRegex(RuntimeError, "CONSUMED_BYTES_CHANGED"):
                    verify_consumed_tree(archive, expected, target, extract, "include/header.h")
                self.assertEqual(file.read_bytes() if file.exists() else None, changed)
                file.write_bytes(before)
            incomplete = root / "incomplete"
            def interrupted(archive, destination):
                (destination / "marker").write_text("half", encoding="utf-8")
                raise RuntimeError("INTERRUPTED_EXTRACTION")
            with self.assertRaisesRegex(RuntimeError, "INTERRUPTED_EXTRACTION"):
                verify_consumed_tree(archive, expected, incomplete, interrupted, "marker")
            self.assertFalse(incomplete.exists())

    def test_resource_generation_recovers_interrupted_swap(self):
        for phase in ("prepared", "journal", "previous_saved", "published"):
            with self.subTest(phase=phase), tempfile.TemporaryDirectory(prefix="wvd-resource-") as directory:
                pack = Path(directory) / "packs/wvd"
                pack.mkdir(parents=True)
                def write(pack, text):
                    content = text.encode("utf-8")
                    (pack / "resource.json").write_bytes(content)
                    (pack / "manifest.json").write_text(json.dumps({"files": [{"path": "resource.json",
                        "bytes": len(content), "sha256": hashlib.sha256(content).hexdigest()}]}), encoding="utf-8")
                write(pack, "old")
                def fail(actual):
                    if actual == phase:
                        raise RuntimeError("INTERRUPTED_SWAP")
                with self.assertRaisesRegex(RuntimeError, "INTERRUPTED_SWAP"):
                    generate_transaction(pack, lambda target: write(target, "new"), fail)
                old_copies = [p for p in Path(directory).rglob("resource.json") if p.read_text(encoding="utf-8") == "old"]
                self.assertTrue(old_copies, "Old authority must survive every interruption")
                generate_transaction(pack, lambda target: write(target, "new"))
                validate_pack(pack)
                self.assertEqual((pack / "resource.json").read_text(encoding="utf-8"), "new")
                self.assertFalse((Path(directory) / ".local/resource-generation/wvd/transaction.json").exists())
                self.assertFalse(list((Path(directory) / ".local/resource-generation/wvd").glob("prepared-*")))

    def test_missing_generated_member_is_seeded_only_in_prepared_generation(self):
        with tempfile.TemporaryDirectory(prefix="wvd-seed-") as directory:
            pack = Path(directory) / "packs/wvd"
            pack.mkdir(parents=True)
            content = b"verified real source bytes"
            (pack / "manifest.json").write_text(json.dumps({"files": [{"path": "image.png",
                "bytes": len(content), "sha256": hashlib.sha256(content).hexdigest()}]}), encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "RESOURCE_MEMBER_MISSING"):
                generate_transaction(pack, lambda prepared: None)
            def seed(prepared):
                self.assertFalse((pack / "image.png").exists())
                (prepared / "image.png").write_bytes(content)
            generate_transaction(pack, lambda prepared: None, seed_missing=seed)
            validate_pack(pack)
            self.assertEqual((pack / "image.png").read_bytes(), content)
            (pack / "image.png").write_bytes(b"unexpected existing edits")
            with self.assertRaisesRegex(RuntimeError, "RESOURCE_MEMBER_CHANGED"):
                generate_transaction(pack, lambda prepared: None, seed_missing=seed)
            self.assertEqual((pack / "image.png").read_bytes(), b"unexpected existing edits")


class DirectoryFailureContract(unittest.TestCase):
    """Faults use the real transaction and filesystem in explicitly disposable roots."""

    def setUp(self):
        self.root = Path(__file__).resolve().parents[1] / ".local/t0-repair-261025" / ("wp04-" + uuid.uuid4().hex)
        self.root.mkdir(parents=True)
        self.pack = self.root / "packs/wvd"
        self.pack.mkdir(parents=True)
        self.write(self.pack, b"o" * 1024)
        self.before = self.snapshot()

    @staticmethod
    def write(pack, content):
        (pack / "resource.json").write_bytes(content)
        (pack / "manifest.json").write_text(json.dumps({"files": [{"path": "resource.json",
            "bytes": len(content), "sha256": hashlib.sha256(content).hexdigest()}]}), encoding="utf-8")

    @property
    def transaction_root(self):
        return self.root / ".local/resource-generation/wvd"

    def snapshot(self):
        info = self.pack.stat() if self.pack.exists() else None
        return {"directory_identity": [info.st_dev, info.st_ino] if info else None,
            "resource_sha256": hashlib.sha256((self.pack / "resource.json").read_bytes()).hexdigest()
                if (self.pack / "resource.json").is_file() else None,
            "manifest_sha256": hashlib.sha256((self.pack / "manifest.json").read_bytes()).hexdigest()
                if (self.pack / "manifest.json").is_file() else None}

    def proof(self, name, **fields):
        (self.root / "evidence.json").write_text(json.dumps({"case": name,
            "root": str(self.root), "before": self.before, "after": self.snapshot(),
            **fields}, indent=2), encoding="utf-8")

    def test_copy_seed_and_generate_failures_have_no_orphans(self):
        original = resource_generation.shutil.copytree
        for fault in ("copy", "seed", "generate"):
            for repeat in range(3):
                def copy(source, target, **kwargs):
                    original(source, target, **kwargs)
                    if fault == "copy": raise OSError("COPY_WRITE_FAILED")
                def seed(target):
                    if fault == "seed": raise OSError("SEED_FAILED")
                def generate(target):
                    if fault == "generate": raise OSError("GENERATE_FAILED")
                with patch.object(resource_generation.shutil, "copytree", copy), self.assertRaises(OSError):
                    generate_transaction(self.pack, generate, seed_missing=seed)
                self.assertEqual((self.pack / "resource.json").read_bytes(), b"o" * 1024)
                self.assertFalse(list(self.transaction_root.glob("prepared-*")))
                self.assertFalse((self.transaction_root / "transaction.json").exists())
        self.proof("copy/seed/generate: each repeated three times", owned_prepared=0)

    def test_process_exit_recovers_each_persistent_switch(self):
        module = str(Path(resource_generation.__file__).parent)
        for phase in ("journal", "prepared", "previous_saved", "published"):
            code = "\n".join([
                "import sys,os,hashlib,json", "from pathlib import Path",
                "sys.path.insert(0," + repr(module) + ")", "from resource_generation import generate_transaction",
                "p=Path(" + repr(str(self.pack)) + ")",
                "def write(p):", " b=b'n'*1024", " (p/'resource.json').write_bytes(b)",
                " (p/'manifest.json').write_text(json.dumps({'files':[{'path':'resource.json','bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()}]}),encoding='utf-8')",
                "def checkpoint(actual):", " if actual==" + repr(phase) + ":os._exit(73)",
                "generate_transaction(p,write,checkpoint)"])
            child = subprocess.run([sys.executable, "-B", "-c", code], capture_output=True, timeout=20)
            self.assertEqual(child.returncode, 73, child.stderr.decode(errors="replace"))
            self.assertTrue((self.transaction_root / "transaction.json").exists())
            generate_transaction(self.pack, lambda p: self.write(p, b"n" * 1024))
            validate_pack(self.pack)
            self.assertEqual((self.pack / "resource.json").read_bytes(), b"n" * 1024)
            self.assertFalse((self.transaction_root / "transaction.json").exists())
            self.assertFalse(list(self.transaction_root.glob("prepared-*")))
            self.write(self.pack, b"o" * 1024)
        self.proof("real child exits at four durable phases", recovered=True, orphan_prepared=0)

    def test_space_rejection_precedes_prepared_and_preserves_history(self):
        generate_transaction(self.pack, lambda p: self.write(p, b"n" * 1024))
        before = resource_generation._inventory([self.pack, *self.transaction_root.glob("previous-*")])
        old = (self.pack / "resource.json").read_bytes()
        with patch.object(resource_generation.shutil, "disk_usage", return_value=type("Disk", (), {"free": 1})()), \
                self.assertRaisesRegex(RuntimeError, "SPACE_LOW"):
            generate_transaction(self.pack, lambda p: self.write(p, b"x" * 1024))
        self.assertEqual((self.pack / "resource.json").read_bytes(), old)
        self.assertEqual(before, resource_generation._inventory([self.pack, *self.transaction_root.glob("previous-*")]))
        self.assertFalse(list(self.transaction_root.glob("prepared-*")))
        self.proof("low space before creation", before=before, after=before)

    def test_journal_flush_and_replace_failures_block_retries_without_prepared(self):
        for operation in ("fsync", "replace"):
            if operation == "replace":
                # A separate owned root avoids deliberately retained prior evidence.
                self.root = self.root.parent / ("wp04-" + uuid.uuid4().hex)
                self.pack = self.root / "packs/wvd"; self.pack.mkdir(parents=True)
                self.write(self.pack, b"o" * 1024)
                self.before = self.snapshot()
            with patch.object(resource_generation.os, operation, side_effect=OSError(operation + "_FAILED")), \
                    self.assertRaisesRegex(RuntimeError, "JOURNAL_WRITE_FAILED"):
                generate_transaction(self.pack, lambda p: self.write(p, b"n" * 1024))
            self.assertFalse(list(self.transaction_root.glob("prepared-*")))
            self.assertTrue(list(self.transaction_root.glob("transaction.json.pending-*")))
            with self.assertRaisesRegex(RuntimeError, "JOURNAL_PENDING"):
                generate_transaction(self.pack, lambda p: self.write(p, b"n" * 1024))
            self.assertEqual((self.pack / "resource.json").read_bytes(), b"o" * 1024)
        self.proof("journal flush/replace failures retained and retry blocked", blocked=True)

    @unittest.skipUnless(os.name == "nt", "Windows file-share denial")
    def test_windows_cleanup_denial_preserves_primary_and_blocks_retry(self):
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.CreateFileW.restype = ctypes.c_void_p
        kernel.CreateFileW.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32,
            ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p]
        kernel.CloseHandle.argtypes = [ctypes.c_void_p]
        held = []
        def fail(target):
            handle = kernel.CreateFileW(str(target / "resource.json"), 0x80000000, 1, None, 3, 0, None)
            self.assertNotEqual(handle, ctypes.c_void_p(-1).value)
            held.append(handle)
            raise RuntimeError("PRIMARY_GENERATOR_FAILED")
        try:
            with self.assertRaisesRegex(RuntimeError, "primary=PRIMARY_GENERATOR_FAILED;cleanup="):
                generate_transaction(self.pack, fail)
            record = json.loads((self.transaction_root / "transaction.json").read_text(encoding="utf-8"))
            self.assertEqual(record["primary_error"], "PRIMARY_GENERATOR_FAILED")
            self.assertTrue(record["cleanup_error"])
            with self.assertRaisesRegex(RuntimeError, "CLEANUP_PENDING"):
                generate_transaction(self.pack, lambda p: None)
        finally:
            for handle in held: kernel.CloseHandle(handle)
        self.proof("Windows shared handle denied actual rmtree", primary=record["primary_error"],
                   cleanup=record["cleanup_error"], journal_retained=True)

    def test_legacy_journal_has_no_unproven_cleanup_authority(self):
        self.transaction_root.mkdir(parents=True)
        prepared = self.transaction_root / "wvd.generation-prepared-old"; prepared.mkdir()
        (prepared / "external-marker").write_bytes(b"keep")
        journal = self.transaction_root / "transaction.json"
        journal.write_text(json.dumps({"schema": 1, "prepared": prepared.name,
            "previous": "wvd.generation-previous-old"}), encoding="utf-8")
        with self.assertRaisesRegex(RuntimeError, "JOURNAL_INVALID"):
            generate_transaction(self.pack, lambda p: None)
        self.assertEqual((prepared / "external-marker").read_bytes(), b"keep")
        self.assertTrue(journal.exists())
        self.proof("legacy index rejected without deletion", marker_unchanged=True)

    def test_final_validation_failure_restores_verified_previous(self):
        def damage(phase):
            if phase == "published": (self.pack / "resource.json").write_bytes(b"corrupt")
        with self.assertRaisesRegex(RuntimeError, "RESOURCE_MEMBER_CHANGED"):
            generate_transaction(self.pack, lambda p: self.write(p, b"n" * 1024), damage)
        self.assertEqual((self.pack / "resource.json").read_bytes(), b"o" * 1024)
        self.assertFalse(list(self.transaction_root.glob("prepared-*")))
        self.assertFalse((self.transaction_root / "transaction.json").exists())
        self.proof("final published validation fails and restores old authority", old_bytes_unchanged=True)

    @unittest.skipUnless(os.name == "nt", "Windows junction rejection")
    def test_windows_junction_is_rejected_before_copy(self):
        external = self.root / "outside 中文 空格"; external.mkdir()
        (external / "keep").write_bytes(b"external")
        junction = self.pack / "linked"
        result = subprocess.run(["cmd", "/c", "mklink", "/J", str(junction), str(external)],
                                capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace"))
        with self.assertRaisesRegex(RuntimeError, "REPARSE"):
            generate_transaction(self.pack, lambda p: None)
        self.assertEqual((external / "keep").read_bytes(), b"external")
        self.assertFalse(list(self.transaction_root.glob("prepared-*")))
        self.proof("real Windows junction rejected", external_bytes_unchanged=True)

    def test_package_uses_real_shared_transaction_and_full_static_validator(self):
        from test_service_binding_contract import write_candidate
        output = self.root / "dist/candidate"
        transaction_root = output.parent / ".candidate.transactions"
        with patch.object(package_functional, "OUTPUT", output), \
                patch.object(package_functional, "stage", side_effect=lambda target: write_candidate(target)), \
                patch.object(package_functional, "_stage_required_bytes", return_value=65536):
            self.assertEqual(package_functional.package(), output)
            package_functional.validate_candidate(output)
            old_id = resource_generation._identity(output)
            self.assertEqual(package_functional.package(), output)
            previous = list(transaction_root.glob("previous-*"))
            self.assertEqual(len(previous), 1)
            self.assertEqual(resource_generation._identity(previous[0]), old_id)
            self.assertFalse(list(transaction_root.glob("prepared-*")))
            self.assertFalse((transaction_root / "transaction.json").exists())
            before = (output / "automationd.exe").read_bytes()
            (output / "automationd.exe").write_bytes(b"tampered")
            with self.assertRaisesRegex(RuntimeError, "CANDIDATE_HASH_MISMATCH"):
                package_functional.package()
            self.assertEqual((output / "automationd.exe").read_bytes(), b"tampered")
            (output / "automationd.exe").write_bytes(before)
        self.proof("package actual switch/validator, only costly stage replaced",
                   retained_previous=1, orphan_prepared=0)

    def test_package_process_exit_after_each_durable_switch_recovers(self):
        from test_service_binding_contract import write_candidate
        output = self.root / "dist/candidate"; write_candidate(output, b"old")
        transaction_root = output.parent / ".candidate.transactions"
        tools = str(Path(resource_generation.__file__).parent)
        tests = str(Path(__file__).parent)
        for phase in ("journal", "prepared", "previous_saved", "published"):
            code = "\n".join(["import sys,os", "from pathlib import Path",
                "sys.path[:0]=" + repr([tools, tests]), "import package_functional as p, resource_generation as r",
                "from test_service_binding_contract import write_candidate", "p.OUTPUT=Path(" + repr(str(output)) + ")",
                "p.stage=lambda target:write_candidate(target,b'new')", "p._stage_required_bytes=lambda:65536",
                "actual=r.directory_transaction", "def checkpoint(phase):",
                " if phase==" + repr(phase) + ":os._exit(74)",
                "def wrapped(*args,**kwargs):", " kwargs['checkpoint']=checkpoint", " return actual(*args,**kwargs)",
                "r.directory_transaction=wrapped", "p.package()"])
            child = subprocess.run([sys.executable, "-B", "-c", code], capture_output=True, timeout=20)
            self.assertEqual(child.returncode, 74, child.stderr.decode(errors="replace"))
            with patch.object(package_functional, "OUTPUT", output), \
                    patch.object(package_functional, "stage", side_effect=lambda target: write_candidate(target, b"new")), \
                    patch.object(package_functional, "_stage_required_bytes", return_value=65536):
                package_functional.package()
            package_functional.validate_candidate(output)
            self.assertEqual((output / "automationd.exe").read_bytes(), b"new")
            self.assertFalse(list(transaction_root.glob("prepared-*")))
            self.assertFalse((transaction_root / "transaction.json").exists())
        self.proof("package actual four-phase child termination", orphan_prepared=0)

    def test_inventory_distinguishes_path_and_file_logical_lengths(self):
        content = self.root / "hardlinks"; content.mkdir()
        (content / "one").write_bytes(b"m" * 1024)
        os.link(content / "one", content / "two")
        measured = resource_generation._inventory([content])
        self.assertEqual(measured["logical_bytes"], 2048)
        self.assertEqual(measured["unique_file_logical_bytes"], 1024)
        self.assertIn("not exclusive", measured["accounting"])
        self.proof("real hardlinks: deduplicated length is not exclusive disk allocation", measured=measured)

    @unittest.skipUnless(os.name == "nt", "Windows transaction ancestor junction")
    def test_transaction_root_junction_is_rejected_before_any_outside_mkdir(self):
        outside = self.root / "external"; outside.mkdir()
        link = self.root / ".local"
        result = subprocess.run(["cmd", "/c", "mklink", "/J", str(link), str(outside)],
                                capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace"))
        with self.assertRaisesRegex(RuntimeError, "REPARSE"):
            generate_transaction(self.pack, lambda p: None)
        self.assertEqual(list(outside.iterdir()), [])
        self.proof("distinct transaction-root ancestor junction rejected before mkdir", outside_empty=True)

    def test_writer_lock_hardlink_cannot_modify_outside_file(self):
        outside = self.root / "outside-file"; outside.write_bytes(b"")
        self.transaction_root.mkdir(parents=True)
        os.link(outside, self.transaction_root / "writer.lock")
        with self.assertRaisesRegex(RuntimeError, "LOCK_INVALID"):
            generate_transaction(self.pack, lambda p: None)
        self.assertEqual(outside.read_bytes(), b"")
        self.proof("lock leaf hardlink rejected before writing lock sentinel", outside_unchanged=True)

    def test_journal_write_failure_keeps_evidence_and_stops_before_copy(self):
        original_open = Path.open
        class FailedWrite:
            def __init__(self, stream): self.stream = stream
            def __enter__(self): return self
            def __exit__(self, *error): self.stream.close()
            def write(self, data): raise OSError("JOURNAL_WRITE_FAULT")
        def open_file(path, *args, **kwargs):
            stream = original_open(path, *args, **kwargs)
            return FailedWrite(stream) if path.name.startswith("transaction.json.pending-") else stream
        with patch.object(Path, "open", open_file), self.assertRaisesRegex(RuntimeError, "JOURNAL_WRITE_FAULT"):
            generate_transaction(self.pack, lambda p: self.write(p, b"n" * 1024))
        self.assertEqual((self.pack / "resource.json").read_bytes(), b"o" * 1024)
        self.assertFalse(list(self.transaction_root.glob("prepared-*")))
        self.assertEqual(len(list(self.transaction_root.glob("transaction.json.pending-*"))), 1)
        with self.assertRaisesRegex(RuntimeError, "JOURNAL_PENDING"):
            generate_transaction(self.pack, lambda p: None)
        self.proof("actual pending file created; injected write failure blocks future copy", pending_files=1)

    def test_ordinary_rename_failure_restores_current_and_cleans_prepared(self):
        original = Path.rename
        for target in ("old", "prepared"):
            failed = []
            def rename(path, destination):
                if not failed and ((target == "old" and path == self.pack) or
                    (target == "prepared" and path.name.startswith("prepared-"))):
                    failed.append(True)
                    raise PermissionError("RENAME_DENIED_" + target)
                return original(path, destination)
            with patch.object(Path, "rename", rename), self.assertRaisesRegex(PermissionError, "RENAME_DENIED"):
                generate_transaction(self.pack, lambda p: self.write(p, b"n" * 1024))
            self.assertTrue(failed)
            validate_pack(self.pack)
            self.assertEqual((self.pack / "resource.json").read_bytes(), b"o" * 1024)
            self.assertFalse(list(self.transaction_root.glob("prepared-*")))
            self.assertFalse((self.transaction_root / "transaction.json").exists())
        self.proof("ordinary errors at both actual rename calls", current_preserved=True, orphan_prepared=0)

    def test_changed_authority_identity_is_not_deleted(self):
        old = self.root / "external-old"
        def replace(phase):
            if phase == "prepared":
                self.pack.rename(old); self.pack.mkdir()
                (self.pack / "external-marker").write_bytes(b"do not delete")
        with self.assertRaisesRegex(RuntimeError, "CLEANUP_PENDING"):
            generate_transaction(self.pack, lambda p: self.write(p, b"n" * 1024), replace)
        self.assertEqual((self.pack / "external-marker").read_bytes(), b"do not delete")
        self.assertEqual((old / "resource.json").read_bytes(), b"o" * 1024)
        self.assertTrue((self.transaction_root / "transaction.json").exists())
        self.proof("identity conflict preserves both outside marker and old authority", retry_blocked=True)

    def test_preoccupied_prepared_is_not_adopted_or_removed(self):
        original = resource_generation._durable_json
        occupied = []
        def durable(path, record):
            original(path, record)
            if record["phase"] == "planned" and not occupied:
                prepared = path.parent / record["prepared"]; prepared.mkdir()
                (prepared / "external-marker").write_bytes(b"external")
                occupied.append(prepared)
        with patch.object(resource_generation, "_durable_json", durable), self.assertRaisesRegex(RuntimeError, "CLEANUP_PENDING"):
            generate_transaction(self.pack, lambda p: self.write(p, b"n" * 1024))
        self.assertEqual((occupied[0] / "external-marker").read_bytes(), b"external")
        self.assertEqual((self.pack / "resource.json").read_bytes(), b"o" * 1024)
        self.proof("exclusive mkdir collision gives no cleanup authority", marker_preserved=True)

    def test_inventory_limit_rejects_before_stage(self):
        with patch.object(resource_generation, "MAX_INVENTORY_ENTRIES", 1), self.assertRaisesRegex(RuntimeError, "INVENTORY_LIMIT"):
            generate_transaction(self.pack, lambda p: None)
        self.assertEqual((self.pack / "resource.json").read_bytes(), b"o" * 1024)
        self.assertFalse(list(self.transaction_root.glob("prepared-*")))
        self.proof("bounded source inventory rejects before copy", authority_preserved=True)

    def test_retained_history_consumes_available_space_before_switch(self):
        initial = resource_generation._inventory([self.pack])["logical_bytes"]
        unit = resource_generation._allocation_unit(self.pack.parent)
        capacity = resource_generation.RUN_RESERVE_BYTES + 2 * resource_generation.TRANSACTION_MARGIN_BYTES + 11 * unit + initial * 5
        def available(parent):
            histories = list(self.transaction_root.glob("previous-*"))
            occupied = resource_generation._inventory([self.pack, *histories])["unique_file_logical_bytes"]
            return type("Disk", (), {"free": capacity - occupied})()
        rejected = False
        with patch.object(resource_generation.shutil, "disk_usage", available):
            for index in range(10):
                before = resource_generation._inventory([self.pack, *self.transaction_root.glob("previous-*")])
                old = (self.pack / "resource.json").read_bytes()
                try: generate_transaction(self.pack, lambda p: self.write(p, bytes([65 + index]) * 1024))
                except RuntimeError as error:
                    self.assertIn("SPACE_LOW", str(error)); rejected = True
                    self.assertEqual((self.pack / "resource.json").read_bytes(), old)
                    self.assertEqual(resource_generation._inventory([self.pack, *self.transaction_root.glob("previous-*")]), before)
                    break
        self.assertTrue(rejected)
        self.assertGreater(len(list(self.transaction_root.glob("previous-*"))), 0)
        self.assertFalse(list(self.transaction_root.glob("prepared-*")))
        self.proof("retained generations consume injected volume free-space budget", rejected_before_switch=True)

    @unittest.skipUnless(os.name == "nt", "Windows directory sharing refusal")
    def test_windows_directory_rename_permission_denial_preserves_current(self):
        user = getpass.getuser()
        def acl(path, *arguments):
            result = subprocess.run(["icacls", str(path), *arguments], capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace"))
        before = resource_generation._identity(self.pack)
        try:
            # Both ways to authorize a rename (DELETE on the source and
            # DELETE_CHILD on its parent) must be denied on this owned fixture.
            acl(self.pack.parent, "/deny", user + ":(DC)")
            acl(self.pack, "/deny", user + ":(DE)")
            with self.assertRaises(PermissionError):
                generate_transaction(self.pack, lambda p: self.write(p, b"n" * 1024))
        finally:
            acl(self.pack.parent, "/remove:d", user)
            acl(self.pack, "/remove:d", user)
        self.assertEqual(resource_generation._identity(self.pack), before)
        self.assertEqual((self.pack / "resource.json").read_bytes(), b"o" * 1024)
        self.assertFalse(list(self.transaction_root.glob("prepared-*")))
        self.proof("real Windows ACL denies directory rename", authority_preserved=True)


if __name__ == "__main__":
    unittest.main()
