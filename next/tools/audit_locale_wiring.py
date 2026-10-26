"""Read-only locale wiring inventory; literal references are not reachability proof."""
import argparse
import collections
import json
import re
import stat
from pathlib import Path


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def walk(value, path=""):
    yield path, value
    if isinstance(value, dict):
        for key, child in value.items():
            yield from walk(child, path + "/" + str(key))
    elif isinstance(value, list):
        for index, child in enumerate(value):
            yield from walk(child, path + "/" + str(index))


def latest_publication(runtime):
    """Select a request's program, never a shared/private directory.

    This is a structural audit selection, not a replacement for BundleLease's
    cryptographic layout validation. Invalid newest requests fail explicitly.
    """
    published = runtime / "published"
    if not published.is_dir():
        return None
    candidates = []
    for count, path in enumerate(published.iterdir(), 1):
        if count > 5000:
            raise RuntimeError("PUBLICATION_AUDIT_ENTRY_LIMIT")
        if path.name == "asset-objects" or not re.fullmatch(r"[A-Za-z0-9_-]+", path.name):
            continue
        info = path.lstat()
        if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & 0x400:
            raise RuntimeError("PUBLICATION_AUDIT_REPARSE:" + path.name)
        if stat.S_ISDIR(info.st_mode):
            candidates.append((info.st_mtime_ns, path.name, path))
    if not candidates:
        return None
    chosen = max(candidates)[2]
    for path, directory in ((chosen / "program", True), (chosen / "program/flow.json", False),
                            (chosen / "program/identity.json", False)):
        info = path.lstat()
        if (stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & 0x400 or
                not (stat.S_ISDIR(info.st_mode) if directory else stat.S_ISREG(info.st_mode))):
            raise RuntimeError("PUBLICATION_AUDIT_PROGRAM_INVALID:" + str(path))
    marker = chosen / "publication-layout.json"
    if marker.exists() or marker.is_symlink():
        info = marker.lstat()
        if not stat.S_ISREG(info.st_mode) or getattr(info, "st_file_attributes", 0) & 0x400:
            raise RuntimeError("PUBLICATION_AUDIT_LAYOUT_INVALID")
        layout = read(marker)
        if (not isinstance(layout, dict) or type(layout.get("schema")) is not int or layout["schema"] != 1 or
                layout.get("kind") != "shared_resource_set" or
                any(not re.fullmatch(r"[0-9a-f]{64}", str(layout.get(key, "")))
                    for key in ("resource_digest", "program_revision")) or
                read(chosen / "program/flow.json").get("revision") != layout["program_revision"]):
            raise RuntimeError("PUBLICATION_AUDIT_LAYOUT_INVALID")
    return chosen


