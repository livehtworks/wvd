"""Module-scoped production contracts. No emulator, service, or game launch."""
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path
from build import ROOT, cmake_path, run
from package_functional import source_identity

FLOW_REGRESSIONS = (
    "recovery-recheck", "linkage-closure", "positive-loop-frame", "child-result-frame",
    "late-receipt", "restored-context-timer", "critical-recovery", "confirmed-result",
    "timeout-reclassification", "selection-race", "restart-during-boot", "tolerance",
)
CASES = {
    "runtime": [("test_native_flow", ["--" + flag]) for flag in
        (*FLOW_REGRESSIONS, "receipt-exception-safety", "exit-proof-causality", "input-effect-contract", "progress-contract")],
    "devices": [("test_native_devices", []), ("test_native_devices", ["--physical-gate-authority"]),
        ("test_native_devices", ["--lifecycle-deadline"])],
    "recognition": [("test_native_author", ["--" + flag]) for flag in
        ("pressure-contract", "builtin-resource-closure", "healing-effect-contract")] +
        [("test_native_ocr", ["--ctc-contract"]),
         ("test_native_ocr", ["--result-contract", ROOT / "resources/ocr/en_us"]),
         ("test_native_ocr", ["--run-model-contract", ROOT / "resources/ocr/en_us"]),
         ("test_strategy_frequency", ["--configuration-contract"]),
         ("test_strategy_frequency", ["--semantics"]),
         ("test_strategy_frequency", ["--fallback-contract"]),
         ("test_native_author", ["--leaf-plan-contract", "{output}/leaf-plan"]),
         ("test_native_author", ["--supply-plan-contract", "{pack}", "{output}/supply-plan"])],
    "app": [("test_native_application", ["--measurement-contract"]),
        ("test_native_application", ["--builtin-transaction", "{output}/builtin-transaction"]),
        ("test_native_application", ["--submission-history", "{output}/submission-history"]),
        ("test_native_application", ["--repository-read-oom", "{output}/repository-read-oom"]),
        ("test_native_application", ["--control-contract", "{pack}", "{pack}/parameters/legacy-quests.json", "{output}/control"]),
        ("test_native_publication", ["{output}/publication"]),
        ("test_native_coordinator", ["--combat-diagnostic"]),
        ("test_native_coordinator", ["--diagnostic-identity"]),
        ("test_native_coordinator", ["--instance-exit"]),
        ("test_native_coordinator", ["--timing-quota"]),
        ("test_native_coordinator", ["--event-history"])],
    "tools": [], "web": [],
}

def materialize_pack(output):
    """Freeze real declared bytes in an isolated pack; never fill a missing leaf with fake data."""
    origin = ROOT / "packs/wvd"
    target = output / "fixture-pack"
    manifest = json.loads((origin / "manifest.json").read_text(encoding="utf-8"))
    for row in manifest["files"]:
        relative = Path(row["path"])
        if relative.is_absolute() or ".." in relative.parts:
            raise RuntimeError("CONTRACT_RESOURCE_PATH_INVALID")
        source = origin / relative
        if not source.is_file():
            source = ROOT.parent / row["source"]
        if not source.resolve().is_relative_to(ROOT.parent) or source.is_symlink() or not source.is_file():
            raise RuntimeError("CONTRACT_REAL_RESOURCE_MISSING:" + row["path"])
        with source.open("rb") as stream:
            digest = hashlib.file_digest(stream, "sha256").hexdigest()
        if digest != row["sha256"] or source.stat().st_size != row["bytes"]:
            raise RuntimeError("CONTRACT_RESOURCE_CHANGED:" + row["path"])
        destination = target / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
        with destination.open("rb") as stream:
            if hashlib.file_digest(stream, "sha256").hexdigest() != digest:
                raise RuntimeError("CONTRACT_RESOURCE_COPY_CHANGED")
    shutil.copyfile(origin / "manifest.json", target / "manifest.json")
    for source in (origin / "parameters").glob("*.json"):
        destination = target / "parameters" / source.name
        if destination.exists():
            if destination.read_bytes() != source.read_bytes():
                raise RuntimeError("CONTRACT_PARAMETER_CHANGED")
        else:
            shutil.copyfile(source, destination)
    return target

