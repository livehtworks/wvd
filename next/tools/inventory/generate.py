"""从固定 Git 基线生成完整盘点；不加载用户配置，也不执行旧 Python。"""

import ast
from collections import Counter
import hashlib
import json
from pathlib import Path
import subprocess

from ast_scan import Scanner, config_rows, expr, resolve_calls
from assets import scan as scan_assets
from mapping import BASELINE, record

NEXT = Path(__file__).resolve().parents[2]
REPO = NEXT.parent
FILES = [
    "src/script.py",
    "src/gui.py",
    "src/main.py",
    "src/utils.py",
    "src/auto_updater.py",
]


def git(*args):
    return subprocess.check_output(["git", "-C", str(REPO), *args])


def content(path):
    return git("show", BASELINE + ":" + path)


def write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )


def flatten(value, pointer=""):
    if isinstance(value, dict) and value:
        for k, v in value.items():
            yield from flatten(
                v, pointer + "/" + k.replace("~", "~0").replace("/", "~1")
            )
    elif isinstance(value, list) and value:
        for i, v in enumerate(value):
            yield from flatten(v, pointer + "/" + str(i))
    else:
        yield pointer, value


def generate():
    paths = (
        git("ls-tree", "-r", "--name-only", "-z", BASELINE).decode().split("\0")[:-1]
    )
    scans = []
    configs = []
    models = []
    source_hashes = {}
    differences = []
    for file in FILES:
        raw = content(file)
        text = raw.decode("utf-8-sig")
        tree = ast.parse(text)
        source_hashes[file] = hashlib.sha256(raw).hexdigest()
        if (
            hashlib.sha256((REPO / file).read_bytes()).hexdigest()
            != source_hashes[file]
        ):
            differences.append(file)
        scanner = Scanner(file, text)
        scanner.visit(tree)
        scans.append(scanner)
        configs += config_rows(tree, file)
        for node in ast.walk(tree):
            if isinstance(node, ast.ClassDef):
                row = record(
                    f"model:{file}:{node.name}:{node.lineno}",
                    "model",
                    ast.get_docstring(node) or "类/枚举数据结构，完整保留字段定义",
                    f"{file}:{node.lineno}",
                    file,
                    node.name,
                )
                row.update(
                    bases=[expr(b) for b in node.bases],
                    fields=[
                        expr(s)
                        for s in node.body
                        if isinstance(s, (ast.Assign, ast.AnnAssign))
                    ],
                )
                models.append(row)
            elif isinstance(node, ast.Lambda):
                row = record(
                    f"lambda:{file}:{node.lineno}:{node.col_offset}",
                    "lambda",
                    "回调表达式，保持求值和调用顺序",
                    f"{file}:{node.lineno}",
                    file,
                    "lambda",
                )
                row["expression"] = expr(node)
                models.append(row)
    functions = [r for s in scans for r in s.functions]
    calls = [r for s in scans for r in s.calls]
    resolve_calls(calls, functions)
    quest_raw = content("resources/quest/quest.json")

    # 重复 JSON key 会让普通 json.loads 静默覆盖，盘点阶段必须阻止这种证据损失。
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError("Duplicate quest JSON key: " + key)
            result[key] = value
        return result

    quests = json.loads(quest_raw.decode("utf-8-sig"), object_pairs_hook=unique)
    tasks = []
    fields = []
    for key, value in quests.items():
        row = record(
            "task:" + key,
            "task",
            value.get("questName", key),
            "resources/quest/quest.json#/" + key,
            destination=("native/games/wvd/tasks", "WvdQuestCatalog", "M4-TASK-" + key),
        )
        row.update(
            task_id=key,
            task_type=value.get("_TYPE"),
            data=value,
            task_title=value.get("questName", key),
        )
        tasks.append(row)
        for pointer, item in flatten(value, "/" + key):
            field = record(
                "quest-field:" + pointer,
                "quest_field",
                "保留任务字段原值、动作顺序与层次",
                "resources/quest/quest.json#" + pointer,
                destination=(
                    "packs/wvd/parameters",
                    "WvdQuestDefinition",
                    "M4-TASK-" + key,
                ),
            )
            field.update(task_id=key, pointer=pointer, value=item)
            fields.append(field)
    assets = scan_assets(paths, [c for s in scans for c in s.template_calls], quests)
    runtime = []
    for s in scans:
        for f in s.runtime_fields:
            row = record(
                "runtime:" + f["source_ref"] + ":" + f["field"],
                "runtime_field",
                "运行/界面字段写入，不能一概当持久化配置",
                f["source_ref"],
                s.file,
                f["scope"],
            )
            row.update(f)
            runtime.append(row)
    operations = []
    for path in [
        "用于本地测试的打包脚本.bat",
        "requirements.txt",
        "requirements-build.txt",
        "CHANGES_LOG.md",
        "LICENSE",
        "FORK_NOTICE.md",
    ]:
        raw = content(path)
        row = record(
            "operation:" + path,
            "operation",
            "保留独立构建、配置保护、依赖/许可证与更新约定",
            path,
            destination=("tools", "ReleaseTooling", "M6-PACKAGING"),
        )
        row["sha256"] = hashlib.sha256(raw).hexdigest()
        operations.append(row)
        source_hashes[path] = row["sha256"]
    asset_rows = []
    for path in paths:
        if not path.startswith(("resources/", "locale/")):
            continue
        row = record(
            "asset:" + path,
            "asset",
            "基线资源/语言文件，精确保留名称和内容 hash",
            path,
            destination=(
                "packs/wvd" if path.startswith("resources/") else "web/src/i18n",
                "LegacyAssetCatalog",
                "M3-ASSETS",
            ),
        )
        row.update(path=path, sha256=hashlib.sha256(content(path)).hexdigest())
        asset_rows.append(row)
    entries = [r for s in scans for r in s.entries]
    branches = [r for s in scans for r in s.branches]
    items = (
        functions
        + configs
        + tasks
        + fields
        + entries
        + branches
        + models
        + runtime
        + [r for s in scans for r in s.data_fields]
        + operations
        + asset_rows
    )
    # 唯一标识必须能承接多个同名 setter、同一行多个字段与重复事件注册。
    counts = Counter()
    for row in items:
        counts[row["id"]] += 1
        if counts[row["id"]] > 1:
            row["id"] += "#" + str(counts[row["id"]])
    assert len({r["id"] for r in items}) == len(items)
    required = (
        "original_semantics",
        "source_ref",
        "new_owner",
        "new_entry",
        "data_mapping",
        "acceptance_ids",
        "status",
    )
    assert all(all(r.get(k) for k in required) for r in items)
    report = {
        "schema_version": 1,
        "baseline": BASELINE,
        "source_hashes": source_hashes,
        "working_source_differences": differences,
        "counts": dict(Counter(r["kind"] for r in items)),
        "items": items,
        "calls": calls,
        "coverage": {
            "unmapped_items": 0,
            "baseline_function_count": sum(
                sum(
                    isinstance(n, (ast.FunctionDef, ast.AsyncFunctionDef))
                    for n in ast.walk(ast.parse(content(f)))
                )
                for f in FILES
            ),
            "registered_functions": len(functions),
            "task_ids": list(quests),
            "config_names": [r["name"] for r in configs],
        },
        "limitations": [
            "MAPPED_NOT_IMPLEMENTED 仅代表已登记迁移归属，不表示业务已实现或已验收。",
            "调用图为 AST 静态可解析部分；EXTERNAL_OR_DYNAMIC 保留表达式，不虚报运行时调用目标。",
            "分支保留源码条件及真假出口；没有执行生产任务探测可达性。",
            "只读基础任务与源码字段，未加载用户 config/mod。",
        ],
    }
    output = NEXT / "docs/migration"
    write(output / "feature_inventory.json", report)
    write(output / "asset_case_report.json", assets)
    public = NEXT / "web/public/migration"
    write(public / "feature_inventory.json", report)
    write(public / "asset_case_report.json", assets)
    # 目录只读资产预览，不注册为可执行 Maa 游戏包。
    for path in assets["images"]:
        target = NEXT / "web/public/reference-assets" / path[len("resources/images/") :]
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(content(path))
    print(
        json.dumps(
            {
                "counts": report["counts"],
                "static_calls": sum(r["resolution"] == "STATIC" for r in calls),
                "dynamic_or_external": sum(r["resolution"] != "STATIC" for r in calls),
                "asset_statuses": dict(
                    Counter(r["status"] for r in assets["references"])
                ),
                "source_differences": differences,
            },
            ensure_ascii=False,
        )
    )


if __name__ == "__main__":
    generate()
