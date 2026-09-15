"""Fordraig 专项状态、计划与因果夹具；编写不代表已构建或通过验收。"""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

import test_workflow as workflow_helpers

ROOT = Path(__file__).resolve().parents[2]
STAGES = ["Leap", "Request", "Enter", "Trap1", "Trap2", "Trap3", "PreBoss", "Boss", "Exit", "Return"]
# 固定旧源 3545-3613 的人工预期，不调用生产图生成预期路线。
TARGETS = [
    ("position", 721, 448, True), ("position", 720, 608, True),
    ("stair_down", 721, 236, True), ("position", 240, 921, False),
    ("position", 33, 1238, False), ("stair_down", 453, 1027, False),
    ("position", 187, 1027, False), ("stair_teleport", 80, 1026, False),
    ("position", 508, 1025, False), ("position", 720, 1025, False),
    ("stair_teleport", 665, 395, True),
]
EVENTS = {
    "fordraig_started", "fordraig_leap_prepared", "fordraig_leaped", "fordraig_requested", "fordraig_entered",
    "fordraig_trap1_routed", "fordraig_trap1_prepared", "fordraig_trap1_completed",
    "fordraig_trap2_routed", "fordraig_trap2_prepared", "fordraig_trap2_completed",
    "fordraig_trap3_completed", "fordraig_preboss_completed", "fordraig_boss_completed", "fordraig_exited", "fordraig_completed",
}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def walk(value):
    if isinstance(value, dict):
        yield value
        for child in value.values():
            yield from walk(child)
    elif isinstance(value, list):
        for child in value:
            yield from walk(child)


class FordraigTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = Path(tempfile.mkdtemp(prefix="m4-fordraig-", dir=ROOT / ".local"))
        cls.sdk = Path(json.loads((ROOT / ".local/maafw.json").read_text(encoding="utf-8"))["sdk"])
        cls.env = dict(os.environ, PATH=str(cls.sdk / "bin") + os.pathsep + os.environ.get("PATH", ""))
        manifest = json.loads((ROOT / "packs/wvd/manifest.json").read_text(encoding="utf-8"))
        cls.default_dialogues = sorted(Path(row["path"]).stem for row in manifest["files"]
                                      if row["path"].startswith("image/dialogueChoices/") and row["path"].endswith(".png"))
        if len(cls.default_dialogues) != 14:
            raise AssertionError("固定基础对话资源数量变化，须重新核对来源")
        print("Fordraig 隔离验证目录：" + str(cls.root), flush=True)

    @staticmethod
    def profile(automatic=False):
        return {
            "LANGUAGE": "zh_CN", "TASK_SPECIFIC_CONFIG": False, "RELOAD_STRATEGY_WHEN": "不需要",
            "DEFAULT_OVERALL_STRATEGY": "全自动战斗" if automatic else "Manual",
            "QUICK_DISARM_CHEST": True, "ACTIVE_ROYALSUITE_REST": False,
            "STRATEGY": [] if automatic else [{"group_name": "Manual", "complete_one_as_all": False,
                "skill_settings": [{"role_var": role, "skill_var": "防御", "target_var": "next",
                    "skill_lvl": 1, "freq_var": "保留原值"} for role in ("A", "B")]}],
        }

    def native_case(self, name, case, automatic=False, **options):
        folder = self.root / name
        folder.mkdir()
        self.assertTrue(folder.resolve().is_relative_to(self.root.resolve()))
        config = {"case": case, "source": {"GENERAL": self.profile(automatic)},
                  "descriptor": str(ROOT / "packs/wvd/parameters/legacy-config-fields.json"),
                  "quests": str(ROOT / "packs/wvd/parameters/legacy-quests.json"),
                  "manifest": str(ROOT / "packs/wvd/manifest.json"), "output": str(folder / "result.json"), **options}
        source = folder / "input.json"
        source.write_text(json.dumps(config, ensure_ascii=False), encoding="utf-8")
        protected = [source, Path(config["descriptor"]), Path(config["quests"]), Path(config["manifest"])]
        before = {path: digest(path) for path in protected}
        exe = ROOT / "build/m4/Release/test_m4_fordraig.exe"
        self.assertTrue(exe.is_file(), "主代理尚未构建独立 test_m4_fordraig；不得改用旧状态 EXE")
        identity = digest(exe)
        with (folder / "native.log").open("wb") as log:
            process = subprocess.run([str(exe), str(source)], cwd=folder, env=self.env,
                                     stdout=log, stderr=log, timeout=180 if case == "plan" else 45)
        (folder / "execution.json").write_text(json.dumps({"exe_sha256": identity, "exit": process.returncode}), encoding="utf-8")
        self.assertEqual(digest(exe), identity)
        self.assertEqual({path: digest(path) for path in protected}, before)
        self.assertEqual(process.returncode, 0, (folder / "native.log").read_text(encoding="utf-8", errors="replace")[-3000:])
        output = json.loads((folder / "result.json").read_text(encoding="utf-8"))
        self.assertEqual(output["outcome"], "PASS", output)
        self.assertFalse(output["workflow_executed"])
        return output["result"]

    def test_domain_rejects_wrong_phase_unit_pending_and_incomplete_routes(self):
        result = self.native_case("domain", "domain")
        self.assertEqual(result["completed"]["completed_cycles"], 1)
        self.assertFalse(result["completed"]["active"])
        self.assertEqual(result["next_cycle"]["expected_unit"], 10)
        self.assertTrue(result["next_cycle"]["force_automatic"])

    def test_real_run_state_two_cycles_keep_receipts_strategy_and_isolation(self):
        result = self.native_case("state-manual", "state")
        self.assert_state_cycles(result, automatic=False)
        self.assertEqual([len(s["current"]["skill_settings"]) for s in result["boss_strategies"]], [2, 1])

    def test_real_run_state_recovery_preserves_phase_and_pending(self):
        result = self.native_case("state-recovery", "state", recovery=True)
        self.assert_state_cycles(result, automatic=False)

    def test_original_automatic_boss_is_not_forced_manual(self):
        self.assert_state_cycles(self.native_case("state-auto", "state", automatic=True), automatic=True)

    def assert_state_cycles(self, result, automatic):
        state = result["state"]
        self.assertEqual(state["fordraig"]["completed_cycles"], 2)
        self.assertFalse(state["fordraig"]["active"])
        self.assertFalse(state["fordraig"]["pending"])
        self.assertEqual(state["unit_index"], 19)
        self.assertEqual(state["dungeons"], 2)
        self.assertEqual(state["combats"], 2)
        self.assertEqual(state["inn_rests"], 2)
        self.assertEqual(state["featured_visit"]["visits_completed"], 2)
        self.assertEqual(state["featured_visit"]["selections_confirmed"], 1)
        self.assertEqual([s["automatic"] for s in result["boss_strategies"]], [automatic, automatic])
        self.assertEqual(state["strategy"]["automatic"], automatic)
        self.assertEqual(result["fresh_state"]["fordraig"]["completed_cycles"], 0)

    def test_stale_or_zero_frame_cannot_clear_real_state_pending(self):
        snapshots = self.native_case("pending", "pending")
        self.assertEqual(len(snapshots), 2)
        for state in snapshots:
            self.assertTrue(state["fordraig"]["leap_pending"])
            self.assertEqual(state["fordraig"]["completed_cycles"], 0)

    def test_plan_mod_routes_events_budgets_and_featured_image(self):
        result = self.native_case("plan", "plan", automatic=True)
        self.assertEqual((result["baseline_size"], result["merged_size"]), (58, 59))
        plan = result["plan"]
        self.assertEqual(plan["task_id"], "fordraig")
        self.assertEqual([(p["target"], *p["position"]) for p in plan["route"]], [(name, x, y) for name, x, y, _ in TARGETS])
        for point, (_, _, _, upper) in zip(plan["route"], TARGETS):
            self.assertEqual(point["swipes"], [[100, 250, 700, 1200] if upper else [100, 1200, 700, 250]])
        self.assertEqual(plan["entry_steps"][0]["world"], {
            "target": "fordraig/labyrinthOfFordraig", "swipe": [450, 150, 500, 150], "dismiss": [550, 1]})
        self.assertEqual(plan["entry_steps"][1]["target"], "fordraig/Entrance")
        stages = result["stages"]
        names = ["tasks.fordraig." + name for name in STAGES]
        self.assertEqual([row["kind"] for row in stages], names)
        self.assertEqual(result["unit_order"], names * 2)
        self.assertEqual(result["normal_units"], 20)
        self.assertEqual([s["time_limit_ms"] for s in stages], [300000, 360000, 180000, 1600000, 1600000, 1420000, 1420000, 1420000, 1420000, 300000])
        actual_events = set()
        for stage in stages:
            self.assertEqual(stage["dialogue"], "fordraig")
            self.assertLessEqual(len(stage["nodes"]), 4096)
            self.assertLessEqual(stage["time_limit_ms"], 1800000)
            self.assertNotIn("Shell", stage["required_actions"])
            actual_events.update(node["event"] for node in walk(stage["nodes"])
                                 if isinstance(node.get("event"), str) and node["event"].startswith("fordraig_"))
        self.assertEqual(actual_events, EVENTS)
        request_nodes = stages[1]["nodes"]
        accepted = [node for node in walk(request_nodes) if node.get("mode") == "featured_request_accepted"]
        self.assertTrue(accepted)
        self.assertEqual({node["image"] for node in accepted}, {"fordraig/RequestAccept"})
        self.assertEqual({node["accepted"] for node in accepted}, {True, False})
        selections = [node["custom_action_param"] for node in request_nodes.values()
                      if node.get("custom_action") == "GuardedAction" and node["custom_action_param"].get("target_offset") == [350, 180]]
        self.assertEqual(len(selections), 1)
        scrolls = [node for node in walk(request_nodes) if node.get("kind") == "Swipe"
                   and [node.get(k) for k in ("x", "y", "x2", "y2")] == [150, 1000, 150, 200]]
        self.assertEqual(len(scrolls), 3)
        # 同时覆盖主代理的真实 CLI 契约：扩展行是 stages，不是单图 nodes。
        cli = self.cli_plan()
        self.assertEqual(cli["outcome"], "PASS", cli)
        self.assertFalse(cli["execution_available"])
        self.assertEqual(len(cli["compiled_specials"]), 1)
        row = cli["compiled_specials"][0]
        self.assertEqual(row["task_id"], "fordraig")
        self.assertEqual(row["required_normal_units"], 10)
        self.assertNotIn("nodes", row)
        self.assertFalse(row["executed"])
        self.assertEqual(row["missing_images"], [])
        self.assertEqual([s["kind"] for s in row["stages"]], names)
        self.assertEqual([s["time_limit_ms"] for s in row["stages"]], [s["time_limit_ms"] for s in stages])
        self.assertEqual(set(row["images"]), set().union(*(set(s["images"]) for s in row["stages"])))
        self.assertEqual(set(row["required_actions"]), set().union(*(set(s["required_actions"]) for s in stages)))
        for actual, expected in zip(row["stages"], stages):
            self.assertEqual(actual["nodes"], expected["nodes"])

    def cli_plan(self):
        folder = self.root / "cli-plan"
        folder.mkdir()
        quests = folder / "extension-quests.json"
        quests.write_text(json.dumps({"fordraig": {"_TYPE": "quest", "questName": "鸟剑"}}, ensure_ascii=False), encoding="utf-8")
        config = {"source": {"GENERAL": self.profile(automatic=True)}, "quests": str(quests),
                  "descriptor": str(ROOT / "packs/wvd/parameters/legacy-config-fields.json"),
                  "compile_specials_manifest": str(ROOT / "packs/wvd/manifest.json"),
                  "output": str(folder / "result.json")}
        source = folder / "input.json"
        source.write_text(json.dumps(config, ensure_ascii=False), encoding="utf-8")
        before = {path: digest(path) for path in (source, quests, Path(config["descriptor"]), Path(config["compile_specials_manifest"]))}
        exe = ROOT / "build/m4/Release/wvd_m4_check.exe"
        self.assertTrue(exe.is_file(), "主代理尚未构建包含 Fordraig stages 的 CLI")
        identity = digest(exe)
        with (folder / "native.log").open("wb") as log:
            process = subprocess.run([str(exe), str(source)], cwd=folder, env=self.env,
                                     stdout=log, stderr=log, timeout=180)
        (folder / "execution.json").write_text(json.dumps({"exe_sha256": identity, "exit": process.returncode}), encoding="utf-8")
        self.assertEqual(digest(exe), identity)
        self.assertEqual({path: digest(path) for path in before}, before)
        self.assertEqual(process.returncode, 0, (folder / "native.log").read_text(encoding="utf-8", errors="replace")[-3000:])
        return json.loads((folder / "result.json").read_text(encoding="utf-8"))

    def workflow_helper(self):
        # 组合现有 helper，不继承其测试类，也不调用含 git 的 setUpClass。
        helper = workflow_helpers.WorkflowTests(methodName="runTest")
        helper.root, helper.sdk, helper.env = self.root, self.sdk, self.env
        helper.default_dialogues = self.default_dialogues
        return helper

    def workflow_options(self, helper, name):
        source = self.root / (name + "-quest.json")
        source.write_text(json.dumps({"fordraig": {"_TYPE": "quest", "questName": "鸟剑"}}, ensure_ascii=False), encoding="utf-8")
        options = helper.scorpion_options(profile={**self.profile(), "RE_ASSEMBLE_PARTY": False})
        options.update(workflow="fordraig", quest_catalog=str(source))
        options["aliases"].update({"Fordraig/Leap.png": "fordraig/Leap.png", "ReturnText.png": "returnText.png"})
        options["extra_images"] += ["specialRequest", "fordraig/Leap", "fordraig/RequestAccept", "request_accepted",
            "fordraig/labyrinthOfFordraig", "fordraig/Entrance", "fordraig/TryPushingIt",
            "fordraig/thedagger", "fordraig/InsertTheDagger", "bondmate_close", "stair_down", "stair_teleport"]
        return options

    def full_fixture(self, helper):
        frames, commands, time_events = [{"cursedWheel": (400, 700)}], [], []
        def advance(command, page):
            commands.append(command)
            frames.append(page.copy())
        def click(x, y, page):
            advance(dict(kind=0, x=x, y=y), page)
        def back(page):
            advance(dict(kind=5, key=4), page)
        click(420, 712, {"Fordraig/Leap": (300, 900)})
        click(320, 912, {"leap": (400, 900)})
        click(420, 912, {"OK": (400, 700)})
        click(420, 712, {"Inn": (400, 700)})
        rest, inputs = helper.inn_sequence()
        frames.extend(page.copy() for page in rest[1:])
        commands.extend(command.copy() for command in inputs)
        frames[-1]["guild"] = (200, 500)
        click(220, 512, {"guildRequest": (400, 500)})
        click(420, 512, {"guildFeatured": (100, 300)})
        listing = {"guildFeatured": (100, 300), "fordraig/RequestAccept": (400, 800)}
        click(120, 312, listing)
        for _ in range(3):
            advance(dict(kind=1, x=150, y=1000, x2=150, y2=200, duration=400), listing)
        click(770, 992, {"guildRequest": (400, 500)})
        back({"Inn": (400, 700), "intoWorldMap": (300, 800)})
        click(320, 812, {"worldmapflag": (100, 300)})
        advance(dict(kind=1, x=450, y=150, x2=500, y2=150, duration=400),
                {"worldmapflag": (100, 300), "fordraig/labyrinthOfFordraig": (400, 700)})
        click(420, 712, {"openworldmap": (50, 100), "fordraig/Entrance": (400, 700)})
        click(420, 712, {"GotoDung": (400, 700)})
        dungeon = {"dungFlag": (50, 150)}
        click(420, 712, dungeon)
        page = {"mapFlag": (100, 100)}
        click(777, 150, page)
        for index, (target, x, y, upper) in enumerate(TARGETS):
            before = page.copy()
            if target.startswith("stair"):
                before[target] = (x - 20, y - 12)
            swipe = dict(kind=1, x=100, y=250 if upper else 1200, x2=700, y2=1200 if upper else 250, duration=400)
            advance(swipe.copy(), before)
            click(x, y, before)
            if index in (0, 9):
                click(136, 1431, helper.turn_screen(**{"spellskill/CombatAutoDisable": (800, 1070)}))
                if index == 0:
                    click(850, 1100, helper.turn_screen(**{"spellskill/CombatAutoEnable": (800, 1070)}))
                    # 由上一次 Auto 输入后的显式四秒事件结束动画，截图次数不推进战斗。
                    frames.append(dungeon.copy())
                    time_events.append(dict(after_input=len(commands), delay_ms=4000, frame=len(frames) - 1))
                else:
                    # boss 恢复 Manual 的防御策略；若仍强制 Auto，因果后端会拒绝错点。
                    click(513, 1200, dungeon)
            elif index == 8:
                click(136, 1431, {"fordraig/thedagger": (400, 600)})
                click(420, 612, {"fordraig/InsertTheDagger": (400, 700)})
                click(420, 712, dungeon)
            else:
                click(136, 1431, dungeon)
            arrived = {**page, "cursor_0": (x - 20, y - 12)} if target == "position" else page.copy()
            click(777, 150, arrived)
            advance(swipe.copy(), arrived)
            if index in (1, 3):
                back(dungeon)
                advance(dict(kind=1, x=100, y=250, x2=800, y2=250, duration=400), dungeon)
                click(400, 800, dungeon)
                click(400, 800, dungeon)
                click(400, 800, {"fordraig/TryPushingIt": (400, 700)})
                click(420, 712, dungeon)
                click(777, 150, page)
        back(dungeon)
        click(455, 1200, {"leaveDung": (400, 700)})
        click(420, 712, {"ReturnText": (400, 700)})
        click(420, 712, {"City_RoyalCityLuknalia": (400, 700)})
        click(420, 712, {"Inn": (400, 700)})
        return frames, commands, time_events

    def test_full_causal_workflow_ten_segments_and_boss_strategy(self):
        helper = self.workflow_helper()
        frames, commands, events = self.full_fixture(helper)
        result = helper.execute("fordraig-full", frames, commands, time_events=events,
                                **self.workflow_options(helper, "full"))
        self.assertEqual(result["snapshot"]["state"], "Completed", result)
        self.assertTrue(result["snapshot"]["quiescent"])
        self.assertFalse(result["mismatch"], result.get("mismatch_detail"))
        self.assertEqual(result["backend_calls"], len(commands))
        self.assertEqual(len(result["snapshot"]["sessions"]), 10)
        self.assertEqual(result["time_event_count"], 1)
        state = result["snapshot"]["business"]
        self.assertEqual(state["fordraig"]["completed_cycles"], 1)
        self.assertFalse(state["fordraig"]["pending"])
        self.assertEqual(state["combats"], 2)
        self.assertEqual(state["inn_rests"], 1)
        self.assertEqual(state["featured_visit"]["visits_completed"], 1)
        self.assertEqual(state["special_dialogues_completed"], 2)
        self.assertEqual([row["role_var"] for row in state["strategy"]["current"]["skill_settings"]], ["B"])

    def test_causal_leap_rejection_keeps_pending_without_request_or_route(self):
        helper = self.workflow_helper()
        frames = [{"cursedWheel": (400, 700)}, {"Fordraig/Leap": (300, 900)},
                  {"leap": (400, 900)}, {"OK": (400, 700)}]
        commands = [dict(kind=0, x=420, y=712), dict(kind=0, x=320, y=912), dict(kind=0, x=420, y=912, reject=True)]
        result = helper.execute("fordraig-leap-rejected", frames, commands,
                                **self.workflow_options(helper, "rejected"))
        self.assertEqual(result["snapshot"]["state"], "Failed", result)
        self.assertFalse(result["mismatch"], result.get("mismatch_detail"))
        self.assertTrue(result["snapshot"]["quiescent"])
        self.assertEqual(result["backend_calls"], 3)
        state = result["snapshot"]["business"]
        self.assertTrue(state["fordraig"]["leap_pending"])
        self.assertEqual(state["fordraig"]["completed_cycles"], 0)
        self.assertEqual(state["featured_visit"]["visits_completed"], 0)


if __name__ == "__main__":
    unittest.main()