def select_modules(changed):
    chosen = set()
    for path in changed:
        if path.startswith("next/native/runtime/") or path.startswith("next/native/workflow/"):
            chosen.update(("runtime", "app"))
        elif path.startswith("next/native/devices/"): chosen.update(("devices", "runtime", "app"))
        elif path.startswith(("next/native/recognition/", "next/native/games/", "next/third_party/")):
            chosen.update(("recognition", "runtime", "app"))
        elif path.startswith(("next/native/app/", "next/native/api/", "next/native/storage/", "next/native/platform/")):
            chosen.update(("app", "devices", "runtime"))
        elif path.startswith("next/web/"): chosen.update(("web", "app"))
        elif path in {"next/tools/prepare_native.py", "next/tools/dependencies.py",
                      "next/tools/dependency_tree.py", "next/tools/verify_build_dependencies.py",
                      "next/tools/resource_generation.py", "next/tools/package_functional.py",
                      "next/tools/run_contracts.py"}:
            chosen.update(CASES)
        elif path.startswith(("next/tools/", "next/tests/tools/", "next/tests/test_")): chosen.add("tools")
        elif path.startswith("next/tests/native/"): chosen.update(("runtime", "recognition", "devices", "app"))
        elif path.startswith("next/") or path.startswith(".github/workflows/native-contracts"):
            chosen.update(CASES)
    return chosen

def changed_paths(base):
    repo = ROOT.parent
    modified = subprocess.check_output(["git", "diff", "--name-only", "-z", base, "--"],
                                       cwd=repo, timeout=20)
    untracked = subprocess.check_output(["git", "ls-files", "--others", "--exclude-standard", "-z",
                                       "--", "next", ".github/workflows/native-contracts.yml"],
                                      cwd=repo, timeout=20)
    return sorted({item.decode("utf-8", errors="surrogateescape")
                   for item in (modified + untracked).split(b"\0") if item})

