"""固定旧源独立预期，对照原生导入与目录绑定；不把数据通过称为任务已迁移。"""
import json
import ast
import hashlib
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
BASE = "6585f4075f5714ab522aa582993860c09af912c1"


class DataTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = Path(tempfile.mkdtemp(prefix="m4-data-", dir=ROOT / ".local"))
        cls.descriptor = ROOT / "packs/wvd/parameters/legacy-config-fields.json"
        cls.fields = json.loads(cls.descriptor.read_text(encoding="utf-8"))["fields"]
        cls.sdk = Path(json.loads((ROOT / ".local/maafw.json").read_text(encoding="utf-8"))["sdk"])
        cls.expected_quests = json.loads(subprocess.check_output(["git", "show", f"{BASE}:resources/quest/quest.json"], cwd=ROOT.parent))
        cls.inventory = json.loads((ROOT / "docs/migration/feature_inventory.json").read_text(encoding="utf-8"))
        print("M4 data evidence: " + str(cls.root), flush=True)

    def run_data(self, name, source=None, **kwargs):
        folder = self.root / name
        folder.mkdir()
        cfg = {"descriptor": str(self.descriptor), "source": {} if source is None else source,
               "output": str(folder / "result.json"), **kwargs}
        if cfg.pop("save_profile", False):
            cfg["profile_path"] = str(folder / "new-profile.json")
        (folder / "input.json").write_text(json.dumps(cfg, ensure_ascii=False), encoding="utf-8")
        exe = ROOT / "build/m4/Release/wvd_m4_check.exe"
        env = dict(os.environ, PATH=str(self.sdk / "bin") + os.pathsep + os.environ.get("PATH", ""))
        with (folder / "native.log").open("wb") as log:
            done = subprocess.run([str(exe), str(folder / "input.json")], cwd=folder, env=env, stdout=log, stderr=log, timeout=30)
        self.assertEqual(done.returncode, 0)
        result = json.loads((folder / "result.json").read_text(encoding="utf-8"))
        self.assertEqual(result["real_connections"], 0)
        self.assertEqual(result["real_inputs"], 0)
        return result

    def test_verified_defaults_and_33_fields(self):
        result = self.run_data("defaults")
        expected = {item["name"]: item["default"] for item in self.inventory["items"] if item["kind"] == "config"}
        class Msgid(ast.NodeTransformer):
            def visit_Call(self, node):
                if not isinstance(node.func, ast.Name) or node.func.id != "_" or len(node.args) != 1:
                    raise ValueError("Unexpected default expression")
                return node.args[0]
        expected = {name: ast.literal_eval(Msgid().visit(ast.parse(value["expression"], mode="eval")))
                    if isinstance(value, dict) and "expression" in value else value for name, value in expected.items()}
        # 盘点以 gettext msgid 记录默认字面量，结构值保留原树。
        self.assertEqual(len(result["values"]), 33)
        self.assertEqual(set(result["values"]), set(expected))
        self.assertEqual(result["values"], expected)
        self.assertEqual(result["export"], {})

    def test_native_pipeline_preparation_cli_preserves_author_and_has_no_execution(self):
        import cv2
        import numpy as np
        source = self.root / "directory-author"
        (source / "image/group").mkdir(parents=True)
        (source / "pipeline").mkdir()
        template = np.random.default_rng(95615).integers(0, 255, (24, 40, 3), dtype=np.uint8)
        (source / "image/group/target.png").write_bytes(cv2.imencode(".png", template)[1].tobytes())
        original = {"Match": {"recognition": "TemplateMatch", "template": "group", "threshold": .99}}
        (source / "pipeline/main.json").write_text(json.dumps(original), encoding="utf-8")
        files = [{"path": p.relative_to(source).as_posix(), "sha256": hashlib.sha256(p.read_bytes()).hexdigest()}
                 for p in source.rglob("*") if p.is_file()]
        destination = self.root / "directory-prepared"
        request = dict(root=str(source), revision="author-1", files=files, destination=str(destination))
        result = self.run_data("directory-cli", prepare_pipeline_bundle=request)
        self.assertEqual(result["outcome"], "PASS", result)
        prepared = result["prepared_pipeline_bundle"]
        self.assertFalse(prepared["executed"])
        self.assertFalse(result["execution_available"])
        self.assertNotEqual(prepared["revision"], "author-1")
        self.assertEqual(json.loads((destination / "pipeline/main.json").read_text())["Match"]["template"],
                         ["group/target.png"])
        for file in files:
            self.assertEqual(hashlib.sha256((source / file["path"]).read_bytes()).hexdigest(), file["sha256"])
        for file in prepared["files"]:
            self.assertEqual(hashlib.sha256((destination / file["path"]).read_bytes()).hexdigest(), file["sha256"])
        rejected = self.run_data("directory-cli-existing", prepare_pipeline_bundle=request)
        self.assertEqual(rejected["error"], "PIPELINE_DESTINATION_EXISTS_OR_INVALID")

    def test_sections_unknown_and_roundtrip(self):
        for specific in (False, True):
            source = {"GENERAL": {"FARM_TARGET": "Dist", "TASK_SPECIFIC_CONFIG": specific, "LANGUAGE": "en_US",
                                 "SKIP_COMBAT_RECOVER": True, "extra": {"键/名": [1, None, {}]}},
                      "DEFAULT": {"REST_INTERVEL": 3, "SKIP_CHEST_RECOVER": True},
                      "Dist": {"REST_INTERVEL": 7, "TASK_POINT_STRATEGY": {"overall_strategy": "自定义任务点策略",
                                "task_point": {"0": "全自动战斗"}, "unknown": [False]}}, "elsewhere": {"keep": []}}
            result = self.run_data("sections-" + str(specific), source)
            self.assertEqual(result["values"]["REST_INTERVEL"], 7 if specific else 3)
            self.assertTrue(result["values"]["SKIP_COMBAT_RECOVER"])
            self.assertEqual(result["export"], source)
            self.assertIn("/GENERAL/extra", result["legacy_passthrough"])
            self.assertIn("/Dist/TASK_POINT_STRATEGY/unknown", result["legacy_passthrough"])

    def test_all_fields_section_precedence_and_missing_target(self):
        def values(mark, number):
            result = {}
            for field in self.fields:
                name, kind = field["name"], field["type"]
                if kind == "boolean":
                    result[name] = bool(number % 2)
                elif kind == "integer":
                    result[name] = number
                elif kind == "string":
                    result[name] = mark + ":" + name
                elif kind == "array":
                    result[name] = [{"group_name": mark, "skill_settings": [], "complete_one_as_all": True}]
                else:
                    result[name] = {"overall_strategy": mark, "task_point": {"0": mark, "11": mark}}
            result["LANGUAGE"] = "en_US" if number % 2 else "zh_CN"
            result["KARMA_ADJUST"] = "+2" if number % 2 else "-2"
            result["DEFAULT_OVERALL_STRATEGY"] = mark
            return result

        general, default, task = values("通用", 1), values("默认", 2), values("任务", 3)
        for specific in (False, True):
            for target in (None, "未登记任务", "目标/甲"):
                with self.subTest(specific=specific, target=target):
                    source = {"GENERAL": {**general, "TASK_SPECIFIC_CONFIG": specific, "FARM_TARGET": target},
                              "DEFAULT": default, "目标/甲": task, "未知区段": {"空": [], "空值": None}}
                    r = self.run_data("precedence-" + str(specific) + "-" + str(target).replace("/", "_"), source)
                    selected = "目标/甲" if specific and target == "目标/甲" else "DEFAULT"
                    self.assertEqual(r["outcome"], "PASS", r)
                    self.assertEqual(r["selected_section"], selected)
                    self.assertEqual(r["values"], source[selected])
                    self.assertEqual(r["sources"], {field["name"]: selected for field in self.fields})
                    self.assertEqual(r["export"], source)
                    self.assertEqual(r["legacy_passthrough"]["/未知区段/空"], [])
                    self.assertIsNone(r["legacy_passthrough"]["/未知区段/空值"])

    def test_empty_containers_are_not_replaced_by_defaults(self):
        source = {"GENERAL": {"STRATEGY": [], "TASK_POINT_STRATEGY": {}}, "DEFAULT": {},
                  "unused": {"list": [], "object": {}, "null": None}}
        r = self.run_data("empty-fields", source, save_profile=True)
        self.assertEqual(r["outcome"], "PASS", r)
        self.assertEqual(r["values"]["STRATEGY"], [])
        self.assertEqual(r["values"]["TASK_POINT_STRATEGY"], {})
        self.assertEqual(r["export"], source)
        for index, value in enumerate(([], "", False, 0)):
            r = self.run_data("empty-root-" + str(index), value)
            self.assertEqual(r["outcome"], "Error", r)
        r = self.run_data("null-root", raw_json="null")
        self.assertEqual(r["outcome"], "Error", r)

    def test_duplicate_nested_keys_are_rejected_without_losing_siblings(self):
        for name, raw in (
            ("unknown", '{"GENERAL":{"未知":{"x":1,"x":2}}}'),
            ("task-point", '{"GENERAL":{"TASK_POINT_STRATEGY":{"task_point":{"0":"a","0":"b"}}}}'),
            ("skill", '{"GENERAL":{"STRATEGY":[{"group_name":"a","skill_settings":[{"skill_lvl":1,"skill_lvl":2}]}]}}'),
        ):
            r = self.run_data("nested-duplicate-" + name, raw_json=raw)
            self.assertEqual(r["outcome"], "Error", r)
            self.assertIn("LEGACY_DUPLICATE_KEY", r["error"])
        source = {"GENERAL": {"未知": [{"x": 1}, {"x": 2}]}}
        r = self.run_data("nested-independent-objects", source)
        self.assertEqual(r["outcome"], "PASS", r)
        self.assertEqual(r["export"], source)

    def test_strategy_and_cas(self):
        source = {"GENERAL": {"KARMA_ADJUST": "-7", "STRATEGY": [{"group_name": "中文", "complete_one_as_all": True,
            "unknown": 12, "skill_settings": [{"role_var": "角色", "skill_var": "左下技能", "target_var": "next",
            "freq_var": "任意旧值", "skill_lvl": 2, "extension": {"a": []}}]}]}}
        result = self.run_data("strategy", source, save_profile=True)
        self.assertEqual(result["outcome"], "PASS", result)
        self.assertEqual(result["export"], source)
        self.assertEqual(result["values"]["KARMA_ADJUST"], "-7")
        self.assertTrue(result["cas_conflict"])
        self.assertEqual(result["profile_saved"], result["profile_after_conflict"])
        self.assertTrue(result["incomplete_rejected"])
        self.assertEqual(result["profile_saved"], result["profile_after_rejected_draft"])
        self.assertNotEqual(result["profile_created"]["revision"], result["profile_saved"]["revision"])
        self.assertIn("/GENERAL/STRATEGY/0/skill_settings/0/extension", result["legacy_passthrough"])

    def test_bad_inputs_do_not_default(self):
        for name, raw in [("duplicate", '{"GENERAL":{"EMU_INDEX":1,"EMU_INDEX":2}}'),
                          ("type", '{"GENERAL":{"AUTO_START_CLASH":"false"}}'),
                          ("bad_json", '{'), ("section", '{"GENERAL":[]}')]:
            result = self.run_data(name, raw_json=raw)
            self.assertEqual(result["outcome"], "Error", result)
        refused = self.run_data("device-denied", device={"type": "adb"})
        self.assertEqual(refused["error"], "M4_REAL_DEVICE_AND_EXECUTION_NOT_ENABLED")

    def test_copy_source_is_unchanged_and_export_edits(self):
        source = {"GENERAL": {"LANGUAGE": "en_US", "TASK_SPECIFIC_CONFIG": True, "FARM_TARGET": "中文任务"},
                  "DEFAULT": {"REST_INTERVEL": 2}, "中文任务": {"REST_INTERVEL": 8, "未知": [None, {}]}}
        original = self.root / "synthetic-config.json"
        original.write_text(json.dumps(source, ensure_ascii=False), encoding="utf-8")
        before = hashlib.sha256(original.read_bytes()).hexdigest()
        result = self.run_data("copy", legacy_source=str(original), copy_directory=str(self.root / "import-copy"),
                               changed_values={"REST_INTERVEL": 5, "LANGUAGE": "zh_CN"})
        self.assertEqual(result["outcome"], "PASS", result)
        self.assertEqual(hashlib.sha256(original.read_bytes()).hexdigest(), before)
        self.assertEqual((self.root / "import-copy/legacy-config.json").read_bytes(), original.read_bytes())
        expected = json.loads(json.dumps(source))
        expected["中文任务"]["REST_INTERVEL"] = 5
        expected["GENERAL"]["LANGUAGE"] = "zh_CN"
        self.assertEqual(result["export"], expected)

    def test_profile_creation_failure_is_not_success(self):
        destination = self.root / "existing-profile.json"
        destination.write_bytes(b"existing private fixture")
        result = self.run_data("save-failed", profile_path=str(destination))
        self.assertEqual(result["outcome"], "Error", result)
        self.assertEqual(destination.read_bytes(), b"existing private fixture")
        for index, filename in enumerate(("config.json", "CONFIG.JSON", "Config.Json")):
            protected = self.root / filename
            result = self.run_data("protected-name-" + str(index), profile_path=str(protected))
            self.assertEqual(result["outcome"], "Error", result)
            self.assertIn("PROFILE_PATH_INVALID", result["error"])
            self.assertFalse(protected.exists())

    def test_all_fields_reject_wrong_types(self):
        for field in self.fields:
            wrong = [] if field["type"] != "array" else "not-an-array"
            result = self.run_data("bad-field-" + field["name"], {"GENERAL": {field["name"]: wrong}})
            self.assertEqual(result["outcome"], "Error", (field, result))

    def test_all_tasks_and_mod_conflicts(self):
        quests = ROOT / "packs/wvd/parameters/legacy-quests.json"
        result = self.run_data("tasks", quests=str(quests))
        self.assertEqual(result["quests"], self.expected_quests)
        self.assertEqual(len(result["task_ids"]), 58)
        self.assertFalse(result["execution_available"])
        mod = json.dumps({"Dist": {"_TYPE": "dungeon", "questName": "自定义"}, "bad": {"_TYPE": "invalid"}}, ensure_ascii=False)
        merged = self.run_data("mods", quests=str(quests), mods=[mod, mod])
        self.assertEqual(merged["quests"]["Dist"], self.expected_quests["Dist"])
        self.assertEqual(merged["quests"]["Dist_mod_mod"]["questName"], "自定义_自定义_自定义")
        self.assertEqual(len(merged["mod_diagnostics"]), 2)


if __name__ == "__main__":
    unittest.main()