def audit(root, runtime):
    catalogue_path = root / "next/resources/authoring/semantic-assets.json"
    catalogue = read(catalogue_path)
    languages = collections.defaultdict(set)
    for resource in catalogue["resources"].values():
        for language, variant in resource.get("variants", {}).items():
            for _, value in walk(variant["condition"]):
                if isinstance(value, dict) and isinstance(value.get("image"), str):
                    languages[value["image"]].add(language)
    for language, names in catalogue["template_languages"].items():
        for name in names:
            languages[name] = {language}

    assets = {p.relative_to(root / "next/packs/wvd/image").with_suffix("").as_posix()
              for p in (root / "next/packs/wvd/image").rglob("*.png")}
    source = (root / "next/native/games/wvd/tasks/locale_assets.cpp").read_text(encoding="utf-8")
    mappings = {}
    for block in ("legacy_assets", "legacy_observations"):
        match = re.search(block + r"\{(.*?)\n\};", source, re.S)
        if match:
            mappings.update(re.findall(r'\{"([^"]+)",\s*"([^"]+)"\}', match[1]))
    mappings["RiseAgain"] = "party.revival.page"

    def state(name, localized):
        name = name.removesuffix(".png")
        if localized and name in mappings:
            return "mapped_at_task_localization"
        if name not in languages:
            return "unclassified_allowed"
        if languages[name] & {"shared", "zh-Hant"}:
            return "language_allowed"
        return "excluded_in_zh_hant"

    rows = []
    for base in (root / "next/native", root / "src"):
        for path in sorted(base.rglob("*")):
            if path.suffix not in {".cpp", ".hpp", ".py"}:
                continue
            # String literals include resource tables/comments, not only executed conditions.
            for line_no, line in enumerate(path.read_text(encoding="utf-8-sig").splitlines(), 1):
                for name in set(re.findall(r'"([^"\n]+)"', line)) & (assets | set(languages)):
                    rel = path.relative_to(root).as_posix()
                    rows.append({"surface": "legacy" if rel.startswith("src/") else "native",
                                 "file": rel, "line": line_no, "image": name,
                                 "status": state(name, False),
                                 "task_mapping": mappings.get(name),
                                 "snippet": line.strip()[:260]})

    json_sets = [("authoring", root / "next/resources/authoring"),
                 ("pack_parameters", root / "next/packs/wvd/parameters")]
    if runtime:
        json_sets.append(("saved_workflows", runtime / "workflows"))
        published = latest_publication(runtime)
        if published:
            json_sets.append(("latest_published", published / "program"))
    json_counts = {}
    for surface, base in json_sets:
        files = sorted(base.rglob("*.json"))
        if surface == "latest_published":
            # identity.json repeats the program and packaging metadata, not another runtime.
            files = [p for p in files if p.name == "flow.json"]
        json_counts[surface] = len(files)
        for path in files:
            data = read(path)
            for pointer, value in walk(data):
                if isinstance(value, dict) and isinstance(value.get("image"), str):
                    name = value["image"]
                    rows.append({"surface": surface, "file": str(path.relative_to(root)),
                                 "pointer": pointer, "image": name,
                                 "mode": value.get("mode"), "status": state(name, False),
                                 "task_mapping": mappings.get(name)})
                elif path.name == "legacy-quests.json" and isinstance(value, str) and value in assets:
                    rows.append({"surface": "legacy_task_data", "file": str(path.relative_to(root)),
                                 "pointer": pointer, "image": value, "status": state(value, True),
                                 "task_mapping": mappings.get(value)})

    coverage = []
    for name, resource in catalogue["resources"].items():
        variants = resource.get("variants", {})
        coverage.append({"id": name, "role": resource.get("role"),
                         "variants": sorted(variants), "shared": "condition" in resource or "shared" in variants,
                         "zh_validation": variants.get("zh-Hant", {}).get("validation")})
    grouped = collections.defaultdict(list)
    for row in rows:
        grouped[(row["surface"], row["status"])].append(row)
    program_groups = []
    program_rows = [r for r in rows if r["surface"] == "latest_published"]
    for image in sorted({r["image"] for r in program_rows}):
        matches = [r for r in program_rows if r["image"] == image]
        program_groups.append({"image": image, "status": state(image, False),
                               "nodes": sorted({r["pointer"].split("/")[4] for r in matches
                                                if r["pointer"].startswith("/definitions/") and "/steps/" in r["pointer"]}),
                               "examples": [r["pointer"] for r in matches[:3]]})
    return {"scope": "Static inventory, not execution or image-language validation. Native literals may be localized later; implicit probes require manual tracing.",
            "summary": {"assets": len(assets), "classified_assets": len(assets & set(languages)),
                        "unclassified_assets": len(assets - set(languages)), "semantic_resources": len(coverage),
                        "json_files": json_counts,
                        "references": [{"surface": k[0], "status": k[1], "count": len(v),
                                        "unique_images": len({r['image'] for r in v})}
                                       for k, v in sorted(grouped.items())]},
            "language_index": {k: sorted(v) for k, v in sorted(languages.items())},
            "task_mappings": mappings, "semantic_coverage": coverage,
            "published_groups": program_groups, "references": rows}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    result = audit(root, args.runtime.resolve() if args.runtime else None)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result["summary"], ensure_ascii=False, indent=2))
