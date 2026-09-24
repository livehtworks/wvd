"""M3 单实例受限检查；只有显式执行此工具才查询/启动指定实例，禁止游戏输入。"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def run_json(args):
    result = subprocess.run(
        args,
        capture_output=True,
        encoding="utf-8",
        timeout=15,
        creationflags=subprocess.CREATE_NO_WINDOW,
    )
    if result.returncode:
        raise RuntimeError("METADATA_COMMAND_FAILED: " + result.stderr[-500:])
    return json.loads(result.stdout)


def audit_controllers():
    command = "Get-CimInstance Win32_Process | Where-Object {$_.Name -match '^(wvd|scrcpy|python|pythonw).*exe$'} | Select-Object ProcessId,Name,ExecutablePath,CommandLine | ConvertTo-Json -Compress"
    result = subprocess.run(
        ["powershell", "-NoProfile", "-Command", command],
        capture_output=True,
        encoding="utf-8",
        errors="replace",
        timeout=15,
        creationflags=subprocess.CREATE_NO_WINDOW,
    )
    if result.returncode:
        raise RuntimeError("PROCESS_AUDIT_FAILED")
    processes = json.loads(result.stdout or "[]")
    if isinstance(processes, dict):
        processes = [processes]
    for process in processes:
        command = process.get("CommandLine") or ""
        if process["Name"].lower() in ("wvd.exe", "scrcpy.exe", "pythonw.exe"):
            raise RuntimeError("OTHER_CONTROLLER_PRESENT")
        if (
            "python" in process["Name"].lower()
            and "validate_m3_device.py" not in command
        ):
            raise RuntimeError("PYTHON_CONTROLLER_OWNERSHIP_UNCONFIRMED")
    return processes


def discover(config_path, output):
    if output.exists():
        raise RuntimeError("BINDING_OUTPUT_MUST_BE_NEW")
    config = read_json(config_path)["GENERAL"]
    executable = Path(config["EMU_PATH"])
    candidates = [
        parent / "nx_main/MuMuManager.exe"
        for parent in executable.parents
        if (parent / "nx_main/MuMuManager.exe").is_file()
    ]
    if len(candidates) != 1:
        raise RuntimeError("MUMU_INSTALL_NOT_UNIQUE")
    manager = candidates[0]
    index = int(config["EMU_INDEX"])
    info = run_json([str(manager), "info", "-v", str(index)])
    if info.get("error_code") != 0 or str(info.get("index")) != str(index):
        raise RuntimeError("MUMU_INSTANCE_MISMATCH")
    audit = audit_controllers()
    binding = {
        "schema": 1,
        "index": index,
        "created_timestamp": info["created_timestamp"],
        "manager": str(manager),
        "adb": str(manager.parent / "adb.exe"),
        "install_root": str(manager.parent.parent),
        "serial": config["ADB_ADRESS"],
        "controller_audit": "NO_OTHER_CONTROLLER",
        "process_audit": audit,
        "initial_manager": info,
        "initially_running": info.get("is_process_started", False),
    }
    if info.get("is_android_started") and binding["serial"] != "127.0.0.1:" + str(
        info.get("adb_port")
    ):
        raise RuntimeError("CONFIG_MANAGER_ADB_MISMATCH")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        json.dumps(binding, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    print("单实例定位已保存到私有绑定文件；尚未连接/启动安卓。")


def validate(binding_path, safe_navigation=False, start_instance=False):
    binding = read_json(binding_path)
    audit_controllers()
    if safe_navigation:
        raise RuntimeError(
            "BLOCKED: 尚无经视觉确认的可逆系统导航场景；本入口不会试点游戏或设置。"
        )
    gate = ROOT / ".local/m3-gate-b.json"
    if not gate.is_file() or read_json(gate).get("offline_result") != "PASS":
        raise RuntimeError("GATE_B_OFFLINE_NOT_PASSED")
    for name, expected in read_json(gate)["binaries"].items():
        if (
            hashlib.sha256((ROOT / "build/m3/Release" / name).read_bytes()).hexdigest()
            != expected
        ):
            raise RuntimeError("GATE_B_BINARY_CHANGED")
    manager = binding["manager"]
    index = str(binding["index"])
    info = run_json([manager, "info", "-v", index])
    if not info.get("is_process_started"):
        if not start_instance:
            raise RuntimeError("MUMU_OFF: 需显式 --start-instance 才启动已确认实例")
        subprocess.run(
            [manager, "control", "-v", index, "launch"],
            check=True,
            timeout=30,
            creationflags=subprocess.CREATE_NO_WINDOW,
            stdout=subprocess.DEVNULL,
        )
        deadline = time.monotonic() + 90
        while not info.get("is_android_started"):
            if time.monotonic() > deadline:
                raise RuntimeError("MUMU_START_TIMEOUT")
            time.sleep(2)
            info = run_json([manager, "info", "-v", index])
    if binding["serial"] != "127.0.0.1:" + str(info.get("adb_port")):
        raise RuntimeError("MUMU_ADB_BINDING_CHANGED")
    folder = ROOT / ".local" / ("m3-device-" + uuid.uuid4().hex)
    sdk = Path(read_json(ROOT / ".local/maafw.json")["sdk"])
    env = os.environ.copy()
    env["PATH"] = str(sdk / "bin") + os.pathsep + env.get("PATH", "")
    log_path = folder.with_suffix(".log")
    with log_path.open("wb") as log:
        # 不对原生会话强杀或超时后换控制者；原生等待的边界单独保留。
        result = subprocess.run(
            [
                str(ROOT / "build/m3/Release/wvd_m3_check.exe"),
                "--binding",
                str(binding_path.resolve()),
                "--capture-only",
                str(folder),
            ],
            env=env,
            cwd=ROOT / ".local",
            stdout=log,
            stderr=log,
            creationflags=subprocess.CREATE_NEW_PROCESS_GROUP,
        )
    if result.returncode:
        raise RuntimeError(
            "M3_NATIVE_FAILED: "
            + log_path.read_text(encoding="utf-8", errors="replace")[-1500:]
        )
    print("私有截图证据: " + str(folder))
    print("实例保持运行；未启动游戏、VPN或执行任务。")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--discover", type=Path, help="只读旧配置路径，定位设备但不连接"
    )
    parser.add_argument("--binding", type=Path, required=True)
    parser.add_argument("--capture-only", action="store_true")
    parser.add_argument("--safe-system-navigation", action="store_true")
    parser.add_argument("--start-instance", action="store_true")
    args = parser.parse_args()
    try:
        if args.discover:
            discover(args.discover, args.binding)
        elif args.capture_only or args.safe_system_navigation:
            validate(args.binding, args.safe_system_navigation, args.start_instance)
        else:
            parser.error("选择 --discover、--capture-only 或 --safe-system-navigation")
    except (
        OSError,
        ValueError,
        KeyError,
        RuntimeError,
        subprocess.SubprocessError,
    ) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
