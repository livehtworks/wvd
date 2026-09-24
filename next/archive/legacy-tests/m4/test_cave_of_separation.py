"""COS 停点与六段流程测试；组合复用 execute，不继承或收集已有测试类。"""
import hashlib
import importlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

import cv2
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
ENA = "COS/EnaTheAdventurer"
REQUEST = "COS/requestwasfor"
TAKE = "COS/takehimwithyou"
POLICY_ENA = "CaveOfSeperation.ena"
POLICY_REQUEST = "CaveOfSeperation.request"
POLICY_RETURN = "CaveOfSeperation.return"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class CaveOfSeparationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = Path(tempfile.mkdtemp(prefix="m4-cos-", dir=ROOT / ".local"))
        cls.exe = ROOT / "build/m4/Release/test_m4_cave_of_separation.exe"
        cls.sdk = Path(json.loads((ROOT / ".local/maafw.json").read_text(encoding="utf-8"))["sdk"])
        cls.env = dict(os.environ, PATH=str(cls.sdk / "bin") + os.pathsep + os.environ.get("PATH", ""))
        print("M4 COS evidence: " + str(cls.root), flush=True)

    def invoke(self, folder, config, timeout):
        config = dict(config, isolated_case=str(folder),
                      descriptor=str(ROOT / "packs/wvd/parameters/legacy-config-fields.json"))
        source = folder / "input.json"
        source.write_text(json.dumps(config, ensure_ascii=False), encoding="utf-8")
        before = digest(self.exe)
        log_path = folder / "native.log"
        with log_path.open("wb") as log:
            result = subprocess.run([str(self.exe), str(source)], cwd=folder, env=self.env,
                                    stdout=log, stderr=log, timeout=timeout)
        self.assertEqual(digest(self.exe), before)
        (folder / "execution.json").write_text(json.dumps({"exe_sha256": before, "exit": result.returncode}), encoding="utf-8")
        self.assertEqual(result.returncode, 0, log_path.read_text(encoding="utf-8", errors="replace")[-3000:])
        output = json.loads((folder / "output.json").read_text(encoding="utf-8"))
        for module in output["loaded_modules"]:
            expected = self.sdk / "bin" / module["name"]
            self.assertEqual(Path(module["path"]).resolve(), expected.resolve())
            self.assertEqual(module["sha256"], digest(expected))
        return output

    def execute(self, name, screens, transitions=(), *, workflow="probe", policy=POLICY_ENA,
                condition=None, omit=None, corrupt=None, **options):
        folder = self.root / name
        folder.mkdir()
        inspect = folder / "inspect"
        inspect.mkdir()
        config = dict(workflow=workflow, dialogue_task=policy, **options)
        if condition is not None:
            config["condition"] = condition
        compiled = self.invoke(inspect, dict(config, inspect=True), 90)
        self.assertNotIn("error", compiled, compiled)
        requires_state = workflow in ("common", "special", "route")
        self.assertEqual(bool(compiled["checkpoint"]), requires_state, compiled)
        aliases = {"returntoTown.png": "returntotown.png"}

        def canonical(name):
            name = name if name.endswith(".png") else name + ".png"
            return aliases.get(name, name)

        names = set(map(canonical, compiled["images"]))
        for screen in screens:
            names.update(map(canonical, screen))
        patterns = {}
        bundle = folder / "bundle"
        for name in sorted(names):
            rng = np.random.default_rng(int.from_bytes(hashlib.sha256(name.encode()).digest()[:8], "little"))
            patterns[name] = rng.integers(30, 255, (24, 40, 3), dtype=np.uint8)
            path = bundle / "image" / name
            if name == omit:
                continue
            path.parent.mkdir(parents=True, exist_ok=True)
            data = b"invalid test image" if name == corrupt else cv2.imencode(".png", patterns[name])[1].tobytes()
            path.write_bytes(data)
        frames = []
        for index, screen in enumerate(screens):
            pixels = np.zeros((1600, 900, 3), dtype=np.uint8)
            for name, (x, y) in screen.items():
                patch = patterns[canonical(name)]
                pixels[y:y + 24, x:x + 40] = patch
            frame = folder / f"frame-{index}.png"
            frame.write_bytes(cv2.imencode(".png", pixels)[1].tobytes())
            frames.append(frame.name)
        config.update(aliases=aliases, frames=frames, transitions=list(transitions), files=[
            dict(path=path.relative_to(bundle).as_posix(), sha256=digest(path))
            for path in sorted(bundle.rglob("*.png"))])
        output = self.invoke(folder, config, compiled["time_limit_ms"] / 1000 + 20)
        if "error" not in output:
            self.assertEqual(output["state_bound"], requires_state, output)
        output["case_path"] = str(folder)
        return output

    def completed(self, result, calls):
        self.assertNotIn("error", result, result)
        self.assertEqual(result["snapshot"]["state"], "Completed", result)
        self.assertTrue(result["snapshot"]["quiescent"], result)
        self.assertTrue(result["snapshot"]["result_saved"], result)
        self.assertEqual(result["backend_calls"], calls, result)
        self.assertIsNone(result["mismatch"], result)

    def test_frozen_stop_is_boolean_only_and_blocks_dialogue_and_movement(self):
        screen = {ENA: (300, 700), TAKE: (400, 600), "dialogueChoices/nope": (400, 1100),
                  "mapFlag": (20, 20), "dungFlag": (20, 120)}
        positive = ["task_stop", "blocking_screen", "map_route_post", "auto_route_post", "dialogue_post", "special_dialogue_post", "boot_post"]
        negative = ["default_dialogue", "special_dialogue", "movement_stopped", "auto_route_moving", "boot_ready"]
        for mode in positive + negative:
            with self.subTest(mode=mode):
                result = self.execute("probe-" + mode, [screen], condition=dict(mode=mode))
                if mode in positive:
                    self.completed(result, 0)
                else:
                    self.assertNotIn("error", result, result)
                    self.assertEqual(result["snapshot"]["state"], "Interrupted", result)
                    self.assertEqual(result["backend_calls"], 0)
        denied = self.execute("stop-no-action-permit", [screen], workflow="forbidden-click")
        self.assertEqual(denied["snapshot"]["state"], "Failed", denied)
        self.assertEqual(denied["backend_calls"], 0)

    def test_common_and_default_stop_without_clicking_any_competing_marker(self):
        for workflow in ("common", "default", "special"):
            result = self.execute("zero-" + workflow,
                [{ENA: (300, 700), TAKE: (400, 600), "dialogueChoices/nope": (400, 1100), "bondmate_close": (450, 900)}],
                workflow=workflow)
            self.completed(result, 0)

    def test_special_choice_reaches_stop_without_closing_bondmate_overlay(self):
        for workflow in ("special", "common"):
            result = self.execute("after-choice-" + workflow,
                [{TAKE: (400, 600)}, {ENA: (300, 700), "bondmate_close": (450, 900)}],
                [dict(kind=0, x=420, y=612)], workflow=workflow)
            self.completed(result, 1)
            self.assertEqual(result["snapshot"]["business"]["special_dialogues_completed"], 1)
            self.assertFalse(result["snapshot"]["business"]["special_dialogue_pending"])

    def test_default_choice_reaches_task_stop_as_normal_child_return(self):
        result = self.execute("default-after-choice", [{"dialogueChoices/nope": (400, 600)}, {ENA: (300, 700)}],
            [dict(kind=0, x=420, y=612)], workflow="default")
        self.completed(result, 1)

    def test_map_and_route_stop_on_automove_post_without_clicking_stop(self):
        screen = {"mapFlag": (20, 20)}
        commands = [dict(kind=1, x=700, y=250, x2=100, y2=1200, duration=400),
                    dict(kind=0, x=394, y=448), dict(kind=0, x=136, y=1431)]
        for workflow in ("map", "route"):
            result = self.execute("automove-stop-" + workflow,
                [screen, screen, screen, {ENA: (300, 700)}], commands, workflow=workflow)
            self.completed(result, 3)

    def test_nested_common_after_automove_returns_new_stop_to_route(self):
        screen = {"mapFlag": (20, 20)}
        result = self.execute("route-dialogue-stop", [screen, screen, screen, {TAKE: (400, 600)}, {ENA: (300, 700)}],
            [dict(kind=1, x=700, y=250, x2=100, y2=1200, duration=400), dict(kind=0, x=394, y=448),
             dict(kind=0, x=136, y=1431), dict(kind=0, x=420, y=612)], workflow="route")
        self.completed(result, 4)

    def test_request_stop_is_not_a_choice_until_the_return_segment(self):
        stopped = self.execute("request-stop", [{REQUEST: (400, 600)}], workflow="common", policy=POLICY_REQUEST)
        self.completed(stopped, 0)
        returned = self.execute("request-return", [{REQUEST: (400, 600)}, {"dungFlag": (20, 120)}],
            [dict(kind=0, x=420, y=612)], workflow="special", policy=POLICY_RETURN)
        self.completed(returned, 1)

    def test_wrong_stop_does_not_complete_the_selected_stage(self):
        result = self.execute("wrong-stop", [{REQUEST: (400, 600)}], condition=dict(mode="task_stop"))
        self.assertEqual(result["snapshot"]["state"], "Interrupted", result)
        self.assertEqual(result["backend_calls"], 0)

    def test_missing_and_corrupt_stop_resources_do_not_become_nohit(self):
        for field in ("omit", "corrupt"):
            result = self.execute("bad-stop-" + field, [{ENA: (300, 700)}], condition=dict(mode="task_stop"),
                **{field: ENA + ".png"})
            self.assertEqual(result["backend_calls"], 0, result)
            if field == "omit":
                self.assertEqual(result.get("error"), "COMPILE_IMAGE_NOT_IN_MANIFEST:image/" + ENA + ".png", result)
                self.assertEqual(result["connections"], 0, result)
            else:
                self.assertNotIn("error", result, result)
                self.assertEqual(result["snapshot"]["state"], "Failed", result)
                self.assertEqual(result["snapshot"]["reason"], "WVD_TEMPLATE_DECODE_INVALID", result)
                self.assertTrue(result["snapshot"]["quiescent"], result)
                self.assertTrue(result["snapshot"]["result_saved"], result)
                self.assertEqual(result["connections"], 1, result)

    def test_rejected_or_stopped_dialogue_keeps_unconfirmed_intent(self):
        for stopped in (False, True):
            command = dict(kind=0, x=420, y=612, reject=not stopped)
            result = self.execute("choice-interrupted-" + str(stopped), [{TAKE: (400, 600)}, {ENA: (300, 700)}],
                [command], workflow="special", stop_after_input=stopped)
            self.assertEqual(result["snapshot"]["state"], "UserStopped" if stopped else "Failed", result)
            self.assertEqual(result["backend_calls"], 1)
            self.assertTrue(result["snapshot"]["business"]["special_dialogue_pending"])
            self.assertEqual(result["snapshot"]["business"]["special_dialogues_completed"], 0)

    def test_fordraig_request_roi_uses_its_own_image_not_lbc_anchor(self):
        screen = {"fordraig/RequestAccept": (300, 700), "LBC/request": (300, 1200), "request_accepted": (600, 720)}
        for image, accepted in (("fordraig/RequestAccept", True), ("LBC/request", False)):
            result = self.execute("request-roi-" + str(accepted), [screen], policy="fordraig",
                condition=dict(mode="featured_request_accepted", image=image, accepted=accepted))
            self.completed(result, 0)

    def test_six_graphs_retain_fixed_routes_policies_and_budgets(self):
        folder = self.root / "six-plans"
        folder.mkdir()
        result = self.invoke(folder, dict(workflow="cos-plan"), 180)
        self.assertNotIn("error", result, result)
        rows = result["segments"]
        self.assertEqual([row["dialogue_task"] for row in rows],
            ["", "CaveOfSeperation.outbound", POLICY_ENA, POLICY_REQUEST, POLICY_RETURN, POLICY_RETURN])
        positions = (
            [[232, 440], [819, 707], [605, 501], [72, 342]],
            [[394, 448], [446, 1088], [452, 766]],
            [[720, 822], [239, 600], [185, 1185], [560, 652]],
            [[827, 547], [394, 448], [446, 1088], [452, 766], [559, 1087], [666, 448], [660, 919]],
        )
        for row, expected in zip(rows[1:5], positions):
            self.assertEqual([point["position"] for point in row["plan"]["route"]], expected)
        for row in rows:
            self.assertLessEqual(row["time_limit_ms"] + 120000, 1800000)
            self.assertTrue(row["checkpoint"])
        self.assertEqual(result["backend_calls"], 0)

    def shared_execute(self, name, frames, commands, **options):
        # 组合复用单个 execute 方法；不继承、重导出或运行其余 WorkflowTests。
        module_name = f"{__package__}.test_workflow" if __package__ else "test_workflow"
        support = importlib.import_module(module_name)
        driver = support.WorkflowTests(methodName="runTest")
        driver.root, driver.sdk, driver.env = self.root, self.sdk, self.env
        baseline = subprocess.check_output(["git", "ls-tree", "-r", "--name-only",
            "6585f4075f5714ab522aa582993860c09af912c1", "resources/images/dialogueChoices"], cwd=ROOT.parent).decode("utf-8")
        driver.default_dialogues = sorted(Path(path).stem for path in baseline.splitlines() if path.endswith(".png"))
        self.assertEqual(len(driver.default_dialogues), 14)
        extension = self.root / (name + "-quest.json")
        extension.write_text(json.dumps({"CaveOfSeperation": {"_TYPE": "quest"}}), encoding="utf-8")
        # 来自 COS 固定源码和所调用的 time_leap/返城/入洞图，不以任意占位素材填充。
        extra_images = ["cursedWheelTitle", "cursedWheel", "cursedWheelTapRight", "leap", "ruins",
            "cursedwheel_impregnableFortress", "GhostsOfYore", "COS/ArnasPast", "CSC", "didnottakethequest",
            "returntotown", "leaveDung", "return", "COS/Okay", "guildRequest", "COS/COS", "COS/COSENT",
            "stair_1", "stair_2", "stair_3", TAKE, ENA, REQUEST, "bondmate_close"]
        return driver.execute(name, frames, commands, workflow="cave-of-separation", with_state=True,
            quest_catalog=str(extension), descriptor=str(ROOT / "packs/wvd/parameters/legacy-config-fields.json"),
            aliases={"returntoTown.png": "returntotown.png"}, extra_images=extra_images,
            profile=dict(ACTIVE_CSC=False, ACTIVE_REST=False, ACTIVE_ROYALSUITE_REST=False), **options)

    @staticmethod
    def full_scenario():
        # 独立来自6585f407的步骤/方向/坐标；画面只由下一项明确输入推进。
        frames = [{"cursedWheelTitle": (200, 100), "GhostsOfYore": (300, 900)}]
        commands = []

        def advance(command, page):
            commands.append(command)
            frames.append(page.copy())

        def click(x, y, page):
            advance(dict(kind=0, x=x, y=y), page)

        town = {"Inn": (200, 500), "guild": (400, 700), "EdgeOfTown": (400, 1000)}
        click(320, 912, {"leap": (400, 900)})
        click(420, 912, {"returntotown": (400, 700)})
        click(420, 712, {"Inn": (200, 500), "intoWorldMap": (400, 700)})
        click(420, 712, {"worldmapflag": (100, 100)})
        advance(dict(kind=1, x=450, y=150, x2=500, y2=150, duration=400),
                {"worldmapflag": (100, 100), "City_RoyalCityLuknalia": (300, 600)})
        click(320, 612, town)
        click(420, 712, {"COS/Okay": (400, 700)})
        click(420, 712, {"return": (400, 1200)})
        click(420, 1212, town)
        click(220, 512, {"Stay": (400, 700)})
        click(420, 712, {"Economy": (400, 700)})
        click(420, 712, {"OK": (400, 700)})
        click(420, 712, {"Stay": (400, 700)})
        advance(dict(kind=5, key=4), town)
        click(420, 1012, {"COS/COS": (400, 700)})
        # 固定旧源码的 fallback 整批执行 EdgeOfTown、(1,1)，下一轮才重查 COS。
        click(1, 1, {"COS/COS": (400, 700)})
        click(420, 712, {"COS/COSENT": (400, 700)})
        click(420, 712, {"mapFlag": (20, 20)})

        def point(x, y, direction, target="position", stop_page=None):
            swipe = {"左上": (100, 250, 700, 1200), "右上": (700, 250, 100, 1200),
                     "右下": (700, 1200, 100, 250), "左下": (100, 1200, 700, 250)}[direction]
            motion = dict(kind=1, x=swipe[0], y=swipe[1], x2=swipe[2], y2=swipe[3], duration=400)
            page = frames[-1].copy()
            advance(motion.copy(), page)
            click(x, y, page)
            if stop_page is not None:
                click(136, 1431, stop_page)
                return
            click(136, 1431, {"dungFlag": (50, 150)})
            reached = {"mapFlag": (20, 20)}
            reached["cursor_0" if target == "position" else target] = (x - 20, y - 12) if target == "position" else (400, 400)
            click(777, 150, reached)
            advance(motion.copy(), reached)

        point(232, 440, "右下")
        point(819, 707, "右下")
        point(605, 501, "右上")
        point(72, 342, "右上", "stair_2")
        point(394, 448, "右上")
        point(446, 1088, "右上")
        point(452, 766, "左上", stop_page={TAKE: (400, 600)})
        click(420, 612, {ENA: (300, 700), "dialogueChoices/nope": (400, 1100)})
        # B2只观察Ena；新B3冻结策略才允许处理其后的普通选项。
        click(420, 1112, {"mapFlag": (20, 20)})
        point(720, 822, "左上", "stair_3")
        point(239, 600, "左下")
        point(185, 1185, "左下")
        point(560, 652, "左下", stop_page={REQUEST: (400, 600)})
        click(420, 612, {"mapFlag": (20, 20)})
        point(827, 547, "左下", "stair_2")
        point(394, 448, "右上")
        point(446, 1088, "右上")
        point(452, 766, "左上")
        point(559, 1087, "左上")
        point(666, 448, "左上", "stair_1")
        point(660, 919, "右下")
        click(1, 1, town)
        click(420, 712, {"guildRequest": (400, 700), "return": (400, 1200)})
        click(420, 1212, town)
        return frames, commands

    def test_full_six_segment_cycle_keeps_causality_routes_dialogues_and_returns(self):
        frames, commands = self.full_scenario()
        self.assertEqual(len(commands), 110)
        self.assertEqual(commands[14:18], [dict(kind=0, x=420, y=1012), dict(kind=0, x=1, y=1),
            dict(kind=0, x=420, y=712), dict(kind=0, x=420, y=712)])
        result = self.shared_execute("six-segment-cycle", frames, commands)
        self.assertEqual(result["snapshot"]["state"], "Completed", result)
        self.assertTrue(result["snapshot"]["quiescent"])
        self.assertEqual(result["snapshot"]["completed_business_units"], 6)
        self.assertEqual(result["snapshot"]["business"]["cave_of_separation"]["completed_cycles"], 1)
        self.assertEqual(result["snapshot"]["business"]["inn_rests"], 1)
        self.assertEqual(result["snapshot"]["business"]["special_dialogues_completed"], 2)
        self.assertEqual(result["backend_calls"], 110)
        self.assertFalse(result["mismatch"], result.get("mismatch_detail"))

    def test_cos_request_rejection_and_user_stop_never_start_the_next_segment(self):
        for stopped in (False, True):
            frames, commands = self.full_scenario()
            commands[7]["reject"] = not stopped
            result = self.shared_execute("cos-request-interrupted-" + str(stopped), frames[:9], commands[:8],
                **({"stop_after_calls": 8} if stopped else {}))
            self.assertEqual(result["snapshot"]["state"], "UserStopped" if stopped else "Failed", result)
            self.assertEqual(result["backend_calls"], 8)
            self.assertTrue(result["snapshot"]["business"]["cave_of_separation"]["pending"])
            self.assertEqual(result["snapshot"]["business"]["cave_of_separation"]["completed_cycles"], 0)


if __name__ == "__main__":
    unittest.main()
