"""仅启动 next 原生服务的实测工具；独立隐藏控制台，正常发送 Ctrl+C 停止。"""

from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile
import time

NEXT = Path(__file__).resolve().parents[1]
EXE = NEXT / "build/Release/automationd.exe"


def interrupt(pid):
    # 辅助进程附着服务的独立控制台，不能向测试器或用户所在控制台广播停止。
    code = """
import ctypes, sys
k = ctypes.WinDLL('kernel32', use_last_error=True)
k.FreeConsole()
if not k.AttachConsole(int(sys.argv[1])):
    raise ctypes.WinError(ctypes.get_last_error())
k.SetConsoleCtrlHandler(None, True)
if not k.GenerateConsoleCtrlEvent(0, 0):
    raise ctypes.WinError(ctypes.get_last_error())
k.FreeConsole()
"""
    subprocess.run([sys.executable, "-c", code, str(pid)], check=True, timeout=5)


class NativeService:
    def __init__(self, root=None, port=0):
        self.temp = tempfile.TemporaryDirectory(prefix="wvd-next-m1-")
        self.log_path = Path(self.temp.name) / "service.log"
        self.log = self.log_path.open("wb")
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
        self.process = subprocess.Popen(
            [
                str(EXE),
                "--web-root",
                str(root or NEXT / "web/dist"),
                "--port",
                str(port),
            ],
            cwd=self.temp.name,
            stdout=self.log,
            stderr=subprocess.STDOUT,
            startupinfo=startup,
            creationflags=subprocess.CREATE_NEW_CONSOLE,
        )
        deadline = time.monotonic() + 12
        while time.monotonic() < deadline:
            match = re.search(r"READY (http://127\.0\.0\.1:(\d+))", self.output())
            if match:
                self.url, self.port = match[1], int(match[2])
                return
            if self.process.poll() is not None:
                break
            time.sleep(0.05)
        output = self.output()
        self.cleanup()
        raise RuntimeError("Service did not become ready: " + output)

    def output(self):
        return self.log_path.read_text(encoding="utf-8", errors="replace")

    def stop(self):
        interrupt(self.process.pid)
        code = self.process.wait(timeout=8)
        if code != 0 or "STOPPED" not in self.output():
            raise AssertionError(f"Not graceful: exit={code}, {self.output()}")

    def cleanup(self):
        if self.process.poll() is None:
            # 只用于失败后的隔离进程清理，绝不计作停止验收通过。
            self.process.kill()
            self.process.wait(timeout=5)
        self.log.close()
        self.temp.cleanup()
