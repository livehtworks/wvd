"""Prepare a bounded, UNARMED natural-crash plan from supplied identities only."""
import argparse
import json
from pathlib import Path


def prepare(identity):
    owners = identity.get("processes", [])
    if {p.get("role") for p in owners} != {"automationd", "capture_host", "emulator"} or len(owners) != 3:
        raise ValueError("THREE_DISTINCT_PROCESS_OWNERS_REQUIRED")
    seen = set()
    for owner in owners:
        key = (owner.get("pid"), owner.get("creation_time_utc"))
        if not isinstance(key[0], int) or key[0] <= 0 or not isinstance(key[1], str) or not key[1] or key in seen:
            raise ValueError("EXACT_PROCESS_CREATION_IDENTITY_REQUIRED")
        seen.add(key)
        if not owner.get("exe_path") or not owner.get("exe_version") or len(owner.get("exe_sha256", "")) != 64:
            raise ValueError("EXE_IDENTITY_REQUIRED")
    emulator = next(p for p in owners if p["role"] == "emulator")
    if not identity.get("instance_id") or not identity.get("modules"):
        raise ValueError("INSTANCE_AND_MODULE_IDENTITIES_REQUIRED")
    for module in identity["modules"]:
        if not module.get("path") or not module.get("version") or len(module.get("sha256", "")) != 64:
            raise ValueError("MODULE_IDENTITY_REQUIRED")
    return {
        "schema": 1, "armed": False, "root_cause": "UNRESOLVED", "identity": identity,
        "target": {"pid": emulator["pid"], "creation_time_utc": emulator["creation_time_utc"],
                   "instance_id": identity["instance_id"]},
        "limits": {"dump_count": 2, "total_dump_bytes": 512 * 1024 * 1024,
                   "duration_seconds": 1200, "stop_on_identity_change": True},
        "required_evidence": ["exception_code", "first_exception_parameter_fast_fail_subcode",
            "faulting_thread_stack", "module_list_and_symbols", "sdk_call_parameters",
            "input_receipts", "recovery_generation", "last_valid_frame_reference",
            "tool_restart_events_before_exit", "dump_sha256", "collector_exit_and_loss_status"],
        "activation_requirements": ["separate_user_authorization", "recheck_PID_creation_and_EXE_hash",
            "new_isolated_dump_directory", "collector_enforced_limits_and_cleanup",
            "preserve_tool_capture_host_emulator_memory_separately"],
        "sdk_change_allowed": False,
        "sdk_contract_gaps": ["vendor_buffer_ownership", "size_change_contract",
            "thread_affinity_and_call_order", "symbolizable_natural_crash_dump"],
        "known_buffer_cost": {"owner": "capture_host", "width": 900, "height": 1600,
                              "rgba_bytes": 5760000, "main_process_allocation": False},
        "signature_policy": "libRenderer, historical nvoglv64 and tool-requested restarts are distinct; 0xc0000409 alone is not a cause",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--identity", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.identity.stat().st_size > 1024 * 1024:
        raise ValueError("IDENTITY_BYTES_EXCEEDED")
    plan = prepare(json.loads(args.identity.read_text(encoding="utf-8")))
    root = Path(__file__).resolve().parents[1] / ".local"
    output = args.output.resolve()
    if not output.is_relative_to(root) or output.exists():
        raise ValueError("NEW_ISOLATED_OUTPUT_REQUIRED")
    output.mkdir(parents=True)
    (output / "unarmed-plan.json").write_text(json.dumps(plan, indent=2), encoding="utf-8")
    print("Prepared only; no process inspected, collector enabled, or SDK changed.")


if __name__ == "__main__":
    main()
