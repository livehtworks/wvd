"""一键 M1 验收：真实 native 服务 + 只读盘点 + 浏览器；不接入生产链。"""

import os
from pathlib import Path
import shutil
import subprocess
import sys

from build import ROOT, run

# 只导入本阶段的进程测试工具，不能把仓库 src 加入模块搜索路径。
sys.path.insert(0, str(ROOT / "tests"))
from service_process import NativeService


def validate():
    npm = shutil.which("npm.cmd")
    if not npm:
        raise RuntimeError("找不到 npm.cmd")
    run(
        "native-inventory-tests",
        [sys.executable, "-m", "unittest", "discover", "-s", str(ROOT / "tests"), "-v"],
    )
    service = NativeService()
    previous = os.environ.get("WVD_NEXT_URL")
    try:
        os.environ["WVD_NEXT_URL"] = service.url
        run("browser-tests", [npm, "test"], ROOT / "web")
    finally:
        if previous is None:
            os.environ.pop("WVD_NEXT_URL", None)
        else:
            os.environ["WVD_NEXT_URL"] = previous
        try:
            service.stop()
        finally:
            service.cleanup()
    print("M1 validation complete; no device or production task was opened.")


if __name__ == "__main__":
    try:
        validate()
    except (OSError, RuntimeError, AssertionError, subprocess.SubprocessError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
