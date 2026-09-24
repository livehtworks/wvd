"""只读单个离线用例的识别耗时；嵌套证据去重不代表完整探针次数。"""
import argparse
import json
from pathlib import Path


def inspect(case: Path):
    files = list((case / "run").glob("*/*/result.json"))
    if len(files) != 1:
        raise ValueError("expected exactly one saved run result in this case")
    events = json.loads(files[0].read_text(encoding="utf-8"))["events"]["events"]
    rows = []
    captured = None
    for event in events:
        if event["type"] == "frame.captured":
            captured = event
        if event["type"] != "recognition.custom" or event["payload"].get("integrity_boundary") != "shared_direct":
            continue
        leaves = {}
        pending = [event["payload"]]
        while pending:
            value = pending.pop()
            if isinstance(value, dict):
                if "timing_ms" in value and "best_score" in value:
                    leaves[json.dumps(value, sort_keys=True)] = value["timing_ms"]
                pending.extend(value.values())
            elif isinstance(value, list):
                pending.extend(value)
        times = list(leaves.values())
        rows.append({
            "seq": event["seq"], "outcome": event["payload"].get("outcome"),
            "target": event["payload"].get("target"),
            "frame_id": captured["payload"]["frame_id"] if captured else None,
            "frame_age_ms": round((event["monotonic_time"] - captured["monotonic_time"]) / 1e6, 3) if captured else None,
            "distinct_retained_match_records": len(times),
            "retained_ms": {key: round(sum(v[key] for v in times), 3) for key in ("match", "sanitize", "reduce")},
            "slowest_retained_ms": sorted(times, key=lambda v: sum(v.values()), reverse=True)[:3],
        })
    return {"case": case.name, "note": "ordered classifiers may omit unmatched probe details; retained totals are not whole-call timings", "direct_calls": rows[-8:]}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("case", type=Path)
    args = parser.parse_args()
    print(json.dumps(inspect(args.case), ensure_ascii=False, indent=2))