def execute(modules, output):
    output = output.absolute()
    if output.exists() or not output.is_relative_to(ROOT / ".local"):
        raise RuntimeError("NEW_ISOLATED_ROOT_UNDER_NEXT_LOCAL_REQUIRED")
    output.mkdir(parents=True)
    frozen = source_identity()
    cases = [(module, target, args) for module in CASES if module in modules for target, args in CASES[module]]
    targets = sorted({target for _, target, _ in cases})
    definitions = (ROOT / "native/CMakeLists.txt").read_text(encoding="utf-8")
    for target in targets:
        if not re.search(r"add_executable\(\s*" + re.escape(target) + r"\b", definitions) and not (
            target in {"test_native_recognition", "test_native_ocr"} and
            "foreach(TEST_TARGET IN ITEMS test_native_recognition test_native_ocr)" in definitions):
            raise RuntimeError("CONTRACT_TARGET_NOT_REGISTERED:" + target)
    if targets:
        try:
            build_targets = ["audit_correctness_targets"] if {"app","runtime","devices","recognition"} <= modules else targets
            run("contract-build", [cmake_path(), "--build", "--preset", "windows-release", "--target", *build_targets])
        except Exception as error:
            (output / "receipt.json").write_text(json.dumps({"source": frozen, "binaries": {},
                "modules": sorted(modules), "passed": [], "complete": False, "build_error": str(error),
                "device_actions": False, "data_root": str(output)}, indent=2), encoding="utf-8")
            raise
    def binary_hash(target):
        directory = ROOT / ("build/Release" if target in {"automationd","test_head_response"} else "build/native/Release")
        with (directory / (target + ".exe")).open("rb") as source:
            return hashlib.file_digest(source, "sha256").hexdigest()
    identity_targets = targets + (["automationd", "test_head_response"] if targets and
        {"app","runtime","devices","recognition"} <= modules else [])
    receipt = {"source": frozen, "binaries": {}, "modules": sorted(modules), "passed": [],
               "data_root": str(output), "device_actions": False, "complete": False}
    def proof(label, command, cwd=ROOT, timeout_seconds=120):
        run("contract-" + label, command, cwd, timeout_seconds)
        receipt["passed"].append(["tool-or-web", label])
    try:
        binaries = {target: binary_hash(target) for target in identity_targets}
        receipt["binaries"] = binaries
        if source_identity()["product_inputs_sha256"] != frozen["product_inputs_sha256"]:
            raise RuntimeError("CONTRACT_SOURCE_CHANGED_DURING_BUILD")
        pack = materialize_pack(output) if modules & {"app","recognition"} else ROOT / "packs/wvd"
        for module, target, args in cases:
            flag = str(args[0]) if args and str(args[0]).startswith("--") else "base"
            run("contract-" + target + "-" + flag.lstrip("-"),
                [ROOT / "build/native/Release" / (target + ".exe"),
                 *[str(arg).replace("{output}", str(output)).replace("{pack}", str(pack)) for arg in args]], timeout_seconds=120)
            receipt["passed"].append([module, target, flag])
        if "tools" in modules:
            proof("tools", [sys.executable, "-m", "unittest", "discover", "-s", "tests",
                "-p", "test_*analysis.py"], timeout_seconds=120)
            proof("resource-tools", [sys.executable, "-m", "unittest", "discover", "-s", "tests",
                "-p", "test_build_resource_contract.py"], timeout_seconds=120)
            for pattern in ("test_diagnostic_io.py", "test_crash_plan_contract.py"):
                proof(pattern, [sys.executable, "-m", "unittest", "discover", "-s", "tests",
                    "-p", pattern], timeout_seconds=120)
            for script in ("verify_bounded_trace_input.ps1", "verify_measurement_endpoints.ps1", "verify_trace_guards.ps1"):
                proof(script, [shutil.which("pwsh.exe") or "pwsh.exe", "-NoProfile", "-File",
                    ROOT / "tests/tools" / script,
                    "-OutputRoot" if script == "verify_bounded_trace_input.ps1" else "-EvidenceRoot",
                    output / script.removesuffix(".ps1")],
                    timeout_seconds=120)
            proof("heap-restore", ["dotnet", "restore", ROOT / "tools/heap_analyzer/HeapAnalyzer.csproj",
                "--locked-mode", "--source", "https://api.nuget.org/v3/index.json",
                "--packages", ROOT / ".local/cache/nuget"], timeout_seconds=120)
            proof("heap-build", ["dotnet", "build", ROOT / "tools/heap_analyzer/HeapAnalyzer.csproj",
                "-c", "Release", "--no-restore"], timeout_seconds=120)
            heap_root = ROOT / "tools/heap_analyzer/bin/Release/net8.0"
            heap_identity = {}
            for name in ("HeapAnalyzer.exe", "HeapAnalyzer.dll", "HeapAnalyzer.deps.json", "HeapAnalyzer.runtimeconfig.json"):
                with (heap_root / name).open("rb") as source:
                    heap_identity[name] = hashlib.file_digest(source, "sha256").hexdigest()
            receipt["tool_binaries"] = heap_identity
            proof("heap-aggregation", [ROOT / "tools/heap_analyzer/bin/Release/net8.0/HeapAnalyzer.exe",
                "--aggregation-contract", output / "heap-aggregation"], timeout_seconds=120)
            for name, expected in heap_identity.items():
                with (heap_root / name).open("rb") as source:
                    if hashlib.file_digest(source, "sha256").hexdigest() != expected:
                        raise RuntimeError("CONTRACT_TOOL_BINARY_CHANGED:" + name)
        if "web" in modules:
            proof("web", [shutil.which("npm.cmd") or "npm.cmd", "run", "build"], ROOT / "web", 120)
        if source_identity()["product_inputs_sha256"] != frozen["product_inputs_sha256"]:
            raise RuntimeError("CONTRACT_SOURCE_CHANGED_DURING_VALIDATION")
        if binaries != {target: binary_hash(target) for target in identity_targets}:
            raise RuntimeError("CONTRACT_BINARY_CHANGED_DURING_VALIDATION")
        receipt["complete"] = True
    except Exception as error:
        receipt["validation_error"] = str(error)
        raise
    finally:
        (output / "receipt.json").write_text(json.dumps(receipt, indent=2), encoding="utf-8")

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    scope = parser.add_mutually_exclusive_group(required=True)
    scope.add_argument("--module", action="append", choices=sorted(CASES))
    scope.add_argument("--changed-from")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--plan", action="store_true")
    args = parser.parse_args()
    if args.changed_from:
        modules = select_modules(changed_paths(args.changed_from))
    else: modules = set(args.module)
    if args.plan:
        print(json.dumps({"modules": sorted(modules), "cases": {m: len(CASES[m]) for m in sorted(modules)}}))
    elif modules:
        if not args.output: raise RuntimeError("ISOLATED_OUTPUT_REQUIRED")
        execute(modules, args.output)
    else: print("No affected product modules; no native validation run.")

if __name__ == "__main__": main()
