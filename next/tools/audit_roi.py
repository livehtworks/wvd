"""只读盘点生产 ROI；几何检查不等于识图或实机验收。"""

import argparse
import json
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parents[1]


def png_size(path):
    with path.open("rb") as stream:
        header = stream.read(24)
    if header[:8] != b"\x89PNG\r\n\x1a\n" or header[12:16] != b"IHDR":
        raise ValueError("PNG_HEADER_INVALID:" + str(path))
    return list(struct.unpack(">II", header[16:24]))


def walk(value, pointer=""):
    if isinstance(value, dict):
        if "roi" in value:
            yield pointer, value
        for key, child in value.items():
            yield from walk(child, pointer + "/" + key.replace("~", "~0").replace("/", "~1"))
    elif isinstance(value, list):
        for index, child in enumerate(value):
            yield from walk(child, pointer + "/" + str(index))


def audit(workflow_root=None):
    manifest = json.loads((ROOT / "packs/wvd/manifest.json").read_text(encoding="utf-8"))
    aliases = manifest.get("aliases", {})
    rows, errors = [], []
    sources = [(ROOT / relative, relative, "builtin") for relative in
               ("resources/authoring/semantic-assets.json", "resources/authoring/public-flows.json")]
    if workflow_root is not None:
        if not workflow_root.is_dir():
            raise ValueError("WORKFLOW_ROOT_INVALID")
        sources.extend((path, "user-workflows/" + path.name, "user_readonly")
                       for path in sorted(workflow_root.glob("*.json")))
    for path, relative, owner in sources:
        document = json.loads(path.read_text(encoding="utf-8"))
        for pointer, value in walk(document):
            roi = value["roi"]
            row = {"file": relative, "pointer": pointer, "owner": owner, "mode": value.get("mode"),
                   "image": value.get("image"), "roi": roi, "checks": []}
            valid = isinstance(roi, list) and len(roi) == 4 and all(type(n) is int for n in roi)
            if valid:
                x, y, w, h = roi
                valid = x >= 0 and y >= 0 and w > 0 and h > 0 and x + w <= 900 and y + h <= 1600
            if not valid:
                row["checks"].append("ROI_INVALID")
            if value.get("image"):
                name = value["image"]
                name += "" if name.endswith(".png") else ".png"
                name = aliases.get(name, name)
                image = ROOT / "packs/wvd/image" / name
                if not image.is_file():
                    row["checks"].append("ASSET_UNRESOLVED")
                else:
                    width, height = png_size(image)
                    scale = value.get("scale", 1)
                    width, height = round(width * scale), round(height * scale)
                    if "crop" in value:
                        width, height = value["crop"][2:]
                    row["template_size"] = [width, height]
                    if valid:
                        row["travel_px"] = [roi[2] - width, roi[3] - height]
                        if min(row["travel_px"]) < 0:
                            row["checks"].append("TEMPLATE_EXCEEDS_ROI")
                        elif min(row["travel_px"]) < 32:
                            row["checks"].append("TIGHT_REVIEW_REQUIRED")
            if any(c != "TIGHT_REVIEW_REQUIRED" for c in row["checks"]):
                errors.append({"file": relative, "pointer": pointer, "checks": row["checks"]})
            rows.append(row)
    # 原生中的计算ROI不能靠正则证明合法；逐行列出来源，交给调用链审查。
    # 包含内部采样、显式传参、ROI覆盖和声明；不冒充C++ AST或有效条件数量。
    native = []
    for path in sorted((ROOT / "native").rglob("*")):
        if path.suffix not in (".cpp", ".hpp"):
            continue
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            if '"roi"' in line or re.search(r"cv::Rect\s+(?:area|field|main|bounds)\b", line):
                native.append({"file": path.relative_to(ROOT).as_posix(), "line": number,
                               "source": line.strip()})
    return {"scope": "static_inventory_not_runtime_acceptance", "json_rois": rows,
            "native_locations": native, "errors": errors}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True, help="本轮专用诊断文件，不写配置或资源")
    parser.add_argument("--workflow-root", type=Path, help="可选：只读检查用户保存的流程，不递归读取运行日志")
    args = parser.parse_args()
    report = audit(args.workflow_root)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    tight = sum("TIGHT_REVIEW_REQUIRED" in row["checks"] for row in report["json_rois"])
    print(f"JSON ROI={len(report['json_rois'])}, 原生来源行={len(report['native_locations'])}, "
          f"几何/资产错误={len(report['errors'])}, 紧边界待核对={tight}; 非实机验收")
    for error in report["errors"][:10]:
        print(json.dumps(error, ensure_ascii=False))
    return bool(report["errors"])


if __name__ == "__main__":
    raise SystemExit(main())
