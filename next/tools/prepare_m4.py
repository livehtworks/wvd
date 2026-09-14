"""从固定 Git 对象生成配置描述和任务数据；不导入旧模块、不访问用户配置。"""
import ast
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BASE = "6585f4075f5714ab522aa582993860c09af912c1"


def source(path):
    return subprocess.check_output(["git", "show", f"{BASE}:{path}"], cwd=ROOT.parent)


def literal(node):
    if isinstance(node, ast.Constant):
        return node.value
    if isinstance(node, (ast.List, ast.Tuple)):
        return [literal(x) for x in node.elts]
    if isinstance(node, ast.Dict):
        return {literal(k): literal(v) for k, v in zip(node.keys, node.values)}
    if isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and node.func.id == "_" and len(node.args) == 1:
        return literal(node.args[0])
    raise ValueError("Unsupported frozen default: " + ast.dump(node))


def prepare():
    script = source("src/script.py")
    table = next(n.value for n in ast.parse(script.decode("utf-8")).body if isinstance(n, ast.Assign)
                 and any(isinstance(t, ast.Name) and t.id == "CONFIG_VAR_LIST" for t in n.targets))
    types = {"tk.StringVar": "string", "tk.BooleanVar": "boolean", "tk.IntVar": "integer", "list": "array", "dict": "object"}
    fields = []
    for row in table.elts:
        category, name, kind, default = row.elts
        fields.append({"category": literal(category), "name": literal(name), "type": types[ast.unparse(kind)],
                       "default": literal(default), "source_ref": "src/script.py:" + str(row.lineno)})
    if len(fields) != 33:
        raise ValueError("CONFIG_BASELINE_COUNT_CHANGED")
    out = ROOT / "packs/wvd/parameters"
    out.mkdir(parents=True, exist_ok=True)
    (out / "legacy-config-fields.json").write_text(json.dumps({"schema": 1, "source_baseline": BASE,
         "source_sha256": hashlib.sha256(script).hexdigest(), "fields": fields}, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    quests = source("resources/quest/quest.json")
    parsed = json.loads(quests)
    if len(parsed) != 58 or sum(q["_TYPE"] == "quest" for q in parsed.values()) != 15:
        raise ValueError("TASK_BASELINE_COUNT_CHANGED")
    (out / "legacy-quests.json").write_bytes(quests)
    properties = {field["name"]: {"type": [field["type"], "null"] if field["default"] is None else field["type"]}
                  for field in fields}
    schema = {"$schema": "https://json-schema.org/draft/2020-12/schema", "title": "WVD 离线迁移配置副本",
              "type": "object", "required": ["schema", "revision", "values", "legacy_document",
                                                   "legacy_passthrough", "sources", "selected_section"],
              "properties": {"schema": {"const": 1}, "revision": {"type": "string", "pattern": "^[a-f0-9]{64}$"},
                  "values": {"type": "object", "required": list(properties), "properties": properties,
                             "additionalProperties": False},
                  "legacy_document": {"type": "object"}, "legacy_passthrough": {"type": "object"},
                  "sources": {"type": "object"}, "selected_section": {"type": "string"},
                  "last_business_update": {"type": "object",
                      "required": ["operation_id", "field", "before", "after", "frame_id", "generation"],
                      "properties": {"operation_id": {"type": "string", "minLength": 1},
                          "field": {"const": "KARMA_ADJUST"}, "before": {"type": "string"}, "after": {"type": "string"},
                          "frame_id": {"type": "integer", "minimum": 1}, "generation": {"type": "integer", "minimum": 1}},
                      "additionalProperties": False}},
              "$comment": "由固定旧源码生成；生产副本读写的类型和嵌套语义校验由 ProfileStore/LegacyConfigImporter 执行。"}
    (ROOT / "schemas/wvd-profile.schema.json").write_text(json.dumps(schema, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print("33 frozen config fields; 58 source tasks; no business execution.")


if __name__ == "__main__":
    prepare()
