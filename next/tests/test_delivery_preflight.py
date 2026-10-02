"""预检只使用新临时目录与替身 HTTP 端口，不操作正式服务或设备。"""
import argparse
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


def source_change_case():
    spec = importlib.util.spec_from_file_location("packager", Path(__file__).parents[1] / "tools/package_functional.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    with tempfile.TemporaryDirectory(prefix="wvd-build-identity-") as folder:
        repo = Path(folder)
        module.ROOT = repo / "next"
        module.BUILD_RECEIPT = module.ROOT / ".local/build-input.json"
        for path in ("next/resources/authoring", "next/packs/wvd/parameters", "next/resources/images", "next/build/generated"):
            (repo / path).mkdir(parents=True)
        for name, value in (("semantic-assets.json", {"resources": {}}), ("public-flows.json", [])):
            raw = json.dumps(value).encode()
            (module.ROOT / "resources/authoring" / name).write_bytes(raw)
            (module.ROOT / "packs/wvd/parameters" / name).write_bytes(raw)
        source = module.ROOT / "resources/authoring/semantic-assets.json"
        (module.ROOT / "build/generated/semantic_catalogue.sha256").write_text(module.sha256(source), encoding="ascii")
        for args in (("init", "-q"), ("config", "user.name", "Isolated delivery test"),
                     ("config", "user.email", "test@example.invalid"), ("add", "next"), ("commit", "-qm", "fixture")):
            subprocess.run(["git", *args], cwd=repo, check=True, capture_output=True)
        module.begin_build()
        source.write_text('{"resources": {}, "changed": true}', encoding="utf-8")
        try:
            module.finish_build()
        except RuntimeError as error:
            assert str(error) == "BUILD_SOURCE_CHANGED", error
        else:
            raise AssertionError("Source mutation accepted")
        assert json.loads(module.BUILD_RECEIPT.read_text())["state"] == "BUILDING"
    print("PASS: source changed during build rejected before artifact acceptance")


def candidate_cases(candidate):
    calls = []

    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            calls.append(("GET", self.path))
            self.send_response(503)
            self.end_headers()

        def do_POST(self):
            calls.append(("POST", self.path))
            self.send_response(503)
            self.end_headers()

        def log_message(self, *args):
            pass

    with tempfile.TemporaryDirectory(prefix="wvd-preflight-") as folder:
        root = Path(folder)
        fixture = root / "candidate"
        shutil.copytree(candidate, fixture, ignore=shutil.ignore_patterns("service-launch.json"))
        server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        thread = threading.Thread(target=server.serve_forever)
        thread.start()
        try:
            common = ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(fixture / "tools/manage_service.ps1"),
                      "-CandidateRoot", str(fixture), "-DataRoot", str(root / "fake-data"), "-Port", str(server.server_port)]

            def invoke(action):
                completed = subprocess.run([*common, "-Action", action], capture_output=True, timeout=90)
                assert not calls, f"Preflight touched old service: {calls}"
                return completed

            complete = invoke("Validate")
            assert complete.returncode == 0, complete.stderr.decode(errors="replace")
            print("PASS: complete candidate validated; no deployment or service query")
            for relative, fault in (("wvd-capture-host.exe", "missing"), ("automationd.exe", "exe-hash"),
                                    ("pack/parameters/semantic-assets.json", "pack-member")):
                path = fixture / relative
                saved = path.read_bytes()
                try:
                    if fault == "missing":
                        path.unlink()
                    else:
                        path.write_bytes(saved + b"tampered")
                    # 使用生产 Start/Deploy 同入口的只读预检，不启动或关闭任何服务。
                    rejected = invoke("Validate")
                    assert rejected.returncode != 0, f"Invalid candidate accepted: {fault}"
                    assert b"CANDIDATE_" in rejected.stderr, rejected.stderr
                    print(f"PASS: {fault} rejected before old-service read/shutdown")
                finally:
                    path.write_bytes(saved)
            assert not (root / "fake-data").exists(), "Preflight wrote service data"
            assert not (fixture / "service-launch.json").exists(), "Preflight wrote launch binding"
        finally:
            server.shutdown()
            server.server_close()
            thread.join()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate", type=Path)
    args = parser.parse_args()
    source_change_case()
    if args.candidate:
        candidate_cases(args.candidate.resolve())
