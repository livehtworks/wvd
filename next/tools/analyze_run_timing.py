"""只读汇总已有 RunStore 日志；主线程互斥时间不与 worker 或节点墙钟相加。"""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import re
import statistics
import sys


def node_name(value):
    try:
        return json.loads(value)[-1].get("native_node", value)
    except (TypeError, ValueError, KeyError, IndexError):
        return str(value)


def analyze(root):
    directories = sorted((p for p in root.iterdir() if p.is_dir() and p.name.isdecimal()), key=lambda p: int(p.name))
    if not directories:
        raise ValueError("NO_RUN_DATA")
    totals, counts = collections.Counter(), collections.Counter()
    nodes, input_results, phases = (collections.defaultdict(list) for _ in range(3))
    retries, faults, memory, durations, sources = [], [], [], [], []
    diagnostic_reasons, identities = collections.Counter(), set()
    for directory in directories:
        run = int(directory.name)
        data = {}
        def read_log(name):
            path = directory / name
            if not path.is_file():
                raise ValueError(f"MISSING_LOG:{run}/{name}")
            raw = path.read_bytes()
            sources.append({"run": run, "file": name, "sha256": hashlib.sha256(raw).hexdigest(), "bytes": len(raw)})
            data[name] = raw.decode("utf-8-sig")
        for name in ("run.json", "result.json"):
            read_log(name)
        run_info = json.loads(data["run.json"])
        result = json.loads(data["result.json"])
        if run_info["run_id"] != run or result["run_id"] != run:
            raise ValueError(f"RUN_ID_MISMATCH:{run}")
        definition = run_info["definition"]
        identity = (run_info["instance"], definition["program_revision"], definition["pack_revision"])
        if "executable_sha256" in definition:
            identity += (definition["executable_sha256"],)
        identities.add(identity)
        if len(identities) != 1:
            raise ValueError("MIXED_RUN_IDENTITIES")
        timing = result["diagnostics"]["action_timing"]
        if not timing.get("collected", True):
            raise ValueError(f"TIMING_COLLECTION_DISABLED:{run}")
        read_log("action-timing.jsonl")
        diagnostic_logs = result["diagnostics"].get("logs")
        if diagnostic_logs:
            memory_rows = []
            if diagnostic_logs.get("memory_collected"):
                read_log("diagnostics.jsonl")
                if not data["diagnostics.jsonl"].endswith("\n") or not diagnostic_logs["complete"]:
                    raise ValueError(f"INCOMPLETE_DIAGNOSTIC_LOG:{run}")
                diagnostic_lines = data["diagnostics.jsonl"].splitlines()
                if len(diagnostic_lines) != diagnostic_logs["rows"]:
                    raise ValueError(f"INCOMPLETE_DIAGNOSTIC_LOG:{run}")
                for line in diagnostic_lines:
                    row = json.loads(line)
                    if row.get("run_id") != run or row.get("instance_id") != run_info["instance"]:
                        raise ValueError(f"DIAGNOSTIC_IDENTITY_MISMATCH:{run}")
                    if row.get("type") == "session_owners_released" and row["payload"].get("process_memory_available"):
                        memory_rows.append(row["payload"]["private_bytes"] / 1024 / 1024)
            else:
                faults.append({"run": run, "type": "memory_collection_disabled"})
            basis = "session_owners_released"
        else:
            read_log("recognition-memory.log")
            memory_rows = [int(m.group(1)) / 1024 / 1024 for line in data["recognition-memory.log"].splitlines()
                           if (m := re.search(r"\bprivate=(\d+)\b", line))]
            basis = "legacy_recognition_sample"
        if memory_rows:
            sample = {"run": run, "first_mib": round(memory_rows[0], 3),
                      "last_mib": round(memory_rows[-1], 3), "peak_sample_mib": round(max(memory_rows), 3)}
            if diagnostic_logs:
                sample["basis"] = basis
            memory.append(sample)
        elif not diagnostic_logs or diagnostic_logs.get("memory_collected"):
            faults.append({"run": run, "type": "memory_samples_missing"})
        durations.append(result["business"]["elapsed_seconds"])
        if result["state"] != "Completed" or result.get("secondary_errors"):
            faults.append({"run": run, "type": "run_terminal", "state": result["state"], "reason": result.get("reason")})
        for session in result["sessions"]:
            perf = session["performance"]
            totals.update(perf["exclusive_ns"])
            counts.update(perf["counts"])
            totals["wall_ns"] += perf["wall_ns"]
            totals["unattributed_ns"] += perf["unattributed_ns"]
            phases[session["generation"]].append(perf["wall_ns"] / 1e9)
            if perf.get("timing_errors"):
                faults.append({"run": run, "type": "timing_errors", "count": perf["timing_errors"]})
        recovery_seen = set()
        for event in result["events"]["events"]:
            payload = event.get("payload", {})
            if event["type"] == "observation.recovery":
                key = (payload.get("started_at_ns"), payload.get("code"))
                if key not in recovery_seen:
                    faults.append({"run": run, "type": event["type"], "code": payload.get("code")})
                    recovery_seen.add(key)
            if event["type"].startswith("diagnostic."):
                diagnostic_reasons[payload.get("reason", payload.get("status", "unknown"))] += 1
        text = data["action-timing.jsonl"]
        if not text.endswith("\n"):
            raise ValueError(f"TRUNCATED_TIMING_LOG:{run}")
        rows = text.splitlines()
        timing = result["diagnostics"]["action_timing"]
        if len(rows) != timing["rows"] or not timing["complete"]:
            raise ValueError(f"INCOMPLETE_TIMING_LOG:{run}")
        for index, line in enumerate(rows, 1):
            try:
                row = json.loads(line)
            except ValueError as error:
                raise ValueError(f"INVALID_TIMING_JSON:{run}:{index}") from error
            if row["run_id"] != run or row["instance_id"] != run_info["instance"]:
                raise ValueError(f"TIMING_IDENTITY_MISMATCH:{run}:{index}")
            p = row["payload"]
            if row["type"] == "timing.segment":
                nodes[p["node_id"]].append(p)
            elif row["type"] == "input.result":
                name = node_name(p["source_path"])
                input_results[name].append(p["result_wait_ns"] / 1e9)
                if p["attempts"] > 1 or p["delivery_unknown"] or p["outcome"] != "confirmed":
                    retries.append({"run": run, "node": name, "attempts": p["attempts"],
                                    "outcome": p["outcome"], "seconds": round(p["result_wait_ns"] / 1e9, 3)})
            elif row["type"] == "input.attempt" and p.get("state") in ("rejected", "unresolved", "error"):
                faults.append({"run": run, "node": node_name(p["source_path"]), "type": "input." + p["state"], "detail": p.get("detail")})
    node_rows = [{"node": name, "segments": len(rows),
                  "per_round_seconds": round(sum(p["wall_ns"] for p in rows) / 1e9 / len(durations), 3),
                  "max_segment_seconds": round(max(p["wall_ns"] for p in rows) / 1e9, 3),
                  "captures": sum(p["counts"]["captures"] for p in rows),
                  "matches": sum(p["counts"]["actual_matches"] for p in rows),
                  "wait_per_round_seconds": round(sum(p["exclusive_ns"]["explicit_wait"] for p in rows) / 1e9 / len(durations), 3)}
                 for name, rows in nodes.items()]
    groups = collections.defaultdict(list)
    for row in retries:
        groups[row["node"]].append(row)
    return {"runs": len(durations), "seconds": {"mean": round(statistics.mean(durations), 3),
            "min": round(min(durations), 3), "max": round(max(durations), 3)},
            "phase_mean_seconds": {str(k): round(statistics.mean(v), 3) for k, v in phases.items()},
            "time_per_round_seconds": {k: round(v / 1e9 / len(durations), 3) for k, v in totals.items()},
            "counts_per_round": {k: round(v / len(durations), 2) for k, v in counts.items()},
            "top_nodes": sorted(node_rows, key=lambda p: p["per_round_seconds"], reverse=True)[:18],
            "slow_inputs": sorted([{"node": k, "count": len(v), "mean_seconds": round(statistics.mean(v), 3),
                                    "max_seconds": round(max(v), 3)} for k, v in input_results.items()],
                                   key=lambda p: p["mean_seconds"], reverse=True)[:12],
            "retry_groups": [{"node": k, "cases": len(v), "max_attempts": max(p["attempts"] for p in v),
                              "max_seconds": max(p["seconds"] for p in v), "runs": [p["run"] for p in v]} for k, v in groups.items()],
            "faults": faults, "diagnostic_reasons": dict(diagnostic_reasons), "memory_samples": memory,
            "provenance": {"inputs": sources, "run_identity": list(next(iter(identities))),
                           "binary_identity": (next(iter(identities))[3] if len(next(iter(identities))) > 3
                                               else None) or "NOT_RECORDED_IN_RUN_LOGS"}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runs-root", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    root, output = args.runs_root.resolve(), args.output_dir.resolve()
    if output == root or output.is_relative_to(root) or root.is_relative_to(output):
        raise ValueError("OUTPUT_MUST_BE_SEPARATE_FROM_INPUT")
    summary = analyze(root)
    output.mkdir(parents=True, exist_ok=True)
    destination = output / "run-timing-analysis.json"
    destination.write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Analyzed {summary['runs']} runs: {destination}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f"ANALYSIS_FAILED: {error}", file=sys.stderr)
        sys.exit(1)
