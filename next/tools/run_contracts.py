"""Module-scoped production contracts. No emulator, service, or game launch."""
import argparse
import hashlib
import json
import os
import stat
import uuid
import re
import shutil
import subprocess
import sys
from pathlib import Path
from build import ROOT, cmake_path, run
from package_functional import source_identity

ARTIFACT_TRASH_ROOT = Path(ROOT.anchor) / "trash/wvd-contract-artifacts"

FLOW_REGRESSIONS = (
    "recovery-recheck", "linkage-closure", "positive-loop-frame", "child-result-frame",
    "late-receipt", "restored-context-timer", "critical-recovery", "confirmed-result",
    "timeout-reclassification", "selection-race", "restart-during-boot", "tolerance",
)
CASES = {
    "runtime": [("test_native_flow", ["--" + flag]) for flag in
        (*FLOW_REGRESSIONS, "receipt-exception-safety", "exit-proof-causality", "input-effect-contract", "progress-contract",
         "unknown-input-manual-stop", "event-scope-copy-contract", "unknown-ancestor-reconnect-input")],
    "devices": [("test_native_devices", []), ("test_native_devices", ["--physical-gate-authority"]),
        ("test_native_devices", ["--input-context", "tests/fixtures/android-input/mumu15-input.txt"]),
        ("test_native_devices", ["--lifecycle-deadline"]),
        ("test_scrcpy_connection", ["{output}/scrcpy-connection"])],
    "recognition": [("test_native_author", ["--" + flag]) for flag in
        ("pressure-contract", "builtin-resource-closure", "healing-effect-contract", "target-chest-completion",
         "combat-open-repair", "single-auto-progress")] +
        [("test_native_author", ["--skill-level-contract", "{pack}", "{output}/skill-level"]),
         ("test_recognition_methods", ["--recorded-input-contract", "{pack}",
            "tests/fixtures/rescue-marker/cases.json", "{output}/rescue-marker"]),
         ("test_recognition_methods", ["--recorded-input-contract", "{pack}",
            "tests/fixtures/healing-harken/cases.json", "{output}/healing-harken"]),
         ("test_recognition_methods", ["--recorded-input-contract", "{pack}",
            "tests/fixtures/healing-reward/cases.json", "{output}/healing-reward"]),
         ("test_recognition_methods", ["--recorded-input-contract", "{pack}",
            "tests/fixtures/guild-boot/cases.json", "{output}/guild-boot"]),
         ("test_recognition_methods", ["--recorded-input-contract", "{pack}",
            "tests/fixtures/support-grid/cases.json", "{output}/support-grid"]),
         ("test_support_selection", ["--buff-frame", "tests/fixtures/support-grid/alice-two-buff-corners.png"]),
         ("test_support_selection", ["--friend-frame", "tests/fixtures/support-grid/alice-two-buff-corners.png"]),
         ("test_native_author", ["--public-download-retry", "{output}/public-download-retry"]),
         ("test_native_author", ["--shared-page-actions", "{output}/shared-page-actions"]),
         ("test_native_ocr", ["--ctc-contract"]),
         ("test_native_ocr", ["--result-contract", ROOT / "resources/ocr/en_us"]),
         ("test_native_ocr", ["--run-model-contract", ROOT / "resources/ocr/en_us"]),
         ("test_strategy_frequency", ["--configuration-contract"]),
         ("test_strategy_frequency", ["--semantics"]),
         ("test_strategy_frequency", ["--fallback-contract"]),
         ("test_native_author", ["--leaf-plan-contract", "{output}/leaf-plan"]),
         ("test_native_author", ["--supply-plan-contract", "{pack}", "{output}/supply-plan"])],
    "app": [("test_native_author", ["--event-stop-contract"]),
        ("test_native_author", ["--low-willpower-contract"]),
        ("test_native_application", ["--measurement-contract"]),
        ("test_native_application", ["--builtin-transaction", "{output}/builtin-transaction"]),
        ("test_native_application", ["--submission-history", "{output}/submission-history"]),
        ("test_native_application", ["--submission-readonly", "{pack}", "{pack}/parameters/legacy-quests.json", "{output}/submission-readonly"]),
        ("test_native_application", ["--preparation-ownership", "{pack}", "{pack}/parameters/legacy-quests.json", "{output}/preparation-ownership"]),
        ("test_native_application", ["--repository-read-oom", "{output}/repository-read-oom"]),
        ("test_native_application", ["--control-contract", "{pack}", "{pack}/parameters/legacy-quests.json", "{output}/control"]),
        ("test_native_publication", ["{output}/publication"]),
        ("test_native_coordinator", []),
        ("test_native_coordinator", ["--logging-boundary"]),
        ("test_native_coordinator", ["--recent-frame-quota", "{output}/recent-frame-quota"]),
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

def materialize_recorded_models(pack):
    models = json.loads((ROOT / "resources/recognition/ocr-models.json").read_text(encoding="utf-8"))["models"]
    for model in models.values():
        for name, member in model["files"].items():
            source = ROOT / member["source"]
            if hashlib.sha256(source.read_bytes()).hexdigest() != member["sha256"]:
                raise RuntimeError("CONTRACT_OCR_MODEL_CHANGED:" + str(source))
            target = pack / model["bundle_directory"] / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)


def materialize_preparation_models(pack):
    """Declare real OCR bytes in the disposable Application publication fixture."""
    if not pack.resolve().is_relative_to((ROOT / ".local").resolve()) or \
            pack.resolve() == (ROOT / "packs/wvd").resolve():
        raise RuntimeError("CONTRACT_PREPARATION_PACK_MUST_BE_ISOLATED")
    materialize_recorded_models(pack)
    manifest_path = pack / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    members = {row["path"]: row for row in manifest["files"]}
    models = json.loads((ROOT / "resources/recognition/ocr-models.json").read_text(encoding="utf-8"))["models"]
    for model in models.values():
        for name, member in model["files"].items():
            relative = model["bundle_directory"] + "/" + name
            target = pack / relative
            if relative in members and members[relative]["sha256"] != member["sha256"]:
                raise RuntimeError("CONTRACT_PREPARATION_MODEL_CONFLICT:" + relative)
            if relative not in members:
                members[relative] = {"path": relative, "sha256": member["sha256"],
                                     "source": member["source"], "bytes": target.stat().st_size,
                                     "source_refs": []}
    manifest["files"] = sorted(members.values(), key=lambda row: row["path"])
    manifest.setdefault("source_revision", manifest["revision"])
    identity = {"files": manifest["files"], "aliases": manifest.get("aliases", {})}
    manifest["revision"] = hashlib.sha256(json.dumps(identity, sort_keys=True,
        ensure_ascii=False, separators=(",", ":")).encode("utf-8")).hexdigest()
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def select_modules(changed):
    chosen = set()
    for path in changed:
        if path.startswith(("docs/", "next/docs/")) or Path(path).name in ("README.md", "AGENTS.md"):
            continue
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

def atomic_artifact_json(path, value):
    """Flush a new sibling before atomic replacement; directory power-loss durability is not guaranteed."""
    temporary = path.with_name(path.name + "." + uuid.uuid4().hex + ".tmp")
    try:
        with temporary.open("x", encoding="utf-8", newline="\n") as stream:
            json.dump(value, stream, indent=2)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if temporary.exists():
            temporary.unlink()  # Only this call's exclusive, reconstructible temporary.


def measure_contract_artifacts(source, max_files=65536, max_member_bytes=1024, max_total_bytes=65536 * 1024):
    """Bound enumeration and reads in an exclusively owned output with no concurrent writers."""
    inventory_rows, samples = [], []
    size = 0
    sample_names = {hashlib.sha256(name.encode()).hexdigest() + ".json"
                    for name in ("history-0", "65536th")}
    with os.scandir(source) as entries:
        for entry in entries:
            if len(inventory_rows) >= max_files:
                raise RuntimeError("CONTRACT_ARTIFACT_FILE_COUNT_LIMIT")
            member = entry.stat(follow_symlinks=False)
            if (not stat.S_ISREG(member.st_mode) or
                getattr(member, "st_file_attributes", 0) & 0x400):
                raise RuntimeError("CONTRACT_ARTIFACT_REPARSE_OR_NONREGULAR:" + entry.name)
            if not re.fullmatch(r"[0-9a-f]{64}\.json", entry.name):
                raise RuntimeError("CONTRACT_ARTIFACT_UNEXPECTED_MEMBER:" + entry.name)
            # Windows DirEntry metadata omits nlink; obtain this one field once per file.
            links = os.stat(entry.path, follow_symlinks=False).st_nlink if os.name == "nt" else member.st_nlink
            if links != 1 or member.st_size > max_member_bytes:
                raise RuntimeError("CONTRACT_ARTIFACT_MEMBER_NOT_OWNED_OR_BOUNDED:" + entry.name)
            with open(entry.path, "rb") as stream:
                data = stream.read(min(max_member_bytes, max_total_bytes - size) + 1)
            if len(data) > max_member_bytes or size + len(data) > max_total_bytes:
                raise RuntimeError("CONTRACT_ARTIFACT_BYTE_LIMIT:" + entry.name)
            if len(data) != member.st_size:
                raise RuntimeError("CONTRACT_ARTIFACT_MEMBER_CHANGED:" + entry.name)
            size += len(data)
            digest = hashlib.sha256(data).hexdigest()
            inventory_rows.append((entry.name, len(data), digest))
            if entry.name in sample_names:
                body = json.loads(data)
                if (body.get("schema") != 1 or body.get("receipt", {}).get("state") != "completed" or
                    hashlib.sha256(body["request_id"].encode()).hexdigest() + ".json" != entry.name):
                    raise RuntimeError("CONTRACT_ARTIFACT_SAMPLE_INVALID:" + entry.name)
                samples.append({"name": entry.name, "sha256": digest, "bytes": len(data), "record": body})
    inventory = hashlib.sha256()
    for name, length, digest in sorted(inventory_rows):
        inventory.update((name + "\0" + str(length) + "\0" + digest + "\n").encode())
    return len(inventory_rows), size, inventory.hexdigest(), samples, sample_names


def checked_artifact_path(path, directory=True):
    """Reject links/reparse ancestors before handling this run's owned fixture."""
    path = Path(os.path.abspath(path))
    for part in (*reversed(path.parents), path):
        if not os.path.lexists(part):
            continue
        info = part.lstat()
        if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & 0x400:
            raise RuntimeError("CONTRACT_ARTIFACT_REPARSE:" + str(part))
        if part != path and not stat.S_ISDIR(info.st_mode):
            raise RuntimeError("CONTRACT_ARTIFACT_ANCESTOR_NOT_DIRECTORY:" + str(part))
    info = path.lstat()
    if (not (stat.S_ISDIR(info.st_mode) if directory else stat.S_ISREG(info.st_mode)) or not info.st_ino):
        raise RuntimeError("CONTRACT_ARTIFACT_IDENTITY_INVALID:" + str(path))
    return {"device": info.st_dev, "file_id": info.st_ino}


def finish_contract_artifacts(output, receipt):
    """Move a successful boundary in this exclusive run; no concurrent writers are supported."""
    lifecycle = {"schema": 1, "owner": receipt["artifact_owner"], "source": receipt["source"],
                 "binaries": receipt["binaries"], "moves": [], "errors": [], "complete": False,
                 "persistence": "file-fsync-and-atomic-replace; directory power-loss durability not guaranteed",
                 "writer_assumption": "exclusive run output; no concurrent writers"}
    receipt["artifact_lifecycle"] = lifecycle
    journal = output / "artifact-lifecycle.json"
    def persist():
        atomic_artifact_json(journal, lifecycle)
    passed = ["app", "test_native_application", "--submission-history"] in receipt["passed"]
    if not passed:
        lifecycle["complete"] = True
        lifecycle["retained_reason"] = "submission-history did not complete successfully"
        persist()
        return
    source = output / "submission-history/boundaries/real-constant"
    move = {"source": str(source), "state": "retained"}
    lifecycle["moves"].append(move)
    try:
        # ROOT/.local containment is lexical and every existing ancestor is checked.
        relative = output.relative_to(ROOT / ".local")
        if not relative.parts or ".." in relative.parts:
            raise RuntimeError("CONTRACT_ARTIFACT_OUTPUT_NOT_OWNED")
        if (receipt["artifact_owner"]["output"] != str(output) or receipt["data_root"] != str(output) or
            checked_artifact_path(output) != receipt["artifact_owner"]["directory_identity"]):
            raise RuntimeError("CONTRACT_ARTIFACT_OUTPUT_IDENTITY_CHANGED")
        identity = checked_artifact_path(source)
        results = source.parent / "results.json"
        checked_artifact_path(results, directory=False)
        rows = json.loads(results.read_text(encoding="utf-8"))
        result = next(row for row in rows if row.get("case") == "real-constant-65535-65536-next-and-restart")
        status = result["status"]
        if status["read_only"] or status["count_limit"] != 65536:
            raise RuntimeError("CONTRACT_ARTIFACT_BOUNDARY_EVIDENCE_INVALID")
        count, size, inventory_digest, samples, sample_names = measure_contract_artifacts(source)
        if (count > 65536 or count != status["committed_count"] or
            {row["name"] for row in samples} != sample_names):
            raise RuntimeError("CONTRACT_ARTIFACT_MEASUREMENT_MISMATCH")
        destination = ARTIFACT_TRASH_ROOT / relative / "submission-history/boundaries/real-constant"
        # Creating parents is allowed only after all existing destination ancestors pass.
        ancestor = destination.parent
        while not os.path.lexists(ancestor):
            ancestor = ancestor.parent
        checked_artifact_path(ancestor)
        if os.path.lexists(destination):
            raise RuntimeError("CONTRACT_ARTIFACT_TRASH_CONFLICT:" + str(destination))
        destination.parent.mkdir(parents=True, exist_ok=True)
        checked_artifact_path(destination.parent)
        if destination.parent.stat().st_dev != source.stat().st_dev:
            raise RuntimeError("CONTRACT_ARTIFACT_CROSS_VOLUME")
        move.update({"destination": str(destination), "directory_identity": identity,
                     "file_count": count, "logical_bytes": size, "inventory_sha256": inventory_digest,
                     "samples": samples, "results_sha256": hashlib.sha256(results.read_bytes()).hexdigest(),
                     "restore": {"from": str(destination), "to": str(source)}, "state": "prepared"})
        persist()
        if (checked_artifact_path(output) != receipt["artifact_owner"]["directory_identity"] or
            checked_artifact_path(source) != identity):
            raise RuntimeError("CONTRACT_ARTIFACT_IDENTITY_CHANGED")
        source.rename(destination)
        move["state"] = "moved"
        if (checked_artifact_path(output) != receipt["artifact_owner"]["directory_identity"] or
            checked_artifact_path(destination) != identity):
            raise RuntimeError("CONTRACT_ARTIFACT_MOVED_IDENTITY_CHANGED")
        lifecycle["complete"] = True
    except Exception as error:
        lifecycle["errors"].append(str(error))
        move["residual_path"] = str(source if source.exists() else Path(move.get("destination", source)))
    persist()


def execute(modules, output):
    output = Path(os.path.abspath(output))
    if output.exists() or not output.is_relative_to(ROOT / ".local"):
        raise RuntimeError("NEW_ISOLATED_ROOT_UNDER_NEXT_LOCAL_REQUIRED")
    ancestor = output.parent
    while not os.path.lexists(ancestor):
        ancestor = ancestor.parent
    checked_artifact_path(ancestor)
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
            failed_receipt = {"source": frozen, "binaries": {},
                "modules": sorted(modules), "passed": [], "complete": False, "build_error": str(error),
                "device_actions": False, "data_root": str(output),
                "artifact_lifecycle": {"complete": True, "moves": [], "errors": [],
                    "retained_reason": "build failed before any cases ran"}}
            try:
                atomic_artifact_json(output / "receipt.json", failed_receipt)
            except Exception as receipt_error:
                print("CONTRACT_BUILD_RECEIPT_WRITE_FAILED:" + str(receipt_error) +
                      "; original_build_error=" + str(error), file=sys.stderr)
            raise
    def binary_hash(target):
        directory = ROOT / ("build/Release" if target in {"automationd","test_head_response"} else "build/native/Release")
        with (directory / (target + ".exe")).open("rb") as source:
            return hashlib.file_digest(source, "sha256").hexdigest()
    identity_targets = targets + (["automationd", "test_head_response"] if targets and
        {"app","runtime","devices","recognition"} <= modules else [])
    receipt = {"source": frozen, "binaries": {}, "modules": sorted(modules), "passed": [],
               "data_root": str(output), "device_actions": False, "complete": False,
               "artifact_owner": {"id": str(uuid.uuid4()), "pid": os.getpid(), "output": str(output),
                                  "directory_identity": checked_artifact_path(output)},
               "artifact_budget": {"boundary_max_files": 65536, "boundary_max_bytes": 65536 * 1024},
               "owned_artifacts": [{"case": "--submission-history", "path": str(output /
                   "submission-history/boundaries/real-constant"), "disposition": "same-volume-trash-after-case-pass"}],
               "reproduction": {"command": [sys.executable, str(ROOT / "tools/run_contracts.py"),
                   *[item for module in sorted(modules) for item in ("--module", module)],
                   "--output", "<new-root-under-next/.local>"], "fresh_output_required": True}}
    def proof(label, command, cwd=ROOT, timeout_seconds=120):
        run("contract-" + label, command, cwd, timeout_seconds)
        receipt["passed"].append(["tool-or-web", label])
    # Establish this run's owner and exact artifact scope before creating fixtures.
    atomic_artifact_json(output / "receipt.json", receipt)
    try:
        binaries = {target: binary_hash(target) for target in identity_targets}
        receipt["binaries"] = binaries
        if source_identity()["product_inputs_sha256"] != frozen["product_inputs_sha256"]:
            raise RuntimeError("CONTRACT_SOURCE_CHANGED_DURING_BUILD")
        pack = materialize_pack(output) if modules & {"app","recognition"} else ROOT / "packs/wvd"
        if any("--recorded-input-contract" in args for _, _, args in cases):
            materialize_recorded_models(pack)
        preparation_pack = pack
        if any("--preparation-ownership" in args for _, _, args in cases):
            # Recorded-frame tests declare OCR members themselves. Keep their
            # original manifest and give Application its own complete manifest.
            preparation_pack = output / "preparation-fixture-pack"
            shutil.copytree(pack, preparation_pack)
            materialize_preparation_models(preparation_pack)
        for module, target, args in cases:
            flag = str(args[0]) if args and str(args[0]).startswith("--") else "base"
            case_pack = preparation_pack if flag == "--preparation-ownership" else pack
            # The true persisted 65,536-row boundary includes NTFS creation and
            # two bounded startup scans; keep it in the official app contract.
            case_timeout = 600 if flag in ("--submission-history", "--preparation-ownership") else 120
            run("contract-" + target + "-" + flag.lstrip("-"),
                [ROOT / "build/native/Release" / (target + ".exe"),
                 *[str(arg).replace("{output}", str(output)).replace("{pack}", str(case_pack)) for arg in args]], timeout_seconds=case_timeout)
            receipt["passed"].append([module, target, flag])
        if "tools" in modules:
            proof("tools", [sys.executable, "-m", "unittest", "discover", "-s", "tests",
                "-p", "test_*analysis.py"], timeout_seconds=120)
            proof("resource-tools", [sys.executable, "-m", "unittest", "discover", "-s", "tests",
                "-p", "test_build_resource_contract.py"], timeout_seconds=120)
            for pattern in ("test_contract_artifact_lifecycle.py", "test_diagnostic_io.py", "test_crash_plan_contract.py", "test_service_binding_contract.py", "test_tool_environment.py"):
                proof(pattern, [sys.executable, "-m", "unittest", "discover", "-s", "tests",
                    "-p", pattern], timeout_seconds=150 if pattern == "test_service_binding_contract.py" else 120)
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
            heap_root = ROOT / "build/dotnet/bin/HeapAnalyzer/Release/net8.0"
            heap_identity = {}
            for name in ("HeapAnalyzer.exe", "HeapAnalyzer.dll", "HeapAnalyzer.deps.json", "HeapAnalyzer.runtimeconfig.json"):
                with (heap_root / name).open("rb") as source:
                    heap_identity[name] = hashlib.file_digest(source, "sha256").hexdigest()
            receipt["tool_binaries"] = heap_identity
            proof("heap-aggregation", [heap_root / "HeapAnalyzer.exe",
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
        # Preserve the validation receipt before any artifact movement; failures stay separate.
        receipt["validation_complete"] = receipt["complete"]
        def persist_receipt():
            try:
                atomic_artifact_json(output / "receipt.json", receipt)
            except Exception as error:
                receipt.setdefault("artifact_receipt_errors", []).append(str(error))
                print("CONTRACT_ARTIFACT_RECEIPT_WRITE_FAILED:" + str(error), file=sys.stderr)
        persist_receipt()
        # Never move fixtures if their pre-move validation receipt could not be saved.
        if not receipt.get("artifact_receipt_errors"):
            try:
                finish_contract_artifacts(output, receipt)
            except Exception as cleanup_error:
                receipt["artifact_cleanup_error"] = str(cleanup_error)
        cleanup_failed = (receipt.get("artifact_cleanup_error") or receipt.get("artifact_receipt_errors") or
                          receipt.get("artifact_lifecycle", {}).get("errors"))
        receipt["complete"] = receipt["validation_complete"] and not bool(cleanup_failed)
        persist_receipt()
        cleanup_failed = cleanup_failed or receipt.get("artifact_receipt_errors")
        if cleanup_failed and "validation_error" not in receipt:
            raise RuntimeError("CONTRACT_ARTIFACT_CLEANUP_FAILED:" + str(cleanup_failed))

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
