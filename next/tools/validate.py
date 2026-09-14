"""M1 服务验收与可选 M2 真实 SDK 离线核心验收；不接入生产链。"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

from build import ROOT, run

# 只导入本阶段的进程测试工具，不能把仓库 src 加入模块搜索路径。
sys.path.insert(0, str(ROOT / "tests"))
from service_process import NativeService


def validate(m2_offline=False, m3=False, m4=False):
    m3 = m3 or m4
    gate = ROOT / ".local/m3-gate-b.json"
    if m3:
        gate.write_text(json.dumps({"offline_result": "RUNNING"}), encoding="utf-8")
    npm = shutil.which("npm.cmd")
    if not npm:
        raise RuntimeError("找不到 npm.cmd")
    run(
        "native-inventory-tests",
        [sys.executable, "-m", "unittest", "discover", "-s", str(ROOT / "tests"), "-v"],
    )
    if m2_offline or m3:
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
    if m3:
        run(
            "m3-offline-tests",
            [
                sys.executable,
                "-m",
                "unittest",
                "discover",
                "-s",
                str(ROOT / "tests/m3"),
                "-v",
            ],
        )
    if m4:
        run("m4-data-tests", [sys.executable,"-m","unittest","discover","-s",str(ROOT/"tests/m4"),"-v"])
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
    if m3:
        binaries = [
            ROOT / "build/m3/Release" / name
            for name in ("test_m3.exe", "wvd_m3_check.exe")
        ]
        gate.write_text(
            json.dumps(
                {
                    "schema": 1,
                    "offline_result": "PASS",
                    "binaries": {
                        p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                        for p in binaries
                    },
                },
                indent=2,
            ),
            encoding="utf-8",
        )
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
    parser.add_argument(
        "--m3", action="store_true", help="执行 M1/M2/M3 离线验收，不连接设备"
    )
    parser.add_argument("--m4", action="store_true", help="执行已实现的 M4 离线覆盖；完整状态以逐任务台账为准")
    args = parser.parse_args()
    try:
        validate(args.m2_offline, args.m3, args.m4)
    except (OSError, RuntimeError, AssertionError, subprocess.SubprocessError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
