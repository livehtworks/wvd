"""Read recorded ownership boundaries; write a separate, bounded attribution report."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path


def read_json(path: Path) -> dict:
    if path.stat().st_size > 32 * 1024 * 1024:
        raise ValueError(f"INPUT_TOO_LARGE: {path.name}")
    with path.open(encoding="utf-8-sig") as stream:
        return json.load(stream)


def ownership_boundaries(path: Path) -> dict:
    records: dict[tuple[str, str, str, str], dict] = {}
    complete = True
    rows = 0
    with path.open(encoding="utf-8-sig") as stream:
        for row in stream:
            rows += 1
            if rows > 1000000:
                complete = False
                break
            if "event=owner_boundary " not in row:
                continue
            values = dict(re.findall(r"([a-z_]+)=([^\s]+)", row))
            key = tuple(values.get(field, "missing") for field in ("run", "generation", "kind", "owner"))
            if values.get("process_ok") != "1":
                records.setdefault(key, {})[values.get("phase", "missing")] = None
            else:
                records.setdefault(key, {})[values["phase"]] = int(values["private"])
    owners = []
    for key, phases in records.items():
        deltas = {}
        for name in ("initialize", "destroy"):
            begin, end = phases.get(name + ".begin"), phases.get(name + ".end")
            deltas[name] = end - begin if begin is not None and end is not None else None
        owners.append(dict(zip(("run", "generation", "kind", "owner"), key)) | {
            "phases": phases, "process_private_delta": deltas,
            "interpretation": "paired process observations; not an exclusive allocation-stack total",
        })
    return {"complete": complete, "scanned_rows": rows, "owners": owners,
            "missing": [] if owners else ["object initialization/destruction boundaries"]}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--run-directory", type=Path)
    parser.add_argument("--census", type=Path)
    parser.add_argument("--recognition-log", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.run_directory is None and args.census is None:
        parser.error("provide --run-directory or --census")
    output = args.output.resolve()
    input_roots = [path.resolve() for path in (args.run_directory,) if path]
    input_files = [path.resolve() for path in (args.census, args.recognition_log) if path]
    if output in input_files or any(output == root or root in output.parents for root in input_roots):
        raise ValueError("OUTPUT_MUST_BE_SEPARATE_FROM_INPUT")
    if output.exists():
        raise ValueError("OUTPUT_ALREADY_EXISTS")
    report = {"schema": 1, "conclusion": "PARTIAL_ATTRIBUTION", "missing": [], "samples": []}
    if args.run_directory:
        path = args.run_directory / "diagnostics.jsonl"
        with path.open(encoding="utf-8-sig") as stream:
            for row in stream:
                event = json.loads(row)
                if event.get("category") != "memory":
                    continue
                report["samples"].append({"phase": event.get("type"), "generation": event.get("generation"),
                    "utc_ms": event.get("utc_ms"), "payload": event.get("payload")})
                if len(report["samples"]) > 10000:
                    raise ValueError("MEMORY_SAMPLE_LIMIT")
        if not any("object_lifetimes" in (item["payload"] or {}) for item in report["samples"]):
            report["missing"].append("historical run has no object lifetime census")
        boundaries = args.run_directory / "memory-lifecycle.json"
        if boundaries.exists():
            report["lifecycle"] = read_json(boundaries)
        else:
            report["missing"].append("memory-lifecycle.json")
    if args.census:
        census = read_json(args.census)
        report["census_sha256"] = hashlib.sha256(args.census.read_bytes()).hexdigest()
        report["census"] = census
        samples = census.get("samples", [])
        if samples:
            first, last = samples[0]["memory"], samples[-1]["memory"]
            report["post_release_net_private_change"] = last["private_bytes"] - first["private_bytes"]
        report["missing"].append("allocation stacks for allocator/external-runtime residual")
    log = args.recognition_log or (args.run_directory / "recognition-memory.log" if args.run_directory else None)
    if log and log.is_file():
        report["owner_boundaries"] = ownership_boundaries(log)
        report["missing"].extend(report["owner_boundaries"]["missing"])
    else:
        report["missing"].append("recognition owner-boundary log")
    report["measurement_rules"] = [
        "BundleLease content/capacity are held file-buffer bytes, separate from decoded pixels and OCR runtime",
        "Shared lease identities must be deduplicated before aggregating units or sessions",
        "Container estimates must not be presented as actual resident allocations",
        "Unmeasured memory remains unknown; release-time decrease does not prove absence of leakage",
    ]
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(report, stream, ensure_ascii=False, indent=2)
        stream.write("\n")
    print(f"PARTIAL_ATTRIBUTION: {output}; missing={len(report['missing'])}")


if __name__ == "__main__":
    main()
