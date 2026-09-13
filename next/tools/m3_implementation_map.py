"""在固定静态盘点上叠加本轮实际实现，不修改原索引或任务迁移状态。"""

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DESTINATION = ROOT / "docs/migration/m3-implementation-map.json"


def generate():
    inventory = json.loads(
        (ROOT / "docs/migration/feature_inventory.json").read_text(encoding="utf-8")
    )
    manifest = json.loads(
        (ROOT / "packs/wvd/manifest.json").read_text(encoding="utf-8")
    )
    vision = "native/games/wvd/vision/recognizers.cpp"
    mappings = {
        "CutRoI": (vision, "match: roi/exclude", "V-ROI"),
        "NormalizeRoiCacheKey": (
            "native/maafw/gateway.cpp",
            "immutable JSON cache key",
            "V-ROI",
        ),
        "GetMatchCache": ("native/maafw/gateway.cpp", "RecognitionCache", "A-ERROR"),
        "_check": (vision, "template", "V-BASE"),
        "_check_bright_mask": (vision, "bright_mask", "V-MASK"),
        "CheckTemplateInRoi": (vision, "template + explicit roi/threshold", "V-ROI"),
        "CheckIf": (vision, "template + default_roi", "V-BASE"),
        "CheckHow": (vision, "evidence.best_score", "V-BASE"),
        "CheckIf_MultiRect": (vision, "multiple", "V-BASE"),
        "CheckIf_FocusCursor": (vision, "focus_cursor", "V-MAP"),
        "CheckIf_ReachPosition": (vision, "reached", "V-MAP"),
        "CheckIf_throughStair": (vision, "through_stair", "V-MAP"),
        "CheckIf_harkenStair": (vision, "harken_stair: pure recognition only", "V-MAP"),
        "CheckIf_fastForwardOff": (vision, "fast_forward_off", "V-MAP"),
        "WrapImage": (
            "native/games/wvd/vision/image_ops.hpp",
            "transform_rgb: multiply",
            "V-BASE",
        ),
        "MinusImage": (
            "native/games/wvd/vision/image_ops.hpp",
            "transform_rgb: subtract",
            "V-BASE",
        ),
        "TryReadPauseTextByOcr": (
            vision,
            "pause_ocr: explicit unavailable Error",
            "V-PAUSE-OCR",
        ),
        "CheckPauseTextLayout": (vision, "pause_layout", "V-PAUSE-LOGIC"),
        "GetPauseNegativeEvidence": (vision, "pause_negative", "V-PAUSE-LOGIC"),
        "CheckPauseOverlay": (
            vision,
            "pause: layout and negative evidence",
            "V-PAUSE-LOGIC",
        ),
        "StateCombatCheck": (vision, "combat_active", "V-BATTLE"),
        "CheckCombatTargetByTemplate": (
            vision,
            "template + roi + threshold",
            "V-NEXT-LOGIC",
        ),
        "CheckCombatNextTarget": (
            vision,
            "next: ordinary correlation then marker",
            "V-NEXT-LOGIC",
        ),
        "CheckCombatNextTargetLowConfidence": (
            vision,
            "next_low_confidence: diagnostic only",
            "V-NEXT-LOGIC",
        ),
        "CheckCombatTargetMarker": (vision, "target_marker", "V-NEXT-LOGIC"),
        "CheckRolePortraitMatch": (vision, "portrait", "V-BATTLE"),
        "Fishing_DetectBobber": (
            "native/games/wvd/vision/bobber.cpp",
            "bobber: pure detections, no drawing",
            "V-BASE",
        ),
        "SkillLvlSelectAndDoubleCheck": (
            vision,
            "skill_level: pure recognition only",
            "V-BATTLE",
        ),
    }
    entries = []
    for item in inventory["items"]:
        if item["kind"] != "function":
            continue
        name = item["legacy_symbol"].split(".")[-1]
        if name not in mappings:
            continue
        path, entry, acceptance = mappings[name]
        if not (ROOT / path).is_file():
            raise RuntimeError("映射目标不存在: " + path)
        partial = name in {"CheckIf_harkenStair", "SkillLvlSelectAndDoubleCheck"}
        ocr = name == "TryReadPauseTextByOcr"
        entries.append(
            {
                "inventory_id": item["id"],
                "source_ref": item["source_ref"],
                "legacy_symbol": item["legacy_symbol"],
                "source_body_sha256": item["body_sha256"],
                "implementation": path,
                "entry": entry,
                "implementation_status": (
                    "EXPLICITLY_UNAVAILABLE"
                    if ocr
                    else "PURE_PART_IMPLEMENTED" if partial else "IMPLEMENTED"
                ),
                "acceptance_id": acceptance,
                "evidence_report": "../m3-device-vision-validation.md",
                "quality_status": "UNVERIFIED_REAL_QUALITY",
                "production_enabled": False,
                "remaining": (
                    "旧 Tesseract 等价性未验证；不替换为 Maa OCR"
                    if ocr
                    else "完整调用者、动作与任务消费归 M4"
                ),
            }
        )
    required = {
        item["id"]
        for item in inventory["items"]
        if item["kind"] == "function" and "vision" in item["new_owner"]
    }
    if not required.issubset({e["inventory_id"] for e in entries}):
        raise RuntimeError("静态视觉族存在未登记项")
    result = {
        "schema": 1,
        "source_baseline": "6585f4075f5714ab522aa582993860c09af912c1",
        "static_inventory": "feature_inventory.json",
        "static_inventory_unchanged": True,
        "entries": entries,
        "resource_manifest": "../../packs/wvd/manifest.json",
        "resource_revision": manifest["revision"],
        "dynamic_references": manifest["dynamic_references"],
        "business_migration": "M4_NOT_STARTED",
        "production_switch": False,
        "notes": [
            "implementation_status 不是验收 PASS；逐族证据以报告为准。",
            "诊断日志、截图编码、时序恢复与卡死决策仍由后续完整业务链承接，不在纯视觉中点击或重启。",
        ],
    }
    DESTINATION.write_text(
        json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print(f"M3 implementation map: {len(entries)} entries")


if __name__ == "__main__":
    generate()
