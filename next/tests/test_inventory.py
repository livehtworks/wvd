"""独立遍历固定源码/JSON 核对覆盖率，不执行盘点器自身计数作为预期。"""

import ast
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import unittest

NEXT = Path(__file__).resolve().parents[1]
REPO = NEXT.parent
BASELINE = "6585f4075f5714ab522aa582993860c09af912c1"


def baseline(path):
    return subprocess.check_output(
        ["git", "-C", str(REPO), "show", BASELINE + ":" + path]
    )


class InventoryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.report = json.loads(
            (NEXT / "docs/migration/feature_inventory.json").read_text(encoding="utf-8")
        )
        cls.items = cls.report["items"]
        cls.trees = {
            file: ast.parse(baseline(file))
            for file in cls.report["source_hashes"]
            if file.endswith(".py")
        }

    def rows(self, kind):
        return [r for r in self.items if r["kind"] == kind]

    def test_all_functions_branches_and_lambdas(self):
        for kind, types in (
            ("function", (ast.FunctionDef, ast.AsyncFunctionDef)),
            ("branch", (ast.If,)),
            ("lambda", (ast.Lambda,)),
            ("model", (ast.ClassDef,)),
        ):
            expected = sorted(
                f"{file}:{n.lineno}"
                for file, tree in self.trees.items()
                for n in ast.walk(tree)
                if isinstance(n, types)
            )
            if kind == "branch":
                expected = sorted(
                    expected
                    + [
                        f"{file}:{case.pattern.lineno}"
                        for file, tree in self.trees.items()
                        for node in ast.walk(tree)
                        if isinstance(node, ast.Match)
                        for case in node.cases
                    ]
                )
            self.assertEqual(
                sorted(r["source_ref"] for r in self.rows(kind)), expected, kind
            )

    def test_task_and_queue_dispatch_patterns(self):
        expected = []
        for file, tree in self.trees.items():
            for node in ast.walk(tree):
                if isinstance(node, ast.Match):
                    expected.extend(
                        (
                            f"{file}:{case.pattern.lineno}",
                            ast.unparse(node.subject),
                            ast.unparse(case.pattern),
                            ast.unparse(case.guard) if case.guard else "",
                        )
                        for case in node.cases
                    )
        actual = [
            (r["source_ref"], r["subject"], r["pattern"], r["guard"])
            for r in self.rows("branch")
            if r.get("branch_type") == "match_case"
        ]
        self.assertEqual(sorted(expected), sorted(actual))
        tasks = {r["task_id"] for r in self.rows("task") if r["task_type"] == "quest"}
        dispatched = set()
        for row in self.rows("branch"):
            if row.get("subject") == "setting.FARM_TARGET":
                pattern = (
                    ast.parse("match target:\n    case " + row["pattern"] + ": pass")
                    .body[0]
                    .cases[0]
                    .pattern
                )
                dispatched.update(
                    n.value
                    for n in ast.walk(pattern)
                    if isinstance(n, ast.Constant) and isinstance(n.value, str)
                )
        self.assertTrue(tasks <= dispatched, tasks - dispatched)

    def test_config_values_and_types(self):
        expected = {}
        for tree in self.trees.values():
            for node in tree.body:
                if isinstance(node, ast.Assign) and any(
                    isinstance(t, ast.Name) and t.id == "CONFIG_VAR_LIST"
                    for t in node.targets
                ):
                    for row in node.value.elts:
                        category, name, typ, default = row.elts
                        expected[ast.literal_eval(name)] = (
                            ast.literal_eval(category),
                            ast.unparse(typ),
                            ast.unparse(default),
                        )
        actual = {
            r["name"]: (r["category"], r["type"], r["default_source"])
            for r in self.rows("config")
        }
        self.assertEqual(actual, expected)

    def test_tasks_and_every_json_leaf(self):
        tasks = json.loads(baseline("resources/quest/quest.json"))
        self.assertEqual({r["task_id"]: r["data"] for r in self.rows("task")}, tasks)
        # 使用显式栈独立展开，不调用生产盘点的 flatten。
        stack = [("", tasks)]
        expected = {}
        while stack:
            pointer, value = stack.pop()
            if isinstance(value, dict) and value:
                stack.extend(
                    (pointer + "/" + key.replace("~", "~0").replace("/", "~1"), item)
                    for key, item in value.items()
                )
            elif isinstance(value, list) and value:
                stack.extend(
                    (pointer + "/" + str(i), item) for i, item in enumerate(value)
                )
            else:
                expected[pointer] = value
        self.assertEqual(
            {r["pointer"]: r["value"] for r in self.rows("quest_field")}, expected
        )

    def test_entry_registrations_and_runtime_fields(self):
        entries, writes = [], []
        for file, tree in self.trees.items():
            for node in ast.walk(tree):
                if isinstance(node, ast.Call):
                    name = ast.unparse(node.func)
                    binding = isinstance(
                        node.func, ast.Attribute
                    ) and node.func.attr in {
                        "bind",
                        "bind_all",
                        "bind_class",
                        "tag_bind",
                        "protocol",
                        "trace_add",
                        "add_command",
                    }
                    if (
                        binding
                        or name.split(".")[-1]
                        in {
                            "Button",
                            "Checkbutton",
                            "Radiobutton",
                            "Entry",
                            "Combobox",
                            "Spinbox",
                            "Scale",
                            "Listbox",
                            "Menu",
                            "Scrollbar",
                            "ScrolledText",
                            "Text",
                        }
                        or name.endswith("add_argument")
                        or any(
                            k.arg
                            in {
                                "command",
                                "validatecommand",
                                "postcommand",
                                "xscrollcommand",
                                "yscrollcommand",
                            }
                            for k in node.keywords
                        )
                    ):
                        entries.append((f"{file}:{node.lineno}", ast.unparse(node)))
                if (
                    isinstance(node, ast.Attribute)
                    and isinstance(node.ctx, ast.Store)
                    and isinstance(node.value, ast.Name)
                    and node.value.id in {"self", "setting", "runtimeContext", "quest"}
                ):
                    writes.append((f"{file}:{node.lineno}", node.value.id, node.attr))
        self.assertEqual(
            sorted(entries),
            sorted((r["source_ref"], r["registration"]) for r in self.rows("entry")),
        )
        self.assertEqual(
            sorted(writes),
            sorted(
                (r["source_ref"], r["receiver"], r["field"])
                for r in self.rows("runtime_field")
            ),
        )

    def test_nested_data_fields(self):
        expected = []
        for file, tree in self.trees.items():
            for node in ast.walk(tree):
                if isinstance(node, ast.Dict):
                    expected.extend(
                        (
                            f"{file}:{key.lineno}",
                            key.value,
                            "construct",
                            ast.unparse(value),
                        )
                        for key, value in zip(node.keys, node.values)
                        if isinstance(key, ast.Constant) and isinstance(key.value, str)
                    )
                elif (
                    isinstance(node, ast.Subscript)
                    and isinstance(node.slice, ast.Constant)
                    and isinstance(node.slice.value, str)
                ):
                    expected.append(
                        (
                            f"{file}:{node.lineno}",
                            node.slice.value,
                            type(node.ctx).__name__,
                            ast.unparse(node),
                        )
                    )
                elif (
                    isinstance(node, ast.Call)
                    and isinstance(node.func, ast.Attribute)
                    and node.func.attr in {"get", "setdefault", "pop"}
                    and node.args
                ):
                    key = node.args[0]
                    if isinstance(key, ast.Constant) and isinstance(key.value, str):
                        expected.append(
                            (
                                f"{file}:{node.lineno}",
                                key.value,
                                node.func.attr,
                                ast.unparse(node),
                            )
                        )
        self.assertEqual(
            sorted(expected),
            sorted(
                (r["source_ref"], r["key"], r["access"], r["expression"])
                for r in self.rows("data_field")
            ),
        )
        keys = {r["key"] for r in self.rows("data_field")}
        self.assertTrue(
            {
                "group_name",
                "skill_settings",
                "role_var",
                "skill_var",
                "target_var",
                "freq_var",
                "skill_lvl",
                "complete_one_as_all",
                "overall_strategy",
                "task_point",
            }
            <= keys
        )

    def test_resource_paths_and_case_report(self):
        all_paths = (
            subprocess.check_output(
                ["git", "-C", str(REPO), "ls-tree", "-r", "--name-only", "-z", BASELINE]
            )
            .decode()
            .split("\0")
        )
        resources = {p for p in all_paths if p.startswith(("resources/", "locale/"))}
        self.assertEqual({r["path"] for r in self.rows("asset")}, resources)
        report = json.loads(
            (NEXT / "docs/migration/asset_case_report.json").read_text(encoding="utf-8")
        )
        for row in report["references"]:
            self.assertNotEqual(row["status"], "MISSING_OR_MOD")
            if row["status"] == "CASE_MISMATCH":
                self.assertTrue(row["matches"])
                for match in row["matches"]:
                    self.assertIn(match, resources)
                    self.assertNotEqual(Path(match).stem, row["reference"])
                    self.assertEqual(
                        Path(match).stem.casefold(), row["reference"].casefold()
                    )
        self.assertEqual(
            sum(r["status"] == "CASE_MISMATCH" for r in report["references"]), 8
        )

    def test_complete_ownership_and_no_implementation_claims(self):
        self.assertEqual(self.report["baseline"], BASELINE)
        self.assertEqual(len(self.items), len({r["id"] for r in self.items}))
        for row in self.items:
            for key in (
                "original_semantics",
                "source_ref",
                "new_owner",
                "new_entry",
                "data_mapping",
                "acceptance_ids",
            ):
                self.assertTrue(row[key], (row["id"], key))
            self.assertEqual(row["status"], "MAPPED_NOT_IMPLEMENTED")
        functions = {r["id"] for r in self.rows("function")}
        for call in self.report["calls"]:
            self.assertIn(call["resolution"], {"STATIC", "EXTERNAL_OR_DYNAMIC"})
            self.assertTrue(set(call["targets"]) <= functions)
            self.assertEqual(bool(call["targets"]), call["resolution"] == "STATIC")

    def test_combat_pause_and_recovery_ownership(self):
        spec = importlib.util.spec_from_file_location(
            "inventory_mapping", NEXT / "tools/inventory/mapping.py"
        )
        mapping = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mapping)
        vision = "native/games/wvd/vision"
        cases = {
            "StateCombatCheck": (vision, "WvdBattleRecognizer", "M3-VISION"),
            "CheckPauseOverlay": (vision, "WvdPauseRecognizer", "M3-PAUSE"),
            "CheckPauseTextLayout": (vision, "WvdPauseRecognizer", "M3-PAUSE"),
            "GetPauseNegativeEvidence": (vision, "WvdPauseRecognizer", "M3-PAUSE"),
            "TryReadPauseTextByOcr": (vision, "WvdPauseRecognizer", "M3-PAUSE"),
            "TryResumePauseOverlay": (
                "native/games/wvd/recovery",
                "WvdRecovery",
                "M4-RECOVERY",
            ),
        }
        functions = {r["legacy_symbol"]: r for r in self.rows("function")}
        for name, expected in cases.items():
            with self.subTest(name=name):
                symbol = "Factory." + name
                self.assertEqual(mapping.owner("src/script.py", symbol), expected)
                row = functions[symbol]
                self.assertEqual(row["new_owner"], expected[0])
                self.assertEqual(row["new_entry"], expected[1] + "::" + name)
                self.assertEqual(row["acceptance_ids"], [expected[2]])

    def test_review_baseline_only_changes_combat_ownership(self):
        review = "a03f15d0c331232b0c25334115aa793b0833252b"

        def read_review(path):
            return json.loads(
                subprocess.check_output(
                    ["git", "-C", str(REPO), "show", review + ":" + path]
                )
            )

        before = read_review("next/docs/migration/feature_inventory.json")
        self.assertEqual(
            {k: v for k, v in before.items() if k != "items"},
            {k: v for k, v in self.report.items() if k != "items"},
        )
        old_rows = {r["id"]: r for r in before["items"]}
        new_rows = {r["id"]: r for r in self.items}
        self.assertEqual(old_rows.keys(), new_rows.keys())
        changes = []
        for identifier, row in new_rows.items():
            previous = old_rows[identifier]
            if previous == row:
                continue
            self.assertEqual(row["legacy_symbol"], "Factory.StateCombatCheck")
            changed_keys = {
                key
                for key in previous.keys() | row.keys()
                if previous.get(key) != row.get(key)
            }
            self.assertTrue(
                changed_keys <= {"new_owner", "new_entry", "acceptance_ids"}
            )
            changes.append(
                {
                    "id": identifier,
                    "source_ref": row["source_ref"],
                    "before": {
                        key: previous[key]
                        for key in ("new_owner", "new_entry", "acceptance_ids")
                    },
                    "after": {
                        key: row[key]
                        for key in ("new_owner", "new_entry", "acceptance_ids")
                    },
                }
            )
        self.assertTrue(changes)
        print("R02_CHANGES " + json.dumps(changes, ensure_ascii=False))
        assets = json.loads(
            (NEXT / "docs/migration/asset_case_report.json").read_text(encoding="utf-8")
        )
        self.assertEqual(
            assets, read_review("next/docs/migration/asset_case_report.json")
        )
        self.assertEqual(
            sum(r["status"] == "DYNAMIC_REVIEW" for r in assets["references"]), 44
        )

    def test_production_sources_unchanged(self):
        self.assertEqual(self.report["working_source_differences"], [])
        for file, digest in self.report["source_hashes"].items():
            self.assertEqual(hashlib.sha256(baseline(file)).hexdigest(), digest)
            # bat 的工作树是 CRLF，历史变更记录也有混合换行；按 Git 的原有过滤规则
            # 核对完整 blob，不能把 Windows checkout 换行当成代码被修改。
            expected = subprocess.check_output(
                ["git", "-C", str(REPO), "rev-parse", BASELINE + ":" + file]
            ).strip()
            actual = subprocess.check_output(
                ["git", "-C", str(REPO), "hash-object", "--path=" + file, file]
            ).strip()
            self.assertEqual(actual, expected, file)
