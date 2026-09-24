"""真实 Windows EXE 的只读 API、边界与正常退出，不连接 Android。"""

import http.client
import json
from pathlib import Path
import socket
import subprocess
import tempfile
import unittest

from service_process import EXE, NativeService, NEXT


def inspect_raw_response(raw, method):
    header, separator, body = raw.partition(b"\r\n\r\n")
    if not separator:
        raise AssertionError("Incomplete HTTP headers")
    lines = header.decode("iso-8859-1").split("\r\n")
    status = int(lines[0].split()[1])
    headers = dict(line.split(":", 1) for line in lines[1:])
    headers = {name.lower(): value.strip() for name, value in headers.items()}
    length = int(headers["content-length"])
    if method == "HEAD" and body:
        raise AssertionError("HEAD sent body bytes")
    if method == "GET" and len(body) != length:
        raise AssertionError("GET body length mismatch")
    return status, headers, body


def raw_request(port, path, method, headers=None):
    values = {"Host": f"127.0.0.1:{port}", "Connection": "close"}
    values.update(headers or {})
    payload = f"{method} {path} HTTP/1.1\r\n"
    payload += (
        "".join(f"{name}: {value}\r\n" for name, value in values.items()) + "\r\n"
    )
    with socket.create_connection(("127.0.0.1", port), timeout=8) as connection:
        connection.sendall(payload.encode("ascii"))
        chunks = []
        total = 0
        # 必须读到 EOF；timeout/reset 都让测试失败，不能被当作没有正文。
        while chunk := connection.recv(4096):
            total += len(chunk)
            if total > 1024 * 1024:
                raise AssertionError("Small-response test exceeded byte limit")
            chunks.append(chunk)
    return inspect_raw_response(b"".join(chunks), method)


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
        status, headers, body = raw_request(self.service.port, "/", "HEAD")
        self.assertEqual(status, 200)
        self.assertEqual(body, b"")
        self.assertGreater(int(headers["content-length"]), 0)
        self.assertIn("frame-ancestors 'none'", headers["content-security-policy"])

    def test_raw_head_success_and_error_matrix(self):
        cases = [
            ("/", {}, 200, None),
            ("/api/v1/version", {}, 200, None),
            ("/api/not-found", {}, 404, "UNKNOWN_API"),
            ("/missing.png", {}, 404, "ASSET_NOT_FOUND"),
            ("/%xx", {}, 400, "INVALID_PATH"),
            ("/%2e%2e/secrets", {}, 403, "PATH_TRAVERSAL"),
            ("/", {"Host": "foreign.example"}, 403, "INVALID_HOST"),
            ("/", {"Origin": "http://foreign.example"}, 403, "CROSS_ORIGIN_DENIED"),
        ]
        with tempfile.TemporaryDirectory(prefix="wvd-head-wire-") as folder:
            (Path(folder) / "index.html").write_bytes(b"M1 HEAD wire check")
            service = NativeService(root=folder)
            try:
                for path, options, expected_status, error_code in cases:
                    with self.subTest(path=path, headers=options):
                        status, headers, body = raw_request(
                            service.port, path, "GET", options
                        )
                        head_status, head_headers, head_body = raw_request(
                            service.port, path, "HEAD", options
                        )
                        self.assertEqual(status, expected_status)
                        self.assertEqual(head_status, status)
                        self.assertEqual(head_headers, headers)
                        self.assertEqual(head_body, b"")
                        self.assertEqual(int(head_headers["content-length"]), len(body))
                        if error_code:
                            self.assertEqual(json.loads(body)["error_code"], error_code)
                        elif path == "/":
                            self.assertEqual(body, b"M1 HEAD wire check")
                        else:
                            self.assertEqual(json.loads(body)["api_version"], 1)
                        print(
                            "R01_RAW "
                            + json.dumps(
                                {
                                    "path": path,
                                    "security_case": error_code,
                                    "status": status,
                                    "declared_length": int(
                                        head_headers["content-length"]
                                    ),
                                    "head_body_bytes": len(head_body),
                                    "get_body_bytes": len(body),
                                }
                            )
                        )
                service.stop()
            finally:
                service.cleanup()

    def test_raw_checker_rejects_head_body(self):
        with self.assertRaisesRegex(AssertionError, "HEAD sent body bytes"):
            inspect_raw_response(
                b"HTTP/1.1 404 Not Found\r\nContent-Length: 3\r\n\r\nbad", "HEAD"
            )
        with self.assertRaisesRegex(AssertionError, "Incomplete HTTP"):
            inspect_raw_response(b"HTTP/1.1 200 OK\r\n", "HEAD")

    def test_internal_exception_response_serialization(self):
        result = subprocess.run(
            [str(EXE.with_name("test_head_response.exe"))],
            capture_output=True,
            text=True,
            timeout=8,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        rows = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual([r["method"] for r in rows], ["GET", "HEAD"])
        self.assertTrue(all(r["status"] == 500 for r in rows))
        self.assertEqual(rows[0]["actual_body_bytes"], rows[0]["declared_length"])
        self.assertGreater(rows[0]["actual_body_bytes"], 0)
        self.assertEqual(rows[1]["actual_body_bytes"], 0)
        self.assertEqual(rows[1]["declared_length"], rows[0]["declared_length"])
        print("R01_INTERNAL " + json.dumps(rows))

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
