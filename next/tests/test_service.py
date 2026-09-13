"""真实 Windows EXE 的只读 API、边界与正常退出，不连接 Android。"""

import http.client
import json
from pathlib import Path
import socket
import subprocess
import tempfile
import unittest

from service_process import EXE, NativeService, NEXT


class ServiceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.service = NativeService()

    @classmethod
    def tearDownClass(cls):
        try:
            cls.service.stop()
        finally:
            cls.service.cleanup()

    def request(self, path, method="GET", headers=None):
        conn = http.client.HTTPConnection("127.0.0.1", self.service.port, timeout=8)
        try:
            conn.request(method, path, headers=headers or {})
            response = conn.getresponse()
            return response.status, dict(response.getheaders()), response.read()
        finally:
            conn.close()

    def test_version_and_capabilities(self):
        status, _, body = self.request("/api/v1/version")
        self.assertEqual(status, 200)
        self.assertEqual(
            json.loads(body),
            {
                "service": "automationd",
                "version": "0.1.0",
                "api_version": 1,
                "stage": "M1",
            },
        )
        status, _, body = self.request("/api/v1/capabilities")
        self.assertEqual(status, 200)
        data = json.loads(body)
        for key in (
            "device_control",
            "task_execution",
            "production_switch",
            "websocket",
        ):
            self.assertIs(data[key], False)
        self.assertEqual(data["maafw"], {"loaded": False, "locked_version": "5.13.0"})

    def test_write_and_future_routes_unavailable(self):
        for method in ("POST", "PUT", "DELETE", "PATCH", "OPTIONS"):
            with self.subTest(method=method):
                status, _, body = self.request("/api/v1/run/start", method)
                self.assertEqual(status, 405)
                self.assertEqual(json.loads(body)["error_code"], "READ_ONLY_STAGE")
        self.assertEqual(self.request("/api/v1/device")[0], 404)

    def test_host_and_origin(self):
        self.assertEqual(
            self.request("/api/v1/version", headers={"Host": "foreign.example"})[0], 403
        )
        self.assertEqual(
            self.request(
                "/api/v1/version", headers={"Origin": "http://foreign.example"}
            )[0],
            403,
        )
        self.assertEqual(
            self.request("/api/v1/version", headers={"Origin": self.service.url})[0],
            200,
        )

    def test_paths_and_head(self):
        for path in (
            "/../CMakeLists.txt",
            "/%2e%2e/CMakeLists.txt",
            "/%5cWindows",
            "/C%3a/Windows",
            "/%00",
            "/%xx",
        ):
            with self.subTest(path=path):
                self.assertIn(self.request(path)[0], (400, 403, 404))
        status, headers, body = self.request("/", "HEAD")
        self.assertEqual(status, 200)
        self.assertEqual(body, b"")
        self.assertGreater(int(headers["Content-Length"]), 0)
        self.assertIn("frame-ancestors 'none'", headers["Content-Security-Policy"])

    def test_real_static_inventory(self):
        status, _, body = self.request("/migration/feature_inventory.json")
        self.assertEqual(status, 200)
        self.assertEqual(
            body, (NEXT / "docs/migration/feature_inventory.json").read_bytes()
        )
        self.assertEqual(self.request("/missing.png")[0], 404)

    def test_occupied_port_fails_without_taking_over(self):
        result = subprocess.run(
            [
                str(EXE),
                "--web-root",
                str(NEXT / "web/dist"),
                "--port",
                str(self.service.port),
            ],
            capture_output=True,
            timeout=8,
        )
        self.assertEqual(result.returncode, 1)
        self.assertIn(b"STARTUP_ERROR", result.stderr)
        self.assertEqual(self.request("/api/v1/version")[0], 200)

    def test_command_line_rejects_invalid_arguments(self):
        for args in (
            [],
            ["--port", "-1"],
            ["--port", "65536"],
            ["--unknown"],
            ["--port", "x"],
        ):
            with self.subTest(args=args):
                result = subprocess.run(
                    [str(EXE), *args], capture_output=True, timeout=5
                )
                self.assertEqual(result.returncode, 1)
        result = subprocess.run([str(EXE), "--version"], capture_output=True, timeout=5)
        self.assertEqual(result.returncode, 0)
        self.assertIn(b"stage=M1", result.stdout)

    def test_pending_reads_cancel_and_process_can_restart(self):
        for _ in range(3):
            service = NativeService()
            pending = socket.create_connection(("127.0.0.1", service.port), timeout=3)
            try:
                pending.sendall(b"GET / HTTP/1.1\r\n")
                service.stop()
                self.assertEqual(service.process.returncode, 0)
            finally:
                pending.close()
                service.cleanup()

    def test_unicode_web_root(self):
        with tempfile.TemporaryDirectory(prefix="wvd-next-中文路径-") as folder:
            (Path(folder) / "index.html").write_text("独立测试目录", encoding="utf-8")
            service = NativeService(root=folder)
            try:
                conn = http.client.HTTPConnection("127.0.0.1", service.port, timeout=5)
                conn.request("GET", "/")
                response = conn.getresponse()
                self.assertEqual(response.read().decode(), "独立测试目录")
                conn.close()
                service.stop()
            finally:
                service.cleanup()
