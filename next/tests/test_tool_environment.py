import os
from pathlib import Path
import tempfile
import unittest
import sys
import shutil
import subprocess
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from tool_environment import checked_directory, tool_environment


class ToolEnvironmentTests(unittest.TestCase):
    def test_inherited_defaults_are_replaced_without_mutating_parent(self):
        with tempfile.TemporaryDirectory(prefix="wvd-tool-paths-") as directory:
            root = Path(directory)
            parent = {"TEMP": "C:/unowned", "_NT_SYMCACHE_PATH": "D:/SymCache", "UNCHANGED": "value"}
            actual = tool_environment(root, parent)
            self.assertEqual(parent["TEMP"], "C:/unowned")
            self.assertEqual(actual["UNCHANGED"], "value")
            for key in actual.keys() - {"UNCHANGED"}:
                self.assertTrue(Path(actual[key]).is_relative_to(root / ".local"))
                self.assertTrue(Path(actual[key]).is_dir())

    def test_invalid_paths_never_create_outside_private_root(self):
        with tempfile.TemporaryDirectory(prefix="wvd-tool-paths-") as directory:
            root = Path(directory)
            for relative in ("../escaped", "tools/cache", ".local", str(root.anchor)):
                with self.subTest(relative=relative), self.assertRaisesRegex(RuntimeError, "TOOL_STATE_"):
                    checked_directory(root, relative)
            self.assertFalse((root / "tools").exists())
            self.assertFalse((root / ".local").exists())

    def test_readonly_resolution_does_not_create_directories(self):
        with tempfile.TemporaryDirectory(prefix="wvd-tool-paths-") as directory:
            root = Path(directory)
            path = checked_directory(root, ".local/cache/headers", create=False)
            self.assertEqual(path, root / ".local/cache/headers")
            self.assertFalse(path.exists())

    def test_heap_cache_escape_is_rejected_before_any_service_request(self):
        calls = []
        class Handler(BaseHTTPRequestHandler):
            def do_GET(self):
                calls.append(self.path)
                self.send_response(503)
                self.end_headers()
            def log_message(self, *args):
                pass
        server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            with tempfile.TemporaryDirectory(prefix="wvd-cache-guard-") as directory:
                root = Path(directory)
                script = Path(__file__).resolve().parents[1] / "tools/capture_idle_heap.ps1"
                result = subprocess.run([shutil.which("pwsh"), "-NoProfile", "-File", str(script),
                    "-EvidenceRoot", str(root / "evidence"), "-Cdb", str(root / "unused.exe"),
                    "-Pdb", str(root / "unused.pdb"), "-SymbolCache", "D:/SymCache",
                    "-Port", str(server.server_port)], env=tool_environment(root), capture_output=True, timeout=20)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(b"HEAP_SYMBOL_CACHE_OUTSIDE_OWNED_OUTPUT", result.stderr)
                self.assertEqual(calls, [])
                self.assertFalse((root / "evidence").exists())
        finally:
            server.shutdown()
            server.server_close()
            thread.join()

    def test_real_junction_cannot_redirect_private_cache(self):
        with tempfile.TemporaryDirectory(prefix="wvd-cache-junction-") as directory:
            root = Path(directory) / "workspace"
            outside = Path(directory) / "outside"
            root.mkdir()
            outside.mkdir()
            link = root / ".local"
            quoted = lambda value: "'" + str(value).replace("'", "''") + "'"
            command = "New-Item -ItemType Junction -Path " + quoted(link) + " -Target " + quoted(outside) + " | Out-Null"
            subprocess.run([shutil.which("pwsh"), "-NoProfile", "-Command", command], check=True, capture_output=True)
            try:
                with self.assertRaisesRegex(RuntimeError, "TOOL_STATE_REPARSE_FORBIDDEN"):
                    tool_environment(root)
                self.assertEqual(list(outside.iterdir()), [])
            finally:
                os.rmdir(link)  # Remove only this test's junction, not its target.


if __name__ == "__main__":
    unittest.main()
