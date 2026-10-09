"""Exercise the actual build/resource helpers only in disposable directories."""

import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile
import subprocess
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from dependency_tree import verify_consumed_tree
from resource_generation import generate_transaction, validate_pack
import package_functional
import build


class BuildResourceContract(unittest.TestCase):
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


if __name__ == "__main__":
    unittest.main()
