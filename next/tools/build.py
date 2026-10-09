"""Build the single native WVD Next product and its Vue workbench."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import base64
from datetime import datetime, timezone
import package_functional

ROOT = Path(__file__).resolve().parents[1]


def cmake_path():
    if tool := shutil.which("cmake"):
        return tool
    installer = Path(os.environ.get("ProgramFiles(x86)", "")) / "Microsoft Visual Studio/Installer/vswhere.exe"
    if not installer.is_file():
        raise RuntimeError("找不到 VS 2022 CMake 组件")
    installs = json.loads(subprocess.check_output([
        str(installer), "-products", "*", "-version", "[17.0,18.0)",
        "-format", "json", "-utf8",
    ]).decode("utf-8-sig"))
    for install in installs:
        path = Path(install["installationPath"]) / "Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
        if path.is_file():
            return str(path)
    raise RuntimeError("已安装的 VS 2022 中没有 CMake 组件")


def run(name, command, cwd=ROOT, timeout_seconds=1200):
    folder = ROOT / ".local/logs"
    folder.mkdir(parents=True, exist_ok=True)
    log = folder / (name + "-" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%f") + ".log")
    print(name, flush=True)
    pwsh = shutil.which("pwsh")
    if not pwsh:
        raise RuntimeError("Bounded build ownership requires PowerShell 7")
    executable, *arguments = [str(item) for item in command]
    resolved = shutil.which(executable)
    if not resolved:
        raise RuntimeError("BUILD_EXECUTABLE_NOT_FOUND:" + executable)
    executable = str(Path(resolved).absolute())
    if executable.lower().endswith((".cmd", ".bat")):
        arguments = ["/d", "/s", "/c", subprocess.list2cmdline([executable, *arguments])]
        executable = os.environ.get("COMSPEC", "C:/Windows/System32/cmd.exe")
    payload = base64.b64encode(json.dumps([executable, arguments], ensure_ascii=False).encode("utf-8")).decode("ascii")
    literal = lambda text: "'" + str(text).replace("'", "''") + "'"
    script = "$ErrorActionPreference='Stop'; Import-Module " + literal(ROOT / "tools/memory_trace_support.psm1") + "; "
    script += "$command=[Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('" + payload + "'))|ConvertFrom-Json; "
    script += "Invoke-TraceTool -Executable $command[0] -Arguments $command[1] -RecordOnly -Log " + literal(log)
    script += " -TimeoutSeconds " + str(int(timeout_seconds)) + " -OutputLimit 67108864"
    encoded = base64.b64encode(script.encode("utf-16-le")).decode("ascii")
    result = subprocess.run([pwsh, "-NoProfile", "-EncodedCommand", encoded], cwd=cwd,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout_seconds + 15)
    if result.returncode:
        print(result.stdout.decode("utf-8", errors="replace")[-2000:])
        if log.exists():
            with log.open("rb") as output:
                output.seek(max(0, log.stat().st_size - 4000))
                print(output.read().decode("utf-8", errors="replace"))
        raise RuntimeError(f"{name} 失败 (exit={result.returncode})，完整日志: {log}")
    print("  OK: " + str(log), flush=True)


def build():
    npm = shutil.which("npm.cmd")
    if npm is None:
        raise RuntimeError("找不到 npm")
    lock = json.loads((ROOT / "dependencies.lock.json").read_text(encoding="utf-8"))
    for name, executable in (("node", shutil.which("node")), ("npm", npm)):
        if not executable:
            raise RuntimeError("找不到 " + name)
        version = subprocess.check_output([executable, "--version"], text=True).strip().removeprefix("v")
        if version != lock["toolchain"][name]:
            raise RuntimeError(f"{name} 需要 {lock['toolchain'][name]}，实际 {version}")
    cmake = cmake_path()
    run("dependencies", [sys.executable, str(ROOT / "tools/dependencies.py")])
    run("native-dependencies", [sys.executable, str(ROOT / "tools/prepare_native.py")])
    package_functional.sync_authoring_resources()
    run("npm-ci", [npm, "ci", "--ignore-scripts", "--no-fund", "--no-audit"], ROOT / "web")
    run("native-configure", [cmake, "--preset", "windows-x64"])
    package_functional.begin_build()
    run("web-build", [npm, "run", "build"], ROOT / "web")
    run("native-build", [cmake, "--build", "--preset", "windows-release", "--target", "automationd"])
    package_functional.finish_build()
    run("native-package", [sys.executable, str(ROOT / "tools/package_functional.py")])
    print("Maa-free Windows candidate ready; legacy wvd.exe/config/mod untouched.")


if __name__ == "__main__":
    try:
        build()
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
