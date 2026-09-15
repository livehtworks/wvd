"""任务计划的纯解析验证；不把 15 个专项的声明算作可执行分支。"""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
BASE = "6585f4075f5714ab522aa582993860c09af912c1"


class PlanTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = Path(tempfile.mkdtemp(prefix="m4-plan-", dir=ROOT / ".local"))
        cls.sdk = Path(json.loads((ROOT / ".local/maafw.json").read_text(encoding="utf-8"))["sdk"])
        cls.source = json.loads(subprocess.check_output(
            ["git", "show", f"{BASE}:resources/quest/quest.json"], cwd=ROOT.parent))
        print("M4 plan evidence: " + str(cls.root), flush=True)

    def inspect(self, name, source=None, **options):
        folder = self.root / name
        folder.mkdir()
        quests = folder / "quests.json"
        quests.write_text(json.dumps(self.source if source is None else source, ensure_ascii=False), encoding="utf-8")
        before = hashlib.sha256(quests.read_bytes()).hexdigest()
        cfg = {"output": str(folder / "result.json"), "source": {}, "quests": str(quests),
               "descriptor": str(ROOT / "packs/wvd/parameters/legacy-config-fields.json"), "inspect_plans": True}
        cfg.update(options)
        (folder / "input.json").write_text(json.dumps(cfg), encoding="utf-8")
        exe = ROOT / "build/m4/Release/wvd_m4_check.exe"
        exe_hash = hashlib.sha256(exe.read_bytes()).hexdigest()
        # 完整批次包含 43 份独立图的编译、校验和序列化；不是单个运行 Session。
        # 外部看护仍有限，不改变任何节点、帧 TTL 或生产停止预算。
        timeout = 120 if "compile_iterations_manifest" in options else 30
        with (folder / "native.log").open("wb") as log:
            p = subprocess.run([str(exe), str(folder / "input.json")],
                               cwd=folder, stdout=log, stderr=log, timeout=timeout,
                               env=dict(os.environ, PATH=str(self.sdk / "bin") + os.pathsep + os.environ.get("PATH", "")))
        self.assertEqual(p.returncode, 0)
        self.assertEqual(hashlib.sha256(exe.read_bytes()).hexdigest(), exe_hash)
        (folder / "execution.json").write_text(json.dumps({"exe_sha256": exe_hash, "exit": p.returncode}), encoding="utf-8")
        self.assertEqual(hashlib.sha256(quests.read_bytes()).hexdigest(), before)
        r = json.loads((folder / "result.json").read_text(encoding="utf-8"))
        self.assertEqual((r["real_connections"], r["real_inputs"]), (0, 0))
        return r

    def test_all_58_preserve_raw_fields_and_sequence(self):
        r = self.inspect("all")
        self.assertEqual(r["outcome"], "PASS", r)
        plans = {p["task_id"]: p for p in r["plans"]}
        self.assertEqual(set(plans), set(self.source))
        self.assertEqual(sum(p["requires_special_case"] for p in plans.values()), 15)
        for task_id, source in self.source.items():
            p = plans[task_id]
            self.assertEqual(p["source"], source)
            self.assertFalse(p["pipeline_available"])
            entries = source.get("_EOT", [])
            self.assertEqual([step["target"] for step in p["entry_steps"]], [row[1] for row in entries])
            self.assertEqual([step["final_step"] for step in p["entry_steps"]],
                             [i == len(entries) - 1 for i in range(len(entries))])
            self.assertEqual([target["target"] for target in p["route"]],
                             [row[0] for row in source.get("_TARGETINFOLIST", [])])
        self.assertEqual(plans["Dist"]["entry_steps"][0]["fallback"],
                         {"kind": "sequence", "actions": [{"kind": "pattern", "pattern": "EdgeOfTown"},
                                                            {"kind": "point", "point": [1, 1]}]})
        self.assertEqual(plans["Dist"]["entry_steps"][1]["fallback"],
                         {"kind": "swipe", "coordinates": [650, 250, 650, 900]})
        self.assertEqual(plans["IWO"]["return"], {"target": "City_portTownGrandLegion",
                          "swipe": [500, 1100, 700, 700], "dismiss": [500, 1100]})
        self.assertEqual(plans["FFXI-2F"]["return"], {"target": "FFXI/EVENT_VNH", "swipe": None, "dismiss": [550, 1]})

    def task(self, **fields):
        return {"example": {"_TYPE": "dungeon", "_EOT": [["press", "Dist", [1, 1], 1]],
                            "_TARGETINFOLIST": [["chest"]], **fields}}

    def test_all_43_dungeon_entries_compile_with_manifest_resources(self):
        result = self.inspect("entries", compile_entries_manifest=str(ROOT / "packs/wvd/manifest.json"))
        self.assertEqual(result["outcome"], "PASS", result)
        entries = {v["task_id"]: v for v in result["compiled_entries"]}
        self.assertEqual(set(entries), {k for k, v in self.source.items() if v["_TYPE"] == "dungeon"})
        self.assertEqual(len(entries), 43)
        for task_id, entry in entries.items():
            with self.subTest(task_id=task_id):
                self.assertEqual(entry["missing_images"], [])
                self.assertEqual(entry["scope"], "ENTRY_ONLY_NOT_FULL_TASK")
                self.assertFalse(entry["executed"])
                self.assertEqual(entry["nodes"]["Terminal"]["custom_action"], "RootTerminal")
                self.assertEqual(entry["nodes"]["Entered"]["next"], ["Terminal"])
                self.assertTrue(any(v.get("custom_action") == "GuardedAction" for v in entry["nodes"].values()))
                self.assertNotIn("Shell", entry["required_actions"])

    def test_implemented_specials_bind_real_manifest_without_hiding_remaining(self):
        result = self.inspect("specials", compile_specials_manifest=str(ROOT / "packs/wvd/manifest.json"))
        self.assertEqual(result["outcome"], "PASS", result)
        implemented = {row["task_id"]: row for row in result["compiled_specials"]}
        self.assertEqual(set(implemented), {"fortress-B8F_trap", "gaintKiller", "darkLight", "FFXI-Org", "manualSepDemon", "lovesleep", "Scorpionesses", "Scorpionesses_plus_6_hands"})
        self.assertEqual(implemented["manualSepDemon"]["required_normal_units"], 2)
        self.assertEqual(implemented["lovesleep"]["required_normal_units"], 250)
        self.assertEqual(implemented["Scorpionesses"]["required_normal_units"], 3)
        self.assertEqual(implemented["Scorpionesses_plus_6_hands"]["required_normal_units"], 4)
        all_specials = {name for name, value in self.source.items() if value["_TYPE"] == "quest"}
        self.assertEqual(set(result["unimplemented_specials"]), all_specials - set(implemented))
        self.assertFalse(result["execution_available"])
        for graph in implemented.values():
            self.assertEqual(graph["missing_images"], [])
            self.assertFalse(graph["executed"])
            self.assertEqual(graph["scope"], "FINITE_SPECIAL_ITERATION_NOT_FULL_TASK")
            self.assertNotIn("Shell", graph["required_actions"])
        giant = implemented["gaintKiller"]["nodes"]
        self.assertEqual(giant["Started"]["custom_action_param"]["event"], "giant_cycle_started")
        self.assertEqual(giant["Completed"]["custom_action_param"]["event"], "giant_cycle_completed")
        self.assertEqual(giant["RestDue"]["custom_recognition_param"]["field"], "/giant_rest_due")

    def test_special_negative_interval_is_not_silently_reinterpreted(self):
        result = self.inspect("specials-negative-interval", changed_values={"REST_INTERVEL": -1},
            compile_specials_manifest=str(ROOT / "packs/wvd/manifest.json"))
        self.assertEqual(result["outcome"], "Error")
        # 全目录按原顺序验证；新增蝎女后由它先拒绝。巨人的独立拒绝仍由原生状态组调用正式编译器验证。
        self.assertEqual(result["error"], "BOUNTY_REST_INTERVAL_INVALID")

    def test_all_43_dungeon_routes_bind_each_original_target(self):
        result = self.inspect("routes", compile_routes_manifest=str(ROOT / "packs/wvd/manifest.json"))
        self.assertEqual(result["outcome"], "PASS", result)
        routes = {row["task_id"]: row for row in result["compiled_routes"]}
        self.assertEqual(set(routes), {key for key, value in self.source.items() if value["_TYPE"] == "dungeon"})
        for task_id, route in routes.items():
            with self.subTest(task_id=task_id):
                self.assertFalse(route["executed"])
                self.assertEqual(route["scope"], "DUNGEON_ROUTE_ONLY_NOT_FULL_TASK")
                self.assertEqual(route["missing_images"], [])
                nodes = route["nodes"]
                self.assertEqual(nodes["Fight"]["custom_action"], "RunChild")
                self.assertEqual(nodes["OpenChest"]["custom_action"], "RunChild")
                self.assertEqual(nodes["Heal"]["custom_action"], "RunChild")
                for i, target in enumerate(self.source[task_id]["_TARGETINFOLIST"]):
                    self.assertEqual(nodes[f"Point{i}"]["custom_recognition_param"]["value"], i)
                    self.assertEqual(nodes[f"Confirm{i}"]["custom_action_param"]["expected_step"], i)
                    if target[0] in ("stay", "chest_auto", "mark_auto", "dungFlag"):
                        self.assertIn(f"Route{i}_" + ("Wait" if target[0] == "stay" else "Choose"), nodes)
                    elif target[0] == "position" or target[0].startswith("stair"):
                        command = nodes[f"Route{i}_Select0"]["custom_action_param"]["command"]
                        self.assertEqual([command["x"], command["y"]], target[2])
                    else:
                        params = nodes[f"Route{i}_Select0"]["custom_action_param"]["target_recognition"]["parameters"]
                        self.assertEqual(params["image"], target[0])
                self.assertEqual(nodes["Finished"]["custom_recognition_param"]["value"], len(self.source[task_id]["_TARGETINFOLIST"]))

    def test_all_43_iterations_bind_entry_supply_and_route(self):
        result = self.inspect("iterations", compile_iterations_manifest=str(ROOT / "packs/wvd/manifest.json"))
        self.assertEqual(result["outcome"], "PASS", result)
        rows = {row["task_id"]: row for row in result["compiled_iterations"]}
        self.assertEqual(set(rows), {key for key, value in self.source.items() if value["_TYPE"] == "dungeon"})
        for task_id, row in rows.items():
            with self.subTest(task_id=task_id):
                self.assertFalse(row["executed"])
                self.assertEqual(row["missing_images"], [])
                self.assertEqual(row["scope"], "NORMAL_FARM_ITERATION_NOT_FULL_TASK")
                nodes = row["nodes"]
                self.assertEqual(nodes["CountDeparture"]["custom_action_param"]["event"], "dungeon_completed")
                self.assertEqual(nodes["Traverse"]["custom_action"], "RunChild")
                self.assertIn("Departure_Inn_Paid", nodes)
                self.assertIn("Enter_EnterNow", nodes)
                for i, target in enumerate(self.source[task_id]["_TARGETINFOLIST"]):
                    self.assertEqual(nodes[f"Dungeon_Point{i}"]["custom_recognition_param"]["value"], i)
                    if target[0] == "position" or target[0].startswith("stair"):
                        command = nodes[f"Dungeon_Route{i}_Select0"]["custom_action_param"]["command"]
                        self.assertEqual([command["x"], command["y"]], target[2])
                if self.source[task_id].get("_RTT"):
                    params = nodes["Departure_ReturnCity_Click0"]["custom_action_param"]["target_recognition"]["parameters"]
                    self.assertEqual(params["image"], self.source[task_id]["_RTT"][0])

    def test_typed_target_hint_and_chest_exclusions(self):
        r = self.inspect("hints", self.task(_TARGETINFOLIST=[
            ["chest"], ["position", "左下", [133, 814]], ["stair_2", "右上", [827, 547]],
            ["harken", "右上", "stair_DH_R4"], ["chest", "左上", "default"]],
            _FloorCheck="stair_2", _SPECIALDIALOGOPTION=["ready", "quit"], extra={"保留": []}))
        self.assertEqual(r["outcome"], "PASS", r)
        p = r["plans"][0]
        self.assertEqual([x["hint_kind"] for x in p["route"]], ["regions", "position", "position", "stair_reference", "regions"])
        self.assertEqual(p["route"][1]["position"], [133, 814])
        self.assertEqual(p["route"][1]["swipes"], [[100, 1200, 700, 250]])
        self.assertEqual(p["route"][3]["stair_reference"], "stair_DH_R4")
        self.assertEqual(len(p["route"][0]["regions"]), 7)
        self.assertEqual(len(p["route"][4]["regions"]), 13)
        self.assertIsNone(p["route"][0]["swipes"][0])
        self.assertEqual(p["floor"], "stair_2")
        self.assertEqual(p["source"]["_SPECIALDIALOGOPTION"], ["ready", "quit"])

    def test_unknown_commands_and_invalid_geometry_reject(self):
        cases = [
            ("shell", self.task(_EOT=[["press", "Dist", "input swipe 1 1 2 2; input tap 1 1", 1]]), "TASK_COMMAND_UNSUPPORTED"),
            ("verb", self.task(_EOT=[["execute", "Dist", [1, 1], 1]]), "TASK_ENTRY_COMMAND_INVALID"),
            ("coords", self.task(_TARGETINFOLIST=[["position", "左下", [900, 2]]]), "TASK_POINT_BOUNDS"),
            ("roi", self.task(_TARGETINFOLIST=[["chest", "左下", [[800, 10, 200, 40]]]]), "TASK_ROI_BOUNDS"),
            ("direction", self.task(_TARGETINFOLIST=[["chest", "随便"]]), "TASK_SWIPE_DIRECTION_UNKNOWN"),
            ("empty", self.task(_EOT=[]), "TASK_DUNGEON_ROUTE_EMPTY"),
            ("interval", self.task(_EOT=[["press", "Dist", [1, 1], 0]]), "TASK_INTERVAL_INVALID"),
        ]
        for name, data, error in cases:
            r = self.inspect(name, data)
            self.assertEqual(r["outcome"], "Error", r)
            self.assertEqual(r["error"], error)


if __name__ == "__main__":
    unittest.main()
