"""M1 服务验收与可选 M2 真实 SDK 离线核心验收；不接入生产链。"""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

from build import ROOT, run

# 只导入本阶段的进程测试工具，不能把仓库 src 加入模块搜索路径。
sys.path.insert(0, str(ROOT / "tests"))
from service_process import NativeService


def validate(m2_offline=False):
    npm = shutil.which("npm.cmd")
    if not npm:
        raise RuntimeError("找不到 npm.cmd")
    run(
        "native-inventory-tests",
        [sys.executable, "-m", "unittest", "discover", "-s", str(ROOT / "tests"), "-v"],
    )
    if m2_offline:
        run(
            "m2-core-tests",
            [
                sys.executable,
                "-m",
                "unittest",
                "discover",
                "-s",
                str(ROOT / "tests/m2"),
                "-v",
            ],
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
    print(
        "M1 validation complete"
        + ("; M2 offline core validated" if m2_offline else "")
        + "; no device or production task was opened."
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--m2-offline",
        action="store_true",
        help="额外执行真实 Maa 识别、运行与门禁离线测试",
    )
    args = parser.parse_args()
    try:
        validate(args.m2_offline)
    except (OSError, RuntimeError, AssertionError, subprocess.SubprocessError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
