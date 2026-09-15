"""运行正式 C++ 编译器的导航/住宿 Pipeline；图像合成，SDK 和门禁不替换。"""
import hashlib
import ast
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

import cv2
import numpy as np

ROOT = Path(__file__).resolve().parents[2]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class WorkflowTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = Path(tempfile.mkdtemp(prefix="m4-workflow-", dir=ROOT / ".local"))
        cls.sdk = Path(json.loads((ROOT / ".local/maafw.json").read_text(encoding="utf-8"))["sdk"])
        cls.env = dict(os.environ, PATH=str(cls.sdk / "bin") + os.pathsep + os.environ.get("PATH", ""))
        baseline_images = subprocess.check_output(["git", "ls-tree", "-r", "--name-only",
            "6585f4075f5714ab522aa582993860c09af912c1", "resources/images/dialogueChoices"], cwd=ROOT.parent).decode("utf-8")
        cls.default_dialogues = sorted(Path(name).stem for name in baseline_images.splitlines() if name.endswith(".png"))
        if len(cls.default_dialogues) != 14:
            raise AssertionError("legacy dialogue inventory changed")
        print("M4 workflow evidence: " + str(cls.root), flush=True)

    def execute(self, name, screens, transitions, **options):
        folder = self.root / name
        bundle = folder / "bundle"
        (bundle / "image").mkdir(parents=True)
        resource_kind = "dungeon-route" if options.get("workflow") == "fortress-trap" else "iteration" if options.get("workflow") in ("giant", "dark-light", "mining", "manual-separation", "scorpion", "fishing-cycle", "jier", "golden-chest", "sandman", "gold-income", "bull-cave", "steel-trial", "repel-forces", "fordraig", "cave-of-separation") else options.get("workflow")
        if resource_kind in ("bounty-visit", "featured-request", "sleep-batch", "fishing-cast", "fishing-reward", "fishing-round", "fishing-seek"): resource_kind = "common"
        names = ["worldmapflag", "City_RoyalCityLuknalia", "Inn", "Stay", "Economy", "royalsuite", "OK"]
        if resource_kind in ("departure", "iteration"):
            names += ["openworldmap", "intoWorldMap", "returntoTown", "returnText", "EdgeOfTown", "dungFlag", "mapFlag", "chestFlag",
                      "combatActive", "combatActive_2", "combatActive_3", "combatActive_4",
                      "guild", "Edit", "PartyManagement", "PartyManagementTitle", "AssembleParty"]
        if resource_kind in ("auto", "turn", "encounter", "dungeon-route", "iteration"):
            names += ["combatActive", "combatActive_2", "combatActive_3", "combatActive_4", "close",
                      "dungFlag", "chestFlag", "RiseAgain",
                      "spellskill/skillDetail", "spellskill/CombatAutoEnable", "spellskill/CombatAutoDisable"]
        if resource_kind in ("turn", "encounter", "dungeon-route", "iteration"):
            names += ["spellskill/char/A", "spellskill/char/A_sp", "spellskill/char/B", "flee", "dungFlag", "chestFlag",
                      "RiseAgain", "supportSkillCheck", "notenoughsp", "notenoughmp", "next", "combatTarget", "combatSpd", "combatSpd_DHI"]
            names += [f"spellskill/skillLvl/{prefix}{level}" for prefix in ("lv", "s_lv") for level in range(1, 10)]
        if resource_kind in ("map", "map-confirm", "state-route", "dungeon-route", "iteration"):
            names += ["cursedWheel_timeLeap", "mapFlag", "dungFlag", "chest", "chestFlag", "chestOpening", "whowillopenit",
                      "AutoMove", "EdgeOfTown", "combatActive", "combatActive_2", "combatActive_3", "combatActive_4",
                      "cursor_0", "cursor_1", "cursor_2", "cursor_3", "stair_up", "stair_floor", "harken",
                      "returnText", "returntoTown", "openworldmap"]
        if resource_kind in ("chest", "dungeon-route", "iteration"):
            names += ["chestFlag", "whowillopenit", "chestOpening", "chestfear", "RiseAgain", "ambush", "dungFlag",
                      "combatActive", "combatActive_2", "combatActive_3", "combatActive_4"]
        if resource_kind in ("heal", "dungeon-route", "iteration"):
            names += ["mapFlag", "dungFlag", "trait", "recover", "story", "chestFlag", "whowillopenit", "chestOpening", "RiseAgain",
                      "combatActive", "combatActive_2", "combatActive_3", "combatActive_4"]
        if resource_kind in ("travel", "city-travel"):
            names += ["openworldmap", "intoWorldMap", "dungFlag"]
        if resource_kind == "time-leap":
            names += ["cursedWheelTitle", "cursedWheel", "cursedWheelTapRight", "leap", "ruins", "startdownload",
                      "dungFlag", "mapFlag", "EdgeOfTown", "returnText", "returntotown", "openworldmap",
                      options.get("leap_chapter", "cursedwheel_impregnableFortress"), options["leap_target"]]
        if resource_kind in ("party", "party-rest"):
            names += ["guild", "Edit", "PartyManagement", "PartyManagementTitle", "AssembleParty", "partyBlue"]
        if resource_kind in ("auto-route", "entry"):
            names += ["mapFlag", "dungFlag", "chestFlag", "combatActive", "combatActive_2", "combatActive_3", "combatActive_4", "EdgeOfTown"]
        if resource_kind in ("auto-route", "dungeon-route", "iteration"):
            names += ["chestOpening", "whowillopenit", "RiseAgain", "NoChestCanBeFound", "theRouteToTheDestinationCannotBeFound",
                      "chest_auto", "mark_auto", "chest_auto_minus", "resume", "returnText", "returntoTown", "openworldmap"]
        if resource_kind in ("entry", "iteration"):
            names += ["GotoDung", "openworldmap", "returntoTown", "intoWorldMap", "TradeWaterway", "Dist", "EVENT", "FFXI/EVENT_GCN", "FFXI/zone5", "preGate"]
        if options.get("attach_recovery") or resource_kind in (
                "recover", "common", "iteration", "dungeon-route", "map", "map-confirm", "state-route",
                "auto-route", "auto", "turn", "encounter", "chest", "heal", "revival"):
            names += ["dungFlag", "openworldmap", "returnText", "returntoTown", "mapFlag", "chestFlag", "whowillopenit",
                      "fishing/cast", "fishing/striking", "fishing/CloseFishInfo", "combatActive", "combatActive_2",
                      "combatActive_3", "combatActive_4", "boot_title_logo", "boot_attention", "startdownload",
                      "retry", "retry_blank", "totitle", "resume", "trait", "recover", "spellskill/skillDetail", "close", "someonedead", "RiseAgain",
                      "multipeopledead", "skull", "sandman_recover", "blessing", "combatClose", "ambush", "ignore"]
            names += ["City_fortress", "City_DHI", "City_portTownGrandLegion"]
            names += ["dialogueChoices/" + name for name in self.default_dialogues]
        names += options.get("extra_images", [])
        if options.get("causality") or resource_kind == "causality":
            names += ["CSC", "leap", "didnottakethequest", "LBC/symbolofalliance", "LBC/EnaWasSaved"]
        if resource_kind == "revival":
            names.append("RiseAgain")
        # Windows 不允许同时保存仅大小写不同的两张图；按封存别名生成同一规范资源。
        def image_name(name):
            return options.get("aliases", {}).get(name + ".png", name + ".png")[:-4]
        names = list(dict.fromkeys(image_name(name) for name in names))
        rng = np.random.default_rng(90614)
        patterns = {name: rng.integers(30, 255, (24, 40, 3), dtype=np.uint8) for name in names}
        if options.get("causality") or resource_kind == "causality":
            patterns["didnottakethequest"][:, :, :2] = 0
            patterns["LBC/EnaWasSaved"][:, :, 0] = 0
        if options.get("real_bobber"):
            patterns["fishing/bobber"] = cv2.imdecode(np.fromfile(ROOT / "packs/wvd/image/fishing/bobber.png", dtype=np.uint8), cv2.IMREAD_COLOR)
        for name in options.get("large_templates", []):
            patterns[name] = rng.integers(30, 255, (80, 80, 3), dtype=np.uint8)
        if "chest_auto_minus" in patterns:
            patterns["chest_auto_minus"] = rng.integers(10, 120, (24, 40, 3), dtype=np.uint8)
        def write(path, pixels):
            path.write_bytes(cv2.imencode(".png", pixels)[1].tobytes())
        for key, pixels in patterns.items():
            if key in options.get("mod_only_images", []):
                continue
            path = bundle / "image" / (key + ".png")
            path.parent.mkdir(parents=True, exist_ok=True)
            write(path, pixels)
            if key in options.get("bad_base_images", []):
                path.write_bytes(b"invalid base fixture image")
        frames = []
        for i, screen in enumerate(screens):
            pixels = np.zeros((1600, 900, 3), dtype=np.uint8)
            # 明确合成背景变化，之后再绘制模板；不让识别器按测试预期直接返回结果。
            for x, y, w, h, value in options.get("frame_rectangles", {}).get(str(i), []):
                self.assertTrue(0 <= x < x+w <= 900 and 0 <= y < y+h <= 1600)
                self.assertTrue(0 <= value <= 255)
                pixels[y:y+h, x:x+w] = value
            for key, (x, y) in screen.items():
                pattern = patterns[image_name(key.split("@", 1)[0])]
                if (options.get("causality") or resource_kind == "causality") and key in ("didnottakethequest", "LBC/EnaWasSaved"):
                    pattern = pattern.copy()
                    pattern[:, :, 2] //= 2
                if key == "fishing/bobber" and options.get("real_bobber"):
                    # 逆变换旧算法的红通道归一化；使用真实模板，识别结果仍由正式C++产生。
                    gray = cv2.cvtColor(pattern, cv2.COLOR_BGR2GRAY)
                    pattern = np.zeros_like(pattern)
                    pattern[:, :, 2] = (14 + gray.astype(float) * 86 / 255).astype(np.uint8)
                if key == "chest_auto_minus":
                    pattern = pattern + np.uint8(90)
                if key == "next" and i in options.get("degraded_next_frames", []):
                    noise = rng.integers(30, 255, pattern.shape, dtype=np.uint8)
                    degraded = ((pattern.astype(np.uint16) + noise) // 2).astype(np.uint8)
                    score = float(cv2.matchTemplate(degraded, pattern, cv2.TM_CCOEFF_NORMED)[0, 0])
                    self.assertGreater(score, .60)
                    self.assertLess(score, .86)
                    pattern = degraded
                if key == "retry" and i in options.get("degraded_retry_frames", []):
                    noise = rng.integers(30, 255, pattern.shape, dtype=np.uint8)
                    degraded = ((pattern.astype(np.uint16) + noise) // 2).astype(np.uint8)
                    score = float(cv2.matchTemplate(degraded, pattern, cv2.TM_CCOEFF_NORMED)[0, 0])
                    self.assertGreater(score, .60)
                    self.assertLess(score, .80)
                    pattern = degraded
                if key.split("@", 1)[0] in options.get("focused_map_templates", {}).get(str(i), []):
                    altered = pattern.copy()
                    cy, cx = (pattern.shape[0] - 15) // 2, (pattern.shape[1] - 15) // 2
                    altered[cy:cy+15, cx:cx+15] = 255
                    self.assertGreater(float(cv2.matchTemplate(altered, pattern, cv2.TM_CCOEFF_NORMED)[0, 0]), .80)
                    difference = np.abs(cv2.cvtColor(altered[cy:cy+15, cx:cx+15], cv2.COLOR_BGR2GRAY).astype(float) -
                        cv2.cvtColor(pattern[cy:cy+15, cx:cx+15], cv2.COLOR_BGR2GRAY).astype(float)).mean() / 255
                    self.assertGreater(difference, .20)
                    pattern = altered
                h, w = pattern.shape[:2]
                pixels[y:y+h, x:x+w] = pattern
            if i in options.get("pause_frames", []):
                area = np.full((110, 240, 3), 20, np.uint8)
                cv2.putText(area, "Pause", (40, 55), cv2.FONT_HERSHEY_SIMPLEX,
                            1.0, (190, 190, 190), 2, cv2.LINE_AA)
                pixels[740:850, 330:570] = area
            frame = folder / f"frame-{i}.png"
            write(frame, pixels)
            frames.append(str(frame))
        config = dict(workflow="city", city="City_RoyalCityLuknalia", bundle=str(bundle),
                      frames=frames, transitions=transitions, run_root=str(folder / "run"),
                      output=str(folder / "output.json"), files=[
                          {"path": p.relative_to(bundle).as_posix(), "sha256": digest(p)}
                          for p in sorted(bundle.rglob("*.png"))])
        config.update(options)
        if options.get("workflow") in ("steel-trial", "repel-forces"):
            # 固定源码存在case，基础目录没有此ID。隔离扩展只提供类型，不伪造路线参数。
            extension = folder / "extension-quests.json"
            extension_id = "steeltrail" if options["workflow"] == "steel-trial" else "repelEnemyForces"
            extension.write_text(json.dumps({extension_id: {"_TYPE": "quest"}}), encoding="utf-8")
            config["quest_catalog"] = str(extension)
        if "mod_images" in options:
            mod = folder / "private-mod"
            (mod / "image").mkdir(parents=True)
            for key, source_key in options["mod_images"].items():
                path = mod / "image" / (key + ".png")
                path.parent.mkdir(parents=True, exist_ok=True)
                write(path, patterns[source_key])
            config["mod_bundle"] = dict(root=str(mod), files=[
                dict(path=p.relative_to(mod).as_posix(), sha256=digest(p)) for p in sorted(mod.rglob("*.png"))])
            if options.get("corrupt_mod_before_publish"):
                for path in mod.rglob("*.png"):
                    path.write_bytes(b"changed before publication")
        if resource_kind in ("chest", "map-confirm", "state-route", "turn", "encounter", "recover", "common", "heal", "dungeon-route", "departure", "inn-tracked", "iteration", "revival"):
            config.update(with_state=True, descriptor=str(ROOT / "packs/wvd/parameters/legacy-config-fields.json"))
        if options.get("workflow") in ("fishing-cast", "fishing-seek"):
            config["with_state"] = False
        if "omit_image" in options:
            config["files"] = [f for f in config["files"] if f["path"] != "image/" + options["omit_image"]]
        source = folder / "input.json"
        source.write_text(json.dumps(config), encoding="utf-8")
        exe = ROOT / "build/m4/Release/test_m4_workflow.exe"
        before_hash = digest(exe)
        # 测试看护预算跟随正式有限定义；不把较短测试超时伪装成产品停止。
        route_budget = 1300 if options.get("profile", {}).get("QUICK_DISARM_CHEST", False) else 1000
        mining_watchdog = (1020 if options.get("attach_recovery") else 900) * (
            options.get("normal_units", 1) + (3 if options.get("attach_recovery") else 0)) + 20
        with (folder / "native.log").open("wb") as log:
            result = subprocess.run([str(exe), str(source)], cwd=folder, env=self.env,
                                    stdout=log, stderr=log, timeout={"dungeon-route": route_budget + 20,
                                        "fortress-trap": (route_budget + 260) * options.get("normal_units", 1),
                                        "giant": (route_budget + 500) * options.get("normal_units", 1),
                                        "recover": 750, "departure": 200, "heal": 260,
                                        "chest": 920 if options.get("quick") else 620,
                                        "fishing-round": 720, "fishing-cast": 110, "fishing-reward": 80, "fishing-seek": 200,
                                        "fishing-cycle": (route_budget + 520) * options.get("normal_units", 1),
                                        "jier": (route_budget + 500) * 3,
                                        "golden-chest": (route_budget + 740) * 2, "sandman": (route_budget + 200) * 2, "gold-income": 1520, "featured-request": 260,
                                        "bull-cave": (route_budget + 400) * (3 if options.get("profile", {}).get("ACTIVE_REST") else 2), "causality": 320,
                                        "steel-trial": (route_budget + 500) * options.get("normal_units", 1),
                                        "repel-forces": (route_budget + 420) * (max(1, options.get("profile", {}).get("REST_INTERVEL", 1)) + 2),
                                        "fordraig": 1820 * 10, "cave-of-separation": 1820 * 6,
                                        "scorpion": (route_budget + 500) * (4 if options.get("hands") else 3), "city-travel": 140, "sleep-batch": 1620 * options.get("normal_units", 1), "bounty-visit": 200 * options.get("normal_units", 1), "manual-separation": 3620, "time-leap": 500 if options.get("causality") else 200, "mining": mining_watchdog, "common": 140, "iteration": (route_budget + 380) * options.get("normal_units", 1)}.get(options.get("workflow"), 90))
        self.assertEqual(digest(exe), before_hash)
        (folder / "execution.json").write_text(json.dumps({"exe_sha256": before_hash, "exit": result.returncode}), encoding="utf-8")
        self.assertEqual(result.returncode, 0, (folder / "native.log").read_text(encoding="utf-8", errors="replace")[-3000:])
        output = json.loads((folder / "output.json").read_text(encoding="utf-8"))
        for module in output["loaded_modules"]:
            self.assertEqual(Path(module["path"]).resolve(), (self.sdk / "bin" / module["name"]).resolve())
            self.assertEqual(module["sha256"], digest(self.sdk / "bin" / module["name"]))
        return output

    @staticmethod
    def turn_profile(level=1, target="next", defend=False):
        return dict(DEFAULT_OVERALL_STRATEGY="Manual", TASK_SPECIFIC_CONFIG=False,
                    STRATEGY=[dict(group_name="Manual", skill_settings=[
                        dict(role_var=role, skill_var="防御" if defend else "左下技能", skill_lvl=level,
                            target_var=target, freq_var="保留原值") for role in ("A", "B")])])

    @staticmethod
    def inn_sequence():
        frames = [{name: (400, 700)} for name in ("Inn", "Stay", "Economy", "OK", "Stay", "Inn")]
        inputs = [dict(kind=0, x=420, y=712)] * 4 + [dict(kind=5, key=4)]
        return frames, inputs

    def test_departure_tracked_inn_does_not_pay_twice(self):
        frames, commands = self.inn_sequence()
        r = self.execute("inn-receipt", frames, commands, workflow="inn-tracked")
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 5)
        self.assertEqual(r["snapshot"]["business"]["inn_rests"], 1)
        self.assertTrue(r["snapshot"]["business"]["inn_rest_completed"])

    def sleep_options(self, **extra):
        return dict(workflow="sleep-batch", quest_catalog=str(ROOT / "packs/wvd/parameters/legacy-quests.json"), **extra)

    def test_sleep_batch_forty_real_inn_visits_are_not_one_receipt(self):
        frames, commands = self.inn_sequence()
        screens = frames[:1] + frames[1:] * 40
        r = self.execute("sleep-forty", screens, commands * 40, **self.sleep_options())
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 200)
        self.assertFalse(r["mismatch"])
        self.assertEqual(r["snapshot"]["business"]["inn_rests"], 40)
        self.assertEqual(r["snapshot"]["business"]["sleep"]["completed_visits"], 40)
        self.assertFalse(r["snapshot"]["business"]["sleep"]["completed"])

    def test_sleep_stop_and_rejected_payment_do_not_retry_or_count(self):
        frames, commands = self.inn_sequence()
        for stop in (False, True):
            actions = commands[:4]
            actions[-1] = dict(actions[-1], reject=not stop, stay=stop)
            r = self.execute("sleep-payment-" + str(stop), frames[:5], actions,
                **self.sleep_options(**({"stop_after_calls": 4} if stop else {})))
            self.assertEqual(r["snapshot"]["state"], "UserStopped" if stop else "Failed", r)
            self.assertEqual(r["backend_calls"], 4)
            self.assertFalse(r["mismatch"])
            self.assertTrue(r["snapshot"]["business"]["inn_payment_pending"])
            self.assertEqual(r["snapshot"]["business"]["sleep"]["completed_visits"], 0)

    def test_sleep_paid_without_exit_never_counts_full_visit(self):
        frames, commands = self.inn_sequence()
        commands[-1] = dict(commands[-1], reject=True)
        r = self.execute("sleep-exit-rejected", frames, commands, **self.sleep_options())
        self.assertEqual(r["snapshot"]["state"], "Failed", r)
        self.assertEqual(r["backend_calls"], 5)
        self.assertFalse(r["mismatch"])
        self.assertEqual(r["snapshot"]["business"]["inn_rests"], 1)
        self.assertEqual(r["snapshot"]["business"]["sleep"]["completed_visits"], 0)

    def test_sleep_unknown_page_never_opens_inn(self):
        r = self.execute("sleep-unknown", [{}], [], **self.sleep_options())
        self.assertEqual(r["snapshot"]["state"], "Interrupted", r)
        self.assertEqual(r["backend_calls"], 0)
        self.assertEqual(r["snapshot"]["business"]["sleep"]["completed_visits"], 0)

    def test_departure_paid_but_exit_failed_preserves_receipt(self):
        frames, commands = self.inn_sequence()
        commands[-1] = dict(kind=5, key=4, reject=True)
        r = self.execute("inn-paid-exit-failed", frames, commands, workflow="inn-tracked")
        self.assertEqual(r["snapshot"]["state"], "Failed", r)
        self.assertEqual(r["backend_calls"], 5)
        self.assertEqual(r["snapshot"]["business"]["inn_rests"], 1)

    def test_departure_payment_intent_survives_stop_and_reject(self):
        for stopped in (False, True):
            with self.subTest(stopped=stopped):
                frames, commands = self.inn_sequence()
                if not stopped:
                    commands[3] = {**commands[3], "reject": True}
                options = {"stop_after_calls": 4} if stopped else {}
                result = self.execute("inn-payment-stop-" + str(stopped), frames, commands,
                    workflow="departure", force_rest=True, attach_recovery=True, **options)
                self.assertEqual(result["snapshot"]["state"], "UserStopped" if stopped else "Failed", result)
                self.assertEqual(result["backend_calls"], 4)
                self.assertFalse(result["mismatch"])
                self.assertTrue(result["snapshot"]["business"]["inn_payment_pending"])
                self.assertFalse(result["snapshot"]["business"]["inn_rest_completed"])
                self.assertEqual(result["snapshot"]["business"]["inn_rests"], 0)
                self.assertEqual(result["lifecycle_calls"], [])

    def test_departure_unknown_payment_post_does_not_retry(self):
        frames, commands = self.inn_sequence()
        result = self.execute("inn-payment-unknown", frames[:4] + [{}], commands[:4],
            workflow="departure", force_rest=True, attach_recovery=True)
        self.assertEqual(result["snapshot"]["state"], "Failed", result)
        self.assertEqual(result["backend_calls"], 4)
        self.assertFalse(result["mismatch"])
        self.assertTrue(result["snapshot"]["business"]["inn_payment_pending"])
        self.assertEqual(result["snapshot"]["business"]["inn_rests"], 0)
        self.assertEqual(result["lifecycle_calls"], [])

    def test_departure_initial_skips_without_encounter(self):
        for symbol in ("Inn", "returntoTown", "openworldmap", "EdgeOfTown"):
            with self.subTest(symbol=symbol):
                r = self.execute("departure-skip-" + symbol, [{symbol: (400, 700)}], [], workflow="departure",
                                 profile=dict(ACTIVE_REST=True, REST_INTERVEL=1))
                self.assertEqual(r["snapshot"]["state"], "Completed", r)
                self.assertEqual(r["backend_calls"], 0)
                self.assertEqual(r["snapshot"]["business"]["inn_rests"], 0)

    def test_departure_forced_rest_ignores_ordinary_interval(self):
        frames, commands = self.inn_sequence()
        r = self.execute("departure-forced", frames, commands, workflow="departure", force_rest=True,
                         profile=dict(ACTIVE_REST=False, REST_INTERVEL=100))
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 5)
        self.assertEqual(r["snapshot"]["business"]["inn_rests"], 1)

    def test_departure_return_town_stops_back_at_inn(self):
        frames, commands = self.inn_sequence()
        r = self.execute("departure-return-town", [{"returntoTown": (400, 700)}] + frames,
                         [dict(kind=5, key=4)] + commands, workflow="departure", force_rest=True)
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 6)

    def test_departure_world_arrival_stops_world_inputs(self):
        r = self.execute("departure-world", [{"worldmapflag": (20, 200), "City_RoyalCityLuknalia": (400, 700)}, {"Inn": (400, 700)}],
                         [dict(kind=0, x=420, y=712)], workflow="departure",
                         return_destination=["City_RoyalCityLuknalia", None, [550, 1]])
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 1)

    def test_departure_prompt_and_uncertain_context(self):
        r = self.execute("departure-prompt", [{"returnText": (400, 700)}, {"Inn": (400, 700)}],
                         [dict(kind=0, x=420, y=712)], workflow="departure")
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        for symbol in ("Stay", "worldmapflag", "mapFlag"):
            with self.subTest(symbol=symbol):
                r = self.execute("departure-uncertain-" + symbol, [{symbol: (400, 700)}], [], workflow="departure")
                self.assertEqual(r["snapshot"]["state"], "Interrupted", r)
                self.assertEqual(r["backend_calls"], 0)

    def test_departure_stop_and_reject_do_not_confirm_rest(self):
        for stop in (False, True):
            frames, commands = self.inn_sequence()
            if not stop:
                commands[0] = dict(kind=0, x=420, y=712, reject=True)
            r = self.execute("departure-stop" if stop else "departure-reject", frames, commands,
                             workflow="departure", force_rest=True, stop_after_first=stop)
            self.assertEqual(r["snapshot"]["state"], "UserStopped" if stop else "Failed", r)
            self.assertEqual(r["backend_calls"], 1)
            self.assertFalse(r["snapshot"]["business"]["inn_rest_completed"])

    @staticmethod
    def turn_screen(role="A", **extra):
        return {"combatActive": (10, 5), f"spellskill/char/{role}": (24, 55), "flee": (750, 1150), **extra}

    def test_turn_enemy_stops_after_detail_closes(self):
        menu = self.turn_screen()
        detail = self.turn_screen(**{"spellskill/skillDetail": (350, 950), "next": (500, 300)})
        r = self.execute("turn-enemy", [menu, detail, detail, self.turn_screen("B")],
                         [dict(kind=0, x=266, y=1054), dict(kind=0, x=440, y=392), dict(kind=0, x=520, y=392)],
                         workflow="turn", profile=self.turn_profile())
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 3)
        self.assertFalse(r["mismatch"])
        rows = r["snapshot"]["business"]["strategy"]["current"]["skill_settings"]
        self.assertEqual([row["role_var"] for row in rows], ["B"])

    def test_turn_level_and_area_confirmation(self):
        detail = self.turn_screen(**{"spellskill/skillDetail": (350, 950), "OK": (500, 1480),
                                    "spellskill/skillLvl/lv1": (250, 1320), "spellskill/skillLvl/s_lv2": (400, 1320)})
        r = self.execute("turn-aoe", [self.turn_screen(), detail, detail, self.turn_screen("B")],
                         [dict(kind=0, x=266, y=1054), dict(kind=0, x=420, y=1332), dict(kind=0, x=520, y=1492)],
                         workflow="turn", profile=self.turn_profile(level=2))
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 3)
        self.assertFalse(r["mismatch"])

    def test_turn_missing_target_closes_popup_before_auto(self):
        detail = self.turn_screen(**{"spellskill/skillDetail": (350, 950), "close": (350, 1490)})
        disabled = self.turn_screen(**{"spellskill/CombatAutoDisable": (810, 1060)})
        enabled = self.turn_screen(**{"spellskill/CombatAutoEnable": (810, 1060)})
        r = self.execute("turn-auto", [self.turn_screen(), detail, disabled, enabled, disabled],
                         [dict(kind=0, x=266, y=1054), dict(kind=0, x=370, y=1502),
                          dict(kind=0, x=850, y=1100), dict(kind=0, x=850, y=1100)],
                         workflow="turn", profile=self.turn_profile())
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 4)
        self.assertFalse(r["mismatch"])
        self.assertEqual(len(r["snapshot"]["business"]["strategy"]["current"]["skill_settings"]), 1)

    def test_turn_failed_input_and_stop_do_not_consume(self):
        for stopped in (False, True):
            commands = [dict(kind=0, x=266, y=1054, **({"stay": True} if stopped else {"reject": True}))]
            r = self.execute(f"turn-fail-{stopped}", [self.turn_screen()], commands,
                             workflow="turn", profile=self.turn_profile(), stop_after_first=stopped)
            self.assertNotEqual(r["snapshot"]["state"], "Completed", r)
            self.assertEqual(r["backend_calls"], 1)
            self.assertEqual(len(r["snapshot"]["business"]["strategy"]["current"]["skill_settings"]), 2)

    def test_turn_low_confidence_and_edge_clipping(self):
        detail = self.turn_screen(**{"spellskill/skillDetail": (350, 950), "next": (850, 850)})
        r = self.execute("turn-low-edge", [self.turn_screen(), detail, detail, self.turn_screen("B")],
                         [dict(kind=0, x=266, y=1054), dict(kind=0, x=790, y=900), dict(kind=0, x=870, y=900)],
                         workflow="turn", profile=self.turn_profile(), degraded_next_frames=[1, 2])
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 3)
        self.assertFalse(r["mismatch"])

    def test_turn_ally_selection_precedes_ok(self):
        detail = self.turn_screen(**{"spellskill/skillDetail": (350, 950), "supportSkillCheck": (720, 1500)})
        selected = {**detail, "OK": (500, 1480)}
        r = self.execute("turn-support", [self.turn_screen(), detail, selected, self.turn_screen("B")],
                         [dict(kind=0, x=266, y=1054), dict(kind=0, x=450, y=1400), dict(kind=0, x=520, y=1492)],
                         workflow="turn", profile=self.turn_profile(target="中下角色"))
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 3)
        self.assertFalse(r["mismatch"])

    def test_turn_unmatched_actor_does_not_consume(self):
        profile = self.turn_profile()
        profile["STRATEGY"][0]["skill_settings"] = profile["STRATEGY"][0]["skill_settings"][:1]
        disabled = self.turn_screen("B", **{"spellskill/CombatAutoDisable": (810, 1060)})
        enabled = self.turn_screen("B", **{"spellskill/CombatAutoEnable": (810, 1060)})
        r = self.execute("turn-unmatched", [disabled, enabled, disabled],
                         [dict(kind=0, x=850, y=1100), dict(kind=0, x=850, y=1100)], workflow="turn", profile=profile)
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 2)
        self.assertFalse(r["mismatch"])
        self.assertEqual(len(r["snapshot"]["business"]["strategy"]["current"]["skill_settings"]), 1)

    def test_turn_uses_best_actor_and_defend_waits_for_change(self):
        menu = self.turn_screen("B")
        r = self.execute("turn-defend", [menu, menu, self.turn_screen()],
                         [dict(kind=0, x=513, y=1200), dict(kind=0, x=513, y=1200)],
                         workflow="turn", profile=self.turn_profile(defend=True))
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 2)
        self.assertFalse(r["mismatch"])
        self.assertEqual([v["role_var"] for v in r["snapshot"]["business"]["strategy"]["current"]["skill_settings"]], ["A"])

    def test_turn_speed_and_three_open_attempts(self):
        menu = self.turn_screen()
        detail = self.turn_screen(**{"spellskill/skillDetail": (350, 950), "next": (500, 300)})
        r = self.execute("turn-three-open", [{**menu, "combatSpd_DHI": (20, 1050)}, menu, menu, menu, detail, self.turn_screen("B")],
                         [dict(kind=0, x=40, y=1062)] + [dict(kind=0, x=266, y=1054)] * 3 + [dict(kind=0, x=440, y=392)],
                         workflow="turn", profile=self.turn_profile())
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 5)
        self.assertFalse(r["mismatch"])

    def test_turn_resource_shortage_retries_level_one_once(self):
        initial = self.turn_screen(**{"spellskill/skillDetail": (350, 950), "OK": (500, 1480),
                                     "spellskill/skillLvl/lv1": (250, 1320), "spellskill/skillLvl/lv2": (400, 1320)})
        low = self.turn_screen(**{"spellskill/skillDetail": (350, 950), "OK": (500, 1480),
                                 "spellskill/skillLvl/lv1": (250, 1320)})
        error = self.turn_screen(**{"notenoughmp": (300, 800)})
        screens = [self.turn_screen(), initial, initial, error, self.turn_screen(), low, low]
        commands = [dict(kind=0, x=266, y=1054), dict(kind=0, x=420, y=1332), dict(kind=0, x=520, y=1492),
                    dict(kind=5, key=4), dict(kind=0, x=266, y=1054), dict(kind=0, x=270, y=1332), dict(kind=0, x=520, y=1492)]
        for exhausted in (False, True):
            r = self.execute(f"turn-resource-{exhausted}", screens + [error if exhausted else self.turn_screen("B")],
                             commands, workflow="turn", profile=self.turn_profile(level=2))
            self.assertEqual(r["snapshot"]["state"], "Interrupted" if exhausted else "Completed", r)
            self.assertEqual(r["backend_calls"], 7)
            self.assertFalse(r["mismatch"])
            self.assertEqual(len(r["snapshot"]["business"]["strategy"]["current"]["skill_settings"]), 2 if exhausted else 1)

    def test_turn_changed_actor_with_detail_does_not_receive_old_clicks(self):
        detail = self.turn_screen(**{"spellskill/skillDetail": (350, 950), "next": (500, 300)})
        wrong_actor = self.turn_screen("B", **{"spellskill/skillDetail": (350, 950), "next": (500, 300)})
        r = self.execute("turn-stale-actor", [self.turn_screen(), detail, wrong_actor],
                         [dict(kind=0, x=266, y=1054), dict(kind=0, x=440, y=392)],
                         workflow="turn", profile=self.turn_profile())
        self.assertNotEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 2)
        self.assertFalse(r["mismatch"])
        self.assertEqual(len(r["snapshot"]["business"]["strategy"]["current"]["skill_settings"]), 2)

    def test_city_stops_at_arrival(self):
        world = {"worldmapflag": (80, 100), "City_RoyalCityLuknalia": (132, 1352)}
        for miss_count in range(5):
            with self.subTest(miss_count=miss_count):
                offsets = [(0, 0), (0, -55), (-35, -35), (35, -35), (0, 35)]
                commands = [dict(kind=0, x=152+dx, y=1364+dy) for dx,dy in offsets[:miss_count+1]]
                result = self.execute(f"city-{miss_count}", [world]*(miss_count+1) + [{"Inn": (100, 400)}], commands)
                self.assertEqual(result["snapshot"]["state"], "Completed", result)
                self.assertEqual(result["backend_calls"], miss_count+1)
                self.assertEqual(result["cursor"], miss_count+1)
                self.assertFalse(result["mismatch"])

    def test_auto_route_expands_then_requires_no_target_notice(self):
        initial = {"dungFlag": (50, 150)}
        ready = {**initial, "chest_auto": (730, 270), "chest_auto_minus": (811, 340)}
        done = {**initial, "NoChestCanBeFound": (300, 700)}
        r = self.execute("auto-route-success", [initial, ready, done],
                         [dict(kind=0, x=762, y=346), dict(kind=0, x=750, y=282)], workflow="auto-route")
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 2)
        self.assertFalse(r["mismatch"])

    def test_auto_route_disabled_interrupted_and_stay_are_not_point_completion(self):
        ready = {"dungFlag": (50, 150), "chest_auto": (730, 270), "chest_auto_minus": (811, 340)}
        disabled = {k: v for k, v in ready.items() if k != "chest_auto_minus"}
        for name, screens, actions, target in [
            ("disabled", [disabled, disabled], [dict(kind=0, x=750, y=282)], "chest_auto"),
            ("battle", [ready, {"combatActive": (10, 5)}], [dict(kind=0, x=750, y=282)], "chest_auto"),
            ("stay", [{"dungFlag": (50, 150)}], [], "stay"),
        ]:
            r = self.execute("auto-route-" + name, screens, actions, workflow="auto-route", auto_target=target)
            self.assertEqual(r["snapshot"]["state"], "Interrupted", r)
            self.assertEqual(r["backend_calls"], len(actions))
            self.assertFalse(r["mismatch"])

    def test_auto_route_retreat_mark_and_stopped_have_distinct_outcomes(self):
        dungeon = {"dungFlag": (50, 150)}
        cases = [
            ("retreat", {**dungeon, "dungFlag@button": (730, 270)}, {"Inn": (100, 400)}, "dungFlag", "Completed"),
            ("mark", {**dungeon, "mark_auto": (730, 270)}, {**dungeon, "theRouteToTheDestinationCannotBeFound": (300, 700)}, "mark_auto", "Completed"),
            ("stopped", {**dungeon, "chest_auto": (730, 270), "chest_auto_minus": (811, 340)}, dungeon, "chest_auto", "Interrupted"),
        ]
        for name, before, after, target, expected in cases:
            r = self.execute("auto-route-" + name, [before, after], [dict(kind=0, x=750, y=282)], workflow="auto-route", auto_target=target)
            self.assertEqual(r["snapshot"]["state"], expected, r)
            self.assertEqual(r["backend_calls"], 1)
            self.assertFalse(r["mismatch"])

    def test_entry_fallback_array_is_sequential_and_goto_is_confirmed(self):
        steps = [["press", "TradeWaterway", ["EdgeOfTown", [1, 1]], 1],
                 ["press", "Dist", "input swipe 650 250 650 900", 1]]
        screens = [{"Inn": (100, 400), "EdgeOfTown": (300, 600)}, {"TradeWaterway": (200, 700)},
                   {"TradeWaterway": (200, 700)}, {"Dist": (400, 700)}, {"GotoDung": (500, 900)}, {"dungFlag": (50, 150)}]
        commands = [dict(kind=0, x=320, y=612), dict(kind=0, x=1, y=1), dict(kind=0, x=220, y=712),
                    dict(kind=0, x=420, y=712), dict(kind=0, x=520, y=912)]
        r = self.execute("entry-sequence", screens, commands, workflow="entry", entry_steps=steps)
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 5)
        self.assertFalse(r["mismatch"])

    def test_entry_does_not_complete_on_last_destination_click(self):
        r = self.execute("entry-not-entered", [{"Dist": (400, 700)}] * 2, [dict(kind=0, x=420, y=712)],
                         workflow="entry", entry_steps=[["press", "Dist", None, 1]])
        self.assertEqual(r["snapshot"]["state"], "Interrupted", r)
        self.assertEqual(r["backend_calls"], 1)
        self.assertFalse(r["mismatch"])

    def test_entry_event_opens_before_zone_and_stops_on_input_failure(self):
        steps = [["press", "EVENT", "FFXI/EVENT_GCN", 1], ["press", "FFXI/zone5", [1, 1], 1]]
        screens = [{"Inn": (100, 400), "EVENT": (200, 500)}, {"EVENT": (200, 500)},
                   {"FFXI/EVENT_GCN": (300, 700)}, {"openworldmap": (300, 100), "FFXI/zone5": (400, 700)}, {"dungFlag": (50, 150)}]
        commands = [dict(kind=0, x=1, y=1), dict(kind=0, x=220, y=512), dict(kind=0, x=320, y=712), dict(kind=0, x=420, y=712)]
        r = self.execute("entry-event", screens, commands, workflow="entry", entry_steps=steps)
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 4)
        self.assertFalse(r["mismatch"])
        r = self.execute("entry-event-fail", screens[:1], [{**commands[0], "reject": True}], workflow="entry", entry_steps=steps)
        self.assertEqual(r["snapshot"]["state"], "Failed", r)
        self.assertEqual(r["backend_calls"], 1)

    def test_entry_precheck_world_and_destination_are_sequential(self):
        steps = [["press", "intoWorldMap", ["City_RoyalCityLuknalia", "input swipe 400 400 500 500"], 1],
                 ["press", "Dist", [1, 1], 1]]
        city = {"Inn": (100, 400), "intoWorldMap": (100, 700)}
        screens = [{**city, "preGate": (300, 500)}, city,
                   {"worldmapflag": (80, 100), "City_RoyalCityLuknalia": (500, 700)},
                   {"openworldmap": (300, 100), "Dist": (400, 700)}, {"GotoDung": (500, 900)}, {"dungFlag": (50, 150)}]
        commands = [dict(kind=0, x=320, y=512), dict(kind=0, x=120, y=712), dict(kind=0, x=520, y=712),
                    dict(kind=0, x=420, y=712), dict(kind=0, x=520, y=912)]
        r = self.execute("entry-world", screens, commands, workflow="entry", entry_steps=steps, pre_entry="preGate")
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 5)
        self.assertFalse(r["mismatch"])

    def test_city_already_arrived_and_wrong_scene(self):
        for name, screen, state in [("arrived", {"Inn": (100, 400)}, "Completed"),
                                    ("no-world", {"City_RoyalCityLuknalia": (132, 1352)}, "Interrupted")]:
            result = self.execute(name, [screen], [])
            self.assertEqual(result["snapshot"]["state"], state, result)
            self.assertEqual(result["backend_calls"], 0)

    def test_city_travel_start_inn_is_not_destination(self):
        start = {"Inn": (100, 400), "intoWorldMap": (300, 600)}
        world = {"worldmapflag": (80, 100), "City_RoyalCityLuknalia": (132, 1352)}
        r = self.execute("city-travel-full", [start, world, {"Inn": (100, 400)}],
            [dict(kind=0, x=320, y=612), dict(kind=0, x=152, y=1364)], workflow="city-travel")
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 2)
        self.assertFalse(r["mismatch"])

    def test_city_travel_unknown_or_inn_without_world_button_is_not_arrival(self):
        for name, screen in [("unknown", {}), ("inn-only", {"Inn": (100, 400)})]:
            r = self.execute("city-travel-" + name, [screen], [], workflow="city-travel")
            self.assertEqual(r["snapshot"]["state"], "Interrupted", r)
            self.assertEqual(r["backend_calls"], 0)

    def test_city_travel_stop_and_reject_do_not_click_destination(self):
        start = {"Inn": (100, 400), "intoWorldMap": (300, 600)}
        for stop in (False, True):
            r = self.execute("city-travel-stop-" + str(stop), [start, start],
                [dict(kind=0, x=320, y=612, reject=not stop)], workflow="city-travel", stop_after_first=stop)
            self.assertEqual(r["snapshot"]["state"], "UserStopped" if stop else "Failed", r)
            self.assertEqual(r["backend_calls"], 1)
            self.assertFalse(r["mismatch"])

    def test_city_offset_clipping(self):
        world = {"worldmapflag": (80, 100), "City_RoyalCityLuknalia": (0, 0)}
        result = self.execute("clipped", [world, world, {"Inn": (100, 400)}],
                              [dict(kind=0, x=20, y=12), dict(kind=0, x=20, y=1)])
        self.assertEqual(result["snapshot"]["state"], "Completed", result)
        self.assertFalse(result["mismatch"])

    def test_inn_requires_actual_stay_and_return(self):
        for royal, available in [(False, False), (True, True), (True, False)]:
            selection = {"Economy": (100, 500)}
            if available:
                selection["royalsuite"] = (400, 500)
            screens = [{"Inn": (100, 400)}, {"Stay": (100, 600)}, selection,
                       {"OK": (500, 800)}, {"Stay": (100, 600)}, {"Inn": (100, 400)}]
            commands = [dict(kind=0, x=120, y=412), dict(kind=0, x=120, y=612),
                        dict(kind=0, x=420 if available else 120, y=512),
                        dict(kind=0, x=520, y=812), dict(kind=5, key=4)]
            result = self.execute(f"inn-{royal}-{available}", screens, commands,
                                  workflow="inn", royal=royal)
            self.assertEqual(result["snapshot"]["state"], "Completed", result)
            self.assertEqual(result["cursor"], 5)
            self.assertEqual(result["backend_calls"], 5)
            self.assertFalse(result["mismatch"])

    def test_input_failure_is_not_arrival(self):
        world = {"worldmapflag": (80, 100), "City_RoyalCityLuknalia": (132, 1352)}
        result = self.execute("reject", [world], [dict(kind=0, x=152, y=1364, reject=True)])
        self.assertEqual(result["snapshot"]["state"], "Failed", result)
        self.assertEqual(result["cursor"], 0)
        self.assertEqual(result["backend_calls"], 1)

    def test_compiler_rejects_unsafe_graph(self):
        for case, error in [("raw-input", "COMPILE_UNGUARDED_ACTION"),
                            ("unknown-next", "COMPILE_NEXT_UNKNOWN"),
                            ("unbounded", "COMPILE_UNBOUNDED_NODE"),
                            ("empty", "COMPILE_ENTRY_INVALID"),
                            ("missing-terminal", "COMPILE_ENTRY_INVALID"),
                            ("stale-images", "COMPILE_RESOURCE_INDEX_STALE"),
                            ("shell-permission", "COMPILE_ACTION_PERMISSION_INVALID"),
                            ("unknown-recognition", "COMPILE_RECOGNITION_UNKNOWN")]:
            result = self.execute(case, [{}], [], invalid=case)
            self.assertEqual(result["error"], error)
            self.assertEqual(result["backend_calls"], 0)

    def test_child_repeated_calls_reset_native_hit_budget(self):
        leg = [{"Inn": (100, 400)}, {"Stay": (100, 600)}, {"Economy": (100, 500)},
               {"OK": (500, 800)}, {"Stay": (100, 600)}]
        actions = [dict(kind=0, x=120, y=412), dict(kind=0, x=120, y=612), dict(kind=0, x=120, y=512),
                   dict(kind=0, x=520, y=812), dict(kind=5, key=4)]
        for nested in (False, True):
            r = self.execute(f"child-repeat-{nested}", leg * 3 + [leg[0]], actions * 3,
                             workflow="child", nested_child=nested)
            self.assertEqual(r["snapshot"]["state"], "Completed", r)
            self.assertEqual(r["backend_calls"], 15)
            self.assertEqual(r["cursor"], 15)
            self.assertFalse(r["mismatch"])

    def test_child_failure_and_stop_do_not_start_later_calls(self):
        for stop in (False, True):
            r = self.execute(f"child-stop-{stop}", [{"Inn": (100, 400)}, {"Stay": (100, 600)}],
                [dict(kind=0, x=120, y=412, reject=not stop)], workflow="child", stop_after_first=stop)
            self.assertEqual(r["snapshot"]["state"], "UserStopped" if stop else "Failed", r)
            self.assertEqual(r["backend_calls"], 1)
            self.assertTrue(r["snapshot"]["quiescent"])

    def test_child_compiler_rejects_undeclared_recursion_and_crossing(self):
        for case, error in [("child-unknown", "COMPILE_NEXT_UNKNOWN"),
                            ("child-recursive", "COMPILE_CHILD_RECURSIVE"),
                            ("child-override", "COMPILE_CHILD_PARAMETERS_INVALID"),
                            ("child-crossing", "COMPILE_CHILD_BOUNDARY_CROSSED"),
                            ("child-reset-parent", "COMPILE_CHILD_RESET_SCOPE_INVALID"),
                            ("child-shared-depth", "COMPILE_CHILD_DEPTH_LIMIT")]:
            r = self.execute(case, [{}], [], workflow="child", invalid=case)
            self.assertEqual(r["error"], error)
            self.assertEqual(r["backend_calls"], 0)

    def test_publish_rejects_missing_assets_and_binding_before_connect(self):
        for name, options, error in [
            ("missing-image", {"omit_image": "Inn.png"}, "COMPILE_IMAGE_NOT_IN_MANIFEST:image/Inn.png"),
            ("bad-alias", {"aliases": {"Inn.png": "not-present.png"}}, "COMPILE_IMAGE_NOT_IN_MANIFEST:image/not-present.png"),
            ("missing-binding", {"missing_binding": True}, "RECO_IMPLEMENTATION_UNKNOWN"),
            ("existing-destination", {"destination_exists": True}, "COMPILE_DESTINATION_EXISTS_OR_INVALID")]:
            result = self.execute(name, [{}], [], **options)
            self.assertEqual(result["publish_error"], error, result)
            self.assertEqual(result["backend_calls"], 0)
            self.assertEqual(result["connections"], 0)

    def test_stuck_city_has_finite_recovery_exit(self):
        world = {"worldmapflag": (80, 100), "City_RoyalCityLuknalia": (132, 1352)}
        offsets = [(0, 0), (0, -55), (-35, -35), (35, -35), (0, 35)] * 5
        result = self.execute("city-budget", [world] * 26,
                              [dict(kind=0, x=152+dx, y=1364+dy) for dx,dy in offsets])
        self.assertEqual(result["snapshot"]["state"], "Interrupted", result)
        self.assertEqual(result["snapshot"]["reason"], "RECOVERY_REQUIRED", result)
        self.assertEqual(result["backend_calls"], 25)
        self.assertFalse(result["mismatch"])

    def test_auto_closes_cover_before_enabling(self):
        disabled = {"combatActive": (20, 20), "spellskill/CombatAutoDisable": (800, 1070)}
        enabled = {"combatActive": (20, 20), "spellskill/CombatAutoEnable": (800, 1070)}
        for name, overlay, command in [
            ("close", {"spellskill/skillDetail": (300, 900), "close": (400, 1450)}, dict(kind=0, x=420, y=1462)),
            ("cancel", {"OK": (500, 1450)}, dict(kind=0, x=240, y=1462)),
            ("back", {"spellskill/skillDetail": (300, 900)}, dict(kind=5, key=4))]:
            result = self.execute("auto-" + name, [{**disabled, **overlay}, disabled, enabled],
                                  [command, dict(kind=0, x=850, y=1100)], workflow="auto")
            self.assertEqual(result["snapshot"]["state"], "Completed", result)
            self.assertEqual(result["backend_calls"], 2)
            self.assertFalse(result["mismatch"])

    def test_auto_already_enabled_never_toggles_off(self):
        result = self.execute("auto-enabled", [{"combatActive": (20, 20),
                              "spellskill/CombatAutoEnable": (800, 1070)}], [], workflow="auto")
        self.assertEqual(result["snapshot"]["state"], "Completed", result)
        self.assertEqual(result["backend_calls"], 0)

    def test_auto_unknown_fallback_requires_confirmation(self):
        unknown = {"combatActive": (20, 20)}
        for success in [True, False]:
            after = {**unknown, "spellskill/CombatAutoEnable": (800, 1070)} if success else unknown
            result = self.execute("auto-unknown-" + str(success), [unknown, after],
                                  [dict(kind=0, x=850, y=1100)], workflow="auto")
            self.assertEqual(result["snapshot"]["state"], "Completed" if success else "Interrupted", result)
            self.assertEqual(result["backend_calls"], 1)
            self.assertFalse(result["mismatch"])

    def test_auto_popup_retry_is_bounded(self):
        popup = {"combatActive": (20, 20), "spellskill/CombatAutoDisable": (800, 1070),
                 "spellskill/skillDetail": (300, 900), "close": (400, 1450)}
        result = self.execute("auto-popup-stuck", [popup] * 4,
                              [dict(kind=0, x=420, y=1462)] * 3, workflow="auto")
        self.assertEqual(result["snapshot"]["state"], "Interrupted", result)
        self.assertEqual(result["backend_calls"], 3)
        self.assertFalse(result["mismatch"])

    def test_composed_city_does_not_finish_root_before_rest(self):
        world = {"worldmapflag": (80, 100), "City_RoyalCityLuknalia": (132, 1352)}
        screens = [world, {"Inn": (100, 400)}, {"Stay": (100, 600)},
                   {"Economy": (100, 500)}, {"OK": (500, 800)},
                   {"Stay": (100, 600)}, {"Inn": (100, 400)}]
        commands = [dict(kind=0, x=152, y=1364), dict(kind=0, x=120, y=412),
                    dict(kind=0, x=120, y=612), dict(kind=0, x=120, y=512),
                    dict(kind=0, x=520, y=812), dict(kind=5, key=4)]
        result = self.execute("city-rest", screens, commands, workflow="city-inn")
        self.assertEqual(result["snapshot"]["state"], "Completed", result)
        self.assertEqual(result["cursor"], 6)
        self.assertFalse(result["mismatch"])
        failed = self.execute("city-rest-failed", screens[:2],
                              [commands[0], {**commands[1], "reject": True}], workflow="city-inn")
        self.assertEqual(failed["snapshot"]["state"], "Failed", failed)
        self.assertEqual(failed["cursor"], 1)

    def test_map_arrival_and_floor_are_not_input_success(self):
        base = {"mapFlag": (100, 100)}
        cases = [("reached", {**base, "cursor_0": (480, 588)}, ["position", [None], [500, 600]], {}, "Completed"),
                 ("stair", base, ["stair_up", [None], [500, 600]], {}, "Completed"),
                 ("floor", base, ["position", [None], [500, 600]], {"floor": "stair_floor"}, "Interrupted")]
        for name, screen, target, options, state in cases:
            result = self.execute("map-" + name, [screen], [], workflow="map", map_target=target, **options)
            self.assertEqual(result["snapshot"]["state"], state, result)
            self.assertEqual(result["backend_calls"], 0)

    def test_map_movement_reopens_and_confirms_position(self):
        base = {"mapFlag": (100, 100)}
        result = self.execute("map-moving", [base, base, {"dungFlag": (100, 1400)},
                              {**base, "cursor_0": (480, 588)}],
                              [dict(kind=0, x=500, y=600), dict(kind=0, x=136, y=1431),
                               dict(kind=0, x=777, y=150)],
                              workflow="map", map_target=["position", [None], [500, 600]])
        self.assertEqual(result["snapshot"]["state"], "Completed", result)
        self.assertEqual(result["cursor"], 3)
        self.assertFalse(result["mismatch"])

    def test_map_transition_revokes_old_move(self):
        base = {"mapFlag": (100, 100)}
        for name, new_scene in [("combat", {"combatActive": (20, 20)}),
                                 ("chest", {"chestFlag": (300, 400)})]:
            result = self.execute("map-interrupt-" + name, [base, new_scene],
                                  [dict(kind=0, x=500, y=600)], workflow="map",
                                  map_target=["position", [None], [500, 600]])
            self.assertEqual(result["snapshot"]["state"], "Interrupted", result)
            self.assertEqual(result["backend_calls"], 1)
            self.assertFalse(result["mismatch"])

    def test_map_automove_freeze_has_no_repeat_clicks(self):
        base = {"mapFlag": (100, 100)}
        result = self.execute("map-frozen", [base, base, {**base, "AutoMove": (200, 400)}],
                              [dict(kind=0, x=500, y=600), dict(kind=0, x=136, y=1431)],
                              workflow="map", map_target=["position", [None], [500, 600]])
        self.assertEqual(result["snapshot"]["state"], "Interrupted", result)
        self.assertEqual(result["backend_calls"], 2)
        self.assertFalse(result["mismatch"])

    def test_map_chest_search_uses_each_direction_and_exclusions(self):
        base = {"mapFlag": (100, 100)}
        commands = [dict(kind=1, x=100, y=250, x2=700, y2=1200, duration=400),
                    dict(kind=1, x=700, y=1200, x2=100, y2=250, duration=400)]
        result = self.execute("map-search", [base] * 3, commands, workflow="map",
                              map_target=["chest", [[100, 250, 700, 1200], [700, 1200, 100, 250]]])
        self.assertEqual(result["snapshot"]["state"], "Completed", result)
        self.assertEqual(result["cursor"], 2)
        self.assertFalse(result["mismatch"])
        excluded = self.execute("map-excluded", [{**base, "chest": (200, 1300)}], [],
                                workflow="map", map_target=["chest", [None]])
        self.assertEqual(excluded["snapshot"]["state"], "Completed", excluded)
        self.assertEqual(excluded["backend_calls"], 0)

    def test_business_confirmation_updates_point_once(self):
        screen = {"mapFlag": (100, 100), "cursor_0": (480, 588)}
        result = self.execute("point-confirmed", [screen], [], workflow="map-confirm",
                              map_target=["position", [None], [500, 600]])
        self.assertEqual(result["snapshot"]["state"], "Completed", result)
        self.assertEqual(result["snapshot"]["business"]["task_step"], 1)
        self.assertEqual(result["snapshot"]["business"]["confirmed_operations"], 1)
        self.assertEqual(result["backend_calls"], 0)
        wrong = self.execute("point-wrong-step", [screen], [], workflow="map-confirm",
                             map_target=["position", [None], [500, 600]], expected_step=2)
        self.assertEqual(wrong["snapshot"]["state"], "Failed", wrong)
        self.assertEqual(wrong["snapshot"]["business"]["task_step"], 0)
        self.assertEqual(wrong["snapshot"]["business"]["confirmed_operations"], 0)

    def test_business_confirmation_rejects_stale_frame_without_advancing_point(self):
        screen = {"mapFlag": (100, 100), "cursor_0": (480, 588)}
        r = self.execute("point-stale", [screen], [], workflow="map-confirm",
                         map_target=["position", [None], [500, 600]], returned_frame_age_ms=3000)
        self.assertEqual(r["snapshot"]["state"], "Failed", r)
        self.assertEqual(r["snapshot"]["reason"], "BUSINESS_CONFIRMATION_STALE")
        self.assertEqual(r["snapshot"]["business"]["task_step"], 0)
        self.assertEqual(r["snapshot"]["business"]["confirmed_operations"], 0)
        self.assertEqual(r["backend_calls"], 0)
        self.assertTrue(r["snapshot"]["quiescent"])

    def test_business_confirmation_rejects_stale_actor_before_preparing_skill(self):
        r = self.execute("actor-stale", [self.turn_screen("B")], [], workflow="turn",
                         profile=self.turn_profile(defend=True), returned_frame_age_ms=3000)
        self.assertEqual(r["snapshot"]["state"], "Failed", r)
        self.assertEqual(r["snapshot"]["reason"], "COMBAT_CONFIRMATION_STALE")
        self.assertFalse(r["snapshot"]["business"]["has_prepared_skill"])
        self.assertEqual(len(r["snapshot"]["business"]["strategy"]["current"]["skill_settings"]), 2)
        self.assertEqual(r["backend_calls"], 0)

    def test_business_confirmation_stale_karma_cannot_prepare_or_write_profile(self):
        r = self.execute("karma-stale", [{"ambush": (300, 700), "ignore": (500, 700)}], [],
                         workflow="common", profile={"KARMA_ADJUST": "+0"}, karma_profile=True,
                         returned_frame_age_ms=3000)
        self.assertEqual(r["snapshot"]["state"], "Failed", r)
        self.assertEqual(r["snapshot"]["reason"], "BUSINESS_CONFIRMATION_STALE")
        self.assertFalse(r["snapshot"]["business"]["karma_pending"])
        self.assertEqual(r["profile_before"], r["profile_after"])
        self.assertEqual(r["backend_calls"], 0)

    def test_chest_confirms_only_after_dungeon_return(self):
        for preferred in range(1, 7):
            x = 258 + ((preferred - 1) % 3) * 258
            y = 1161 + ((preferred - 1) // 3) * 184
            screens = [{"chestFlag": (300, 400)}, {"whowillopenit": (200, 500)},
                       {"chestOpening": (300, 500)}, {"dungFlag": (100, 1400)}]
            result = self.execute(f"chest-character-{preferred}", screens,
                                  [dict(kind=0, x=320, y=412), dict(kind=0, x=x, y=y),
                                   dict(kind=0, x=515, y=934)], workflow="chest", preferred=preferred)
            self.assertEqual(result["snapshot"]["state"], "Completed", result)
            self.assertEqual(result["snapshot"]["business"]["chests"], 1)
            self.assertEqual(result["snapshot"]["business"]["confirmed_operations"], 3)
            self.assertEqual(result["snapshot"]["business"]["chest_character_attempts"], 1)
            self.assertEqual(result["backend_calls"], 3)
            self.assertFalse(result["mismatch"])

    def test_chest_normal_retries_opening_after_eight_inputs(self):
        result = self.execute("chest-opening-retry", [{"chestOpening": (300, 500)}] * 9 + [{"dungFlag": (50, 150)}],
            [dict(kind=0, x=515, y=934)] * 9, workflow="chest")
        self.assertEqual(result["snapshot"]["state"], "Completed", result)
        self.assertEqual(result["backend_calls"], 9)
        self.assertFalse(result["mismatch"])
        self.assertEqual(result["snapshot"]["business"]["chests"], 1)
        self.assertEqual(result["snapshot"]["business"]["chest_character_attempts"], 0)

    def test_chest_normal_keeps_fear_pool_between_rounds(self):
        choosing = {"whowillopenit": (200, 500)}
        restricted = dict(choosing)
        for i in (0, 2, 3, 4, 5):
            restricted[f"chestfear@{i}"] = (258 + (i % 3) * 258 - 20, 1161 + (i // 3) * 184 - 12)
        opening = {"chestOpening": (300, 500)}
        screens = [restricted] + [opening] * 8 + [choosing, opening, {"dungFlag": (50, 150)}]
        commands = [dict(kind=0, x=516, y=1161)] + [dict(kind=0, x=515, y=934)] * 8
        commands += [dict(kind=0, x=516, y=1161), dict(kind=0, x=515, y=934)]
        result = self.execute("chest-persistent-fear", screens, commands, workflow="chest", preferred=1)
        self.assertEqual(result["snapshot"]["state"], "Completed", result)
        self.assertEqual(result["backend_calls"], 11)
        self.assertFalse(result["mismatch"])
        self.assertEqual(result["snapshot"]["business"]["chest_available_mask"], 2)
        self.assertEqual(result["snapshot"]["business"]["chest_character_attempts"], 2)

    def test_chest_quick_full_attempts_and_three_fallbacks(self):
        opening = {"chestOpening": (300, 500)}
        screens = [{"chestFlag": (300, 400)}] + [{"whowillopenit": (200, 500)}] * 3 + [opening] * 36 + [{"dungFlag": (50, 150)}]
        commands = [dict(kind=0, x=320, y=412)] + [dict(kind=0, x=774, y=1345)] * 3
        commands += [dict(kind=0, x=515, y=934)] * 30
        commands += [command for _ in range(3) for command in (dict(kind=0, x=1, y=1), dict(kind=0, x=515, y=934))]
        result = self.execute("chest-quick-full", screens, commands, workflow="chest", quick=True, preferred=0)
        self.assertEqual(result["snapshot"]["state"], "Completed", result)
        self.assertEqual(result["backend_calls"], 40)
        self.assertFalse(result["mismatch"])
        self.assertEqual(result["snapshot"]["business"]["chests"], 1)

    def test_chest_quick_stops_selecting_as_soon_as_opening_appears(self):
        result = self.execute("chest-quick-fast", [{"chestFlag": (300, 400)}, {"whowillopenit": (200, 500)},
            {"chestOpening": (300, 500)}, {"dungFlag": (50, 150)}],
            [dict(kind=0, x=320, y=412), dict(kind=0, x=258, y=1161), dict(kind=0, x=515, y=934)],
            workflow="chest", quick=True)
        self.assertEqual(result["snapshot"]["state"], "Completed", result)
        self.assertEqual(result["backend_calls"], 3)
        self.assertFalse(result["mismatch"])

    def test_chest_quick_stops_on_combat_and_blocking_after_input(self):
        for blocked in (False, True):
            screens = [{"chestFlag": (300, 400)}, {"whowillopenit": (200, 500)},
                       {"chestOpening": (300, 500)}, {"retry": (400, 800)} if blocked else {"combatActive": (20, 20)}]
            result = self.execute(f"chest-quick-interrupt-{blocked}", screens,
                [dict(kind=0, x=320, y=412), dict(kind=0, x=258, y=1161), dict(kind=0, x=515, y=934)],
                workflow="chest", quick=True)
            self.assertEqual(result["snapshot"]["state"], "Interrupted", result)
            self.assertEqual(result["backend_calls"], 3)
            self.assertFalse(result["mismatch"])
            self.assertEqual(result["snapshot"]["business"]["chests"], 0)
            if blocked:
                self.assertEqual(result["snapshot"]["sessions"][-1]["reason"], "chest.disarm_outcome_unconfirmed")

    def test_chest_transition_cancels_disarm_without_false_count(self):
        for name, screen in [("combat", {"combatActive": (20, 20)}),
                             ("revive", {"RiseAgain": (300, 500)}),
                             ("ambush", {"ambush": (300, 500)})]:
            result = self.execute("chest-" + name,
                                  [{"chestOpening": (300, 500)}, screen], [dict(kind=0, x=515, y=934)],
                                  workflow="chest", quick=True)
            self.assertEqual(result["snapshot"]["state"], "Interrupted", result)
            self.assertEqual(result["snapshot"]["business"]["chests"], 0)
            self.assertEqual(result["backend_calls"], 1)
            self.assertFalse(result["mismatch"])

    def test_chest_all_fear_has_no_character_or_disarm_input(self):
        # 同一模板重复放在六个角色 ROI 内，所有角色都不可选择。
        # execute 的场景格式允许同名模板通过 @ 后缀多次放置。
        screen = {"whowillopenit": (200, 500)}
        for i in range(6):
            screen[f"chestfear@{i}"] = (258 + (i % 3) * 258 - 20, 1161 + (i // 3) * 184 - 12)
        result = self.execute("chest-fear", [screen], [], workflow="chest")
        self.assertEqual(result["snapshot"]["state"], "Interrupted", result)
        self.assertEqual(result["snapshot"]["business"]["chests"], 0)
        self.assertEqual(result["backend_calls"], 0)

    def test_chest_failed_input_and_stop_do_not_count(self):
        for name, options, transition in [("failed", {}, dict(kind=0, x=515, y=934, reject=True)),
                                          ("stop", {"stop_after_first": True}, dict(kind=0, x=515, y=934, stay=True))]:
            result = self.execute("chest-" + name, [{"chestOpening": (300, 500)}], [transition],
                                  workflow="chest", **options)
            self.assertEqual(result["snapshot"]["state"], "Failed" if name == "failed" else "UserStopped", result)
            self.assertEqual(result["snapshot"]["business"]["chests"], 0)
            self.assertEqual(result["backend_calls"], 1)

    def test_travel_opens_relocates_and_stops_in_city(self):
        empty_world = {"worldmapflag": (80, 100)}
        target_world = {**empty_world, "City_RoyalCityLuknalia": (132, 1352)}
        screens = [{"openworldmap": (600, 300)}, empty_world, target_world, {"Inn": (100, 400)}]
        result = self.execute("travel-return", screens,
                              [dict(kind=0, x=620, y=312), dict(kind=1, x=450, y=150, x2=500, y2=150, duration=400),
                               dict(kind=0, x=152, y=1364)], workflow="travel")
        self.assertEqual(result["snapshot"]["state"], "Completed", result)
        self.assertEqual(result["cursor"], 3)
        self.assertFalse(result["mismatch"])

    def test_travel_departure_and_existing_arrival(self):
        world = {"worldmapflag": (80, 100), "City_RoyalCityLuknalia": (132, 1352)}
        result = self.execute("travel-depart", [{"Inn": (100, 400), "intoWorldMap": (600, 300)},
                              world, {"openworldmap": (600, 300)}],
                              [dict(kind=0, x=620, y=312), dict(kind=0, x=152, y=1364)],
                              workflow="travel", returning=False)
        self.assertEqual(result["snapshot"]["state"], "Completed", result)
        self.assertFalse(result["mismatch"])
        for returning, screen in [(True, {"Inn": (100, 400)}), (False, {"dungFlag": (100, 1400)})]:
            result = self.execute("travel-arrived-" + str(returning), [screen], [],
                                  workflow="travel", returning=returning)
            self.assertEqual(result["snapshot"]["state"], "Completed", result)
            self.assertEqual(result["backend_calls"], 0)

    def test_travel_unknown_scene_never_uses_map_coordinates(self):
        result = self.execute("travel-unknown", [{"City_RoyalCityLuknalia": (132, 1352)}], [], workflow="travel")
        self.assertEqual(result["snapshot"]["state"], "Interrupted", result)
        self.assertEqual(result["backend_calls"], 0)

    def test_party_requires_assembly_confirmation_and_rest(self):
        title = {"PartyManagementTitle": (100, 200), "AssembleParty": (400, 900)}
        for named in (False, True):
            selected = {**title, "partyBlue": (100, 500)} if named else title
            city = {"Inn": (100, 400), "guild": (500, 400)}
            screens = [city, {"Edit": (100, 500)}, {"PartyManagement": (300, 500)}, selected, title,
                       {**title, "OK": (500, 800)}, title, city, {"Stay": (100, 600)},
                       {"Economy": (100, 500)}, {"OK": (500, 800)}, {"Stay": (100, 600)}, city]
            commands = [dict(kind=0, x=520, y=412), dict(kind=0, x=120, y=512),
                        dict(kind=0, x=320, y=512), dict(kind=0, x=120 if named else 137, y=512 if named else 290),
                        dict(kind=0, x=420, y=912), dict(kind=0, x=520, y=812), dict(kind=5, key=4),
                        dict(kind=0, x=120, y=412), dict(kind=0, x=120, y=612), dict(kind=0, x=120, y=512),
                        dict(kind=0, x=520, y=812), dict(kind=5, key=4)]
            result = self.execute("party-rest-" + str(named), screens, commands, workflow="party-rest",
                                  **({"party_image": "partyBlue"} if named else {}))
            self.assertEqual(result["snapshot"]["state"], "Completed", result)
            self.assertEqual(result["backend_calls"], 12)
            self.assertFalse(result["mismatch"])

    def test_party_confirm_failure_cannot_start_rest(self):
        title = {"PartyManagementTitle": (100, 200), "AssembleParty": (400, 900)}
        result = self.execute("party-reject", [title, title, {**title, "OK": (500, 800)}],
                              [dict(kind=0, x=137, y=290), dict(kind=0, x=420, y=912),
                               dict(kind=0, x=520, y=812, reject=True)], workflow="party-rest")
        self.assertEqual(result["snapshot"]["state"], "Failed", result)
        self.assertEqual(result["cursor"], 2)
        self.assertEqual(result["backend_calls"], 3)

    def test_state_routes_resume_same_step_after_normal_insertion(self):
        base = {"mapFlag": (100, 100)}
        screens = [{**base, "cursor_0": (480, 588)}, {"combatActive": (20, 20)},
                   {"dungFlag": (100, 1400)}, {**base, "cursor_0": (680, 688)}]
        result = self.execute("state-routing", screens,
                              [dict(kind=0, x=700, y=700), dict(kind=0, x=850, y=1100),
                               dict(kind=0, x=777, y=150)], workflow="state-route")
        self.assertEqual(result["snapshot"]["state"], "Completed", result)
        self.assertEqual(result["snapshot"]["generation"], 1)
        self.assertEqual(result["snapshot"]["business"]["task_step"], 2)
        self.assertEqual(result["snapshot"]["business"]["combats"], 1)
        self.assertEqual(result["backend_calls"], 3)
        self.assertFalse(result["mismatch"])

    def test_state_routes_count_two_encounters_at_the_same_node(self):
        base = {"mapFlag": (100, 100)}
        battle = {"combatActive": (20, 20)}
        dungeon = {"dungFlag": (100, 1400)}
        screens = [{**base, "cursor_0": (480, 588)}, battle, dungeon, base,
                   battle, dungeon, {**base, "cursor_0": (680, 688)}]
        actions = [dict(kind=0, x=700, y=700), dict(kind=0, x=850, y=1100), dict(kind=0, x=777, y=150)] * 2
        r = self.execute("state-two-encounters", screens, actions, workflow="state-route")
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["snapshot"]["business"]["combats"], 2)
        self.assertEqual(r["snapshot"]["business"]["task_step"], 2)
        self.assertEqual(r["snapshot"]["generation"], 1)
        self.assertEqual(r["backend_calls"], 6)
        self.assertFalse(r["mismatch"])

    def test_encounter_sequences_two_actors_and_counts_only_after_dungeon(self):
        profile = self.turn_profile()
        profile["RELOAD_STRATEGY_WHEN"] = "每次副本开始"
        detail = {"spellskill/skillDetail": (350, 950), "next": (500, 300)}
        screens = [self.turn_screen(), self.turn_screen(**detail), self.turn_screen("B"),
                   self.turn_screen("B", **detail), {"dungFlag": (50, 150)}]
        actions = [dict(kind=0, x=266, y=1054), dict(kind=0, x=440, y=392)] * 2
        r = self.execute("encounter-two-actors", screens, actions, workflow="encounter", profile=profile)
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 4)
        self.assertFalse(r["mismatch"])
        state = r["snapshot"]["business"]
        self.assertEqual(state["combats"], 1)
        self.assertFalse(state["pending_combat"])
        self.assertEqual(state["strategy"]["current"]["skill_settings"], [])

    def test_encounter_budget_and_input_failure_do_not_complete_battle(self):
        screens = [self.turn_screen(), self.turn_screen("B")]
        for name, command, state in [("budget", dict(kind=0, x=513, y=1200), "Interrupted"),
                                      ("reject", dict(kind=0, x=513, y=1200, reject=True), "Failed")]:
            r = self.execute("encounter-" + name, screens, [command], workflow="encounter",
                             profile=self.turn_profile(defend=True), max_turns=1)
            self.assertEqual(r["snapshot"]["state"], state, r)
            self.assertEqual(r["snapshot"]["business"]["combats"], 0)
            self.assertTrue(r["snapshot"]["business"]["pending_combat"])
            self.assertEqual(r["backend_calls"], 1)
            self.assertFalse(r["mismatch"])

    def test_encounter_shared_turn_budget_does_not_reset_strategy(self):
        profile = self.turn_profile(defend=True)
        rows = profile["STRATEGY"][0]["skill_settings"]
        rows.append(dict(rows[0]))
        r = self.execute("encounter-three-turns", [self.turn_screen(), self.turn_screen("B"),
            self.turn_screen(), {"dungFlag": (50, 150)}], [dict(kind=0, x=513, y=1200)] * 3,
            workflow="encounter", profile=profile, max_turns=16)
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 3)
        self.assertEqual(r["snapshot"]["business"]["strategy"]["current"]["skill_settings"], [])
        self.assertEqual(r["snapshot"]["business"]["combats"], 1)
        self.assertLess(r["node_count"], 1024)

    def test_auto_battle_end_is_not_auto_enabled(self):
        disabled = {"combatActive": (20, 20), "spellskill/CombatAutoDisable": (800, 1070)}
        r = self.execute("auto-ended", [disabled, {"dungFlag": (50, 150)}],
                         [dict(kind=0, x=850, y=1100)], workflow="auto")
        self.assertEqual(r["snapshot"]["state"], "Interrupted", r)
        self.assertEqual(r["backend_calls"], 1)
        self.assertFalse(r["mismatch"])

    def test_encounter_chest_transition_does_not_settle_dungeon_counters(self):
        r = self.execute("encounter-to-chest", [self.turn_screen(), {"chestFlag": (300, 400)}],
                         [dict(kind=0, x=513, y=1200)], workflow="encounter",
                         profile=self.turn_profile(defend=True), max_turns=1)
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["snapshot"]["business"]["combats"], 0)
        self.assertTrue(r["snapshot"]["business"]["pending_combat"])
        self.assertEqual(r["backend_calls"], 1)
        self.assertFalse(r["mismatch"])

    def test_encounter_ended_while_closing_detail_does_not_consume_unconfirmed_auto(self):
        profile = self.turn_profile()
        profile["RELOAD_STRATEGY_WHEN"] = "每次副本开始"
        detail = self.turn_screen(**{"spellskill/skillDetail": (350, 950), "close": (350, 1490)})
        r = self.execute("encounter-ended-before-auto", [self.turn_screen(), detail, {"dungFlag": (50, 150)}],
                         [dict(kind=0, x=266, y=1054), dict(kind=0, x=370, y=1502)],
                         workflow="encounter", profile=profile, max_turns=1)
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["snapshot"]["business"]["combats"], 1)
        self.assertEqual(len(r["snapshot"]["business"]["strategy"]["current"]["skill_settings"]), 2)
        self.assertEqual(r["backend_calls"], 2)
        self.assertFalse(r["mismatch"])

    @staticmethod
    def recovery_scenario():
        screens = [{}, {"boot_title_logo": (200, 400)}, {"boot_attention": (300, 500)},
                   {"startdownload": (300, 920)}, {"Inn": (100, 400)}, {"Stay": (100, 600)},
                   {"Economy": (100, 500)}, {"OK": (500, 800)}, {"Stay": (100, 600)}, {"Inn": (100, 400)}]
        actions = [dict(kind=0, x=450, y=1450), dict(kind=0, x=450, y=1450), dict(kind=0, x=320, y=932),
                   dict(kind=0, x=120, y=412), dict(kind=0, x=120, y=612), dict(kind=0, x=120, y=512),
                   dict(kind=0, x=520, y=812), dict(kind=5, key=4)]
        return screens, actions

    def test_recovery_boot_is_followed_by_original_task_not_root_completion(self):
        screens, actions = self.recovery_scenario()
        r = self.execute("recovery-boot-rest", screens, actions, workflow="recover")
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["snapshot"]["generation"], 2)
        self.assertEqual(r["lifecycle_calls"], ["EnsureVpn", "StopApplication", "StartApplication"])
        self.assertEqual(r["backend_calls"], 8)
        self.assertFalse(r["mismatch"])
        self.assertEqual(r["cursor"], 9)
        self.assertEqual(r["snapshot"]["business"]["crashes"], 1)
        self.assertFalse(r["snapshot"]["business"]["lifecycle_recovery_active"])

    def test_recovery_initial_connection_offline_and_closed_resume_original_task(self):
        screens, actions = self.recovery_scenario()
        for initial, expected, generation in [
            ("offline", ["Reconnect", "EnsureVpn", "StartApplication"], 3),
            ("closed", ["RestartInstance", "EnsureVpn", "StartApplication"], 4),
        ]:
            r = self.execute("cold-" + initial, screens, actions, workflow="recover", initial_connection=initial)
            self.assertEqual(r["snapshot"]["state"], "Completed", r)
            self.assertEqual(r["snapshot"]["generation"], generation)
            self.assertEqual(r["snapshot"]["sessions"][0]["reason"], "CONTROLLER_CONNECT_FAILED")
            self.assertEqual(r["lifecycle_calls"], expected)
            self.assertEqual(r["backend_calls"], 8)
            self.assertEqual(r["cursor"], 9)
            self.assertFalse(r["mismatch"])
            self.assertEqual(r["snapshot"]["business"]["crashes"], 1)
            saved = list((self.root / ("cold-" + initial) / "run").rglob("result.json"))
            self.assertEqual(len(saved), 1)
            events = json.loads(saved[0].read_text(encoding="utf-8"))["events"]["events"]
            calls = [e for e in events if e["type"] == "input.backend_called"]
            # 日志为有界窗口，完整输入次数以保存的逐代次摘要为权威。
            self.assertTrue(calls)
            self.assertTrue(all(e["session_generation"] == generation for e in calls))
            sessions = r["snapshot"]["sessions"]
            self.assertTrue(all(s["inputs"]["backend_called"] == 0 for s in sessions[:-1]))
            self.assertEqual(sessions[-1]["inputs"]["backend_called"], 8)
            self.assertEqual(sum(s["inputs"]["backend_called"] for s in sessions), r["backend_calls"])

    def test_recovery_initial_connection_requires_explicit_policy(self):
        screens, actions = self.recovery_scenario()
        r = self.execute("cold-no-policy", screens, actions, workflow="recover",
                         initial_connection="closed", omit_recovery_policy=True)
        self.assertEqual(r["snapshot"]["state"], "Failed", r)
        self.assertEqual(r["snapshot"]["reason"], "CONTROLLER_CONNECT_FAILED")
        self.assertEqual(r["snapshot"]["generation"], 1)
        self.assertEqual(r["backend_calls"], 0)
        self.assertEqual(r["lifecycle_calls"], [])

    def test_recovery_initial_connection_callback_exception_is_not_recoverable(self):
        screens, actions = self.recovery_scenario()
        r = self.execute("cold-exception", screens, actions, workflow="recover", initial_connection="exception")
        self.assertEqual(r["snapshot"]["state"], "Failed", r)
        self.assertEqual(r["snapshot"]["reason"], "FIXTURE_CONNECTION_EXCEPTION")
        self.assertEqual(r["snapshot"]["generation"], 1)
        self.assertEqual(r["backend_calls"], 0)
        self.assertEqual(r["lifecycle_calls"], [])

    def test_recovery_initial_connection_cannot_bypass_lifecycle_permission(self):
        screens, actions = self.recovery_scenario()
        r = self.execute("cold-no-port", screens, actions, workflow="recover",
                         initial_connection="closed", no_lifecycle_port=True)
        self.assertEqual(r["snapshot"]["state"], "Failed", r)
        self.assertEqual(r["snapshot"]["reason"], "LIFECYCLE_NOT_AUTHORIZED")
        self.assertEqual(r["backend_calls"], 0)
        self.assertEqual(r["lifecycle_calls"], [])

    def test_recovery_initial_connection_stop_during_restart_does_not_start_game(self):
        screens, actions = self.recovery_scenario()
        r = self.execute("cold-stop", screens, actions, workflow="recover",
                         initial_connection="closed", stop_during_lifecycle=True)
        self.assertEqual(r["snapshot"]["state"], "UserStopped", r)
        self.assertTrue(r["snapshot"]["quiescent"])
        self.assertEqual(r["backend_calls"], 0)
        self.assertEqual(r["lifecycle_calls"], ["RestartInstance"])

    def test_recovery_initial_connection_exhausts_finite_attempts(self):
        screens, actions = self.recovery_scenario()
        r = self.execute("cold-exhausted", screens, actions, workflow="recover",
                         initial_connection="closed", fail_starts=3)
        self.assertEqual(r["snapshot"]["state"], "Interrupted", r)
        self.assertEqual(r["snapshot"]["generation"], 4)
        self.assertEqual(r["backend_calls"], 0)
        self.assertEqual(r["lifecycle_calls"], ["RestartInstance", "EnsureVpn", "StartApplication"])

    def test_recovery_escalates_only_after_confirmed_failure(self):
        screens, actions = self.recovery_scenario()
        for failures, expected in [
            (1, ["EnsureVpn", "StopApplication", "StartApplication", "Reconnect", "StartApplication"]),
            (2, ["EnsureVpn", "StopApplication", "StartApplication", "Reconnect", "StartApplication",
                 "RestartInstance", "EnsureVpn", "StartApplication"]),
        ]:
            r = self.execute("recovery-escalate-" + str(failures), screens, actions, workflow="recover", fail_starts=failures)
            self.assertEqual(r["snapshot"]["state"], "Completed", r)
            self.assertEqual(r["snapshot"]["generation"], failures + 2)
            self.assertEqual(r["lifecycle_calls"], expected)
            self.assertEqual(r["backend_calls"], 8)
            self.assertFalse(r["mismatch"])
            self.assertEqual(r["snapshot"]["business"]["crashes"], 1)
            self.assertEqual(r["snapshot"]["business"]["lifecycle_recovery_sequence"], 1)

    def test_recovery_unknown_boot_and_failed_start_are_bounded(self):
        for name, options in [("unknown", {}), ("cannot-start", {"fail_starts": 3})]:
            r = self.execute("recovery-" + name, [{}, {}], [], workflow="recover", **options)
            self.assertEqual(r["snapshot"]["state"], "Interrupted", r)
            self.assertEqual(r["snapshot"]["generation"], 4)
            self.assertEqual(r["backend_calls"], 0)
            self.assertEqual(r["lifecycle_calls"].count("StartApplication"), 3)
            self.assertFalse(r["mismatch"])

    def test_recovery_rejects_missing_or_wrong_lifecycle_authority(self):
        screens, actions = self.recovery_scenario()
        for name, options, reason in [
            ("no-port", {"no_lifecycle_port": True}, "LIFECYCLE_NOT_AUTHORIZED"),
            ("other-app", {"other_lifecycle_app": True}, "LIFECYCLE_NOT_AUTHORIZED"),
            ("other-instance", {"other_lifecycle_instance": True}, "LIFECYCLE_OBSERVATION_INVALID"),
            ("stale", {"stale_lifecycle": True}, "LIFECYCLE_OBSERVATION_INVALID"),
        ]:
            r = self.execute("recovery-" + name, screens, actions, workflow="recover", **options)
            self.assertEqual(r["snapshot"]["state"], "Failed", r)
            self.assertEqual(r["snapshot"]["reason"], reason, r)
            self.assertEqual(r["lifecycle_calls"], [])
            self.assertEqual(r["backend_calls"], 0)

    def test_recovery_stop_during_lifecycle_never_starts_game(self):
        screens, actions = self.recovery_scenario()
        r = self.execute("recovery-stop", screens, actions, workflow="recover", stop_during_lifecycle=True)
        self.assertEqual(r["snapshot"]["state"], "UserStopped", r)
        self.assertTrue(r["snapshot"]["quiescent"])
        self.assertEqual(r["lifecycle_calls"], ["EnsureVpn"])
        self.assertEqual(r["backend_calls"], 0)

    def test_recovery_download_missing_permission_does_not_restart_again(self):
        screens, actions = self.recovery_scenario()
        r = self.execute("recovery-no-download", screens[:4], actions[:2], workflow="recover", allow_download=False)
        self.assertEqual(r["snapshot"]["state"], "Interrupted", r)
        self.assertEqual(r["snapshot"]["generation"], 2)
        self.assertEqual(r["snapshot"]["sessions"][-1]["reason"], "boot.download_permission_missing")
        self.assertEqual(r["backend_calls"], 2)
        self.assertEqual(r["lifecycle_calls"].count("StartApplication"), 1)

    def test_recovery_missing_boot_asset_rejected_before_connection(self):
        r = self.execute("recovery-missing-asset", [{}], [], workflow="recover", omit_image="boot_attention.png")
        self.assertEqual(r["publish_error"], "COMPILE_IMAGE_NOT_IN_MANIFEST:image/boot_attention.png", r)
        self.assertEqual(r["connections"], 0)
        self.assertEqual(r["backend_calls"], 0)

    def test_recovery_late_native_return_retains_ownership(self):
        screens, actions = self.recovery_scenario()
        r = self.execute("recovery-late-release", screens, actions, workflow="recover", late_lifecycle_release=True)
        self.assertEqual(r["lifecycle_stop"]["reason"], "STOP_TIMEOUT", r)
        self.assertFalse(r["lifecycle_stop"]["quiescent"])
        self.assertEqual(r["lifecycle_stop"]["new_run_error"], "RUN_BUSY")
        self.assertEqual(r["snapshot"]["state"], "Failed")
        self.assertEqual(r["snapshot"]["reason"], "STOP_TIMEOUT")
        self.assertTrue(r["snapshot"]["quiescent"])
        self.assertEqual(r["backend_calls"], 0)
        self.assertEqual(r["lifecycle_calls"], ["EnsureVpn"])

    def test_recovery_crash_threshold_forces_instance_once(self):
        screens, actions = self.recovery_scenario()
        r = self.execute("recovery-crash-threshold", screens, actions, workflow="recover", max_crashes=0)
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["lifecycle_calls"], ["RestartInstance", "EnsureVpn", "StartApplication"])
        self.assertEqual(r["snapshot"]["business"]["crashes"], 0)
        self.assertEqual(r["snapshot"]["business"]["lifecycle_recovery_sequence"], 1)
        self.assertFalse(r["mismatch"])

    def test_recovery_boot_input_failure_is_not_retried_as_lifecycle(self):
        screens, actions = self.recovery_scenario()
        r = self.execute("recovery-input-failed", screens[:2], [{**actions[0], "reject": True}], workflow="recover")
        self.assertEqual(r["snapshot"]["state"], "Failed", r)
        self.assertEqual(r["snapshot"]["generation"], 2)
        self.assertEqual(r["backend_calls"], 1)
        self.assertEqual(r["lifecycle_calls"].count("StartApplication"), 1)
        self.assertTrue(r["snapshot"]["business"]["lifecycle_recovery_active"])

    def test_recovery_new_failure_after_ready_starts_at_application_level(self):
        # 启动已完成，但原住宿业务仍不在城内；这是新恢复请求，不是旧请求第二级。
        r = self.execute("recovery-new-request", [{}, {"dungFlag": (50, 150)}], [], workflow="recover")
        self.assertEqual(r["snapshot"]["state"], "Interrupted", r)
        self.assertEqual(r["snapshot"]["generation"], 4)
        self.assertEqual(r["lifecycle_calls"], ["EnsureVpn", "StopApplication", "StartApplication",
                                               "StopApplication", "StartApplication", "StopApplication", "StartApplication"])
        self.assertEqual(r["snapshot"]["business"]["crashes"], 3)
        self.assertEqual(r["snapshot"]["business"]["lifecycle_recovery_sequence"], 3)
        self.assertFalse(r["snapshot"]["business"]["lifecycle_recovery_active"])
        self.assertEqual(r["backend_calls"], 0)

    def test_encounter_auto_ends_into_dungeon_without_another_toggle(self):
        # 外层确认返回地下城可结束遭遇，不把未确认的 Auto 误报为开启。
        r = self.execute("encounter-auto-ended", [self.turn_screen(**{"spellskill/CombatAutoDisable": (800, 1070)}),
                          {"dungFlag": (50, 150)}], [dict(kind=0, x=850, y=1100)],
                         workflow="encounter", profile={"DEFAULT_OVERALL_STRATEGY": "全自动战斗", "STRATEGY": [], "TASK_SPECIFIC_CONFIG": False}, max_turns=1)
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["snapshot"]["business"]["combats"], 1)
        self.assertEqual(r["backend_calls"], 1)
        self.assertFalse(r["mismatch"])

    def test_encounter_auto_waits_for_clock_progress_without_consuming_turns(self):
        r = self.execute("encounter-auto-clock", [
            self.turn_screen(**{"spellskill/CombatAutoDisable": (800, 1070)}),
            self.turn_screen(**{"spellskill/CombatAutoEnable": (800, 1070)}),
            {"dungFlag": (50, 150)}], [dict(kind=0, x=850, y=1100)], workflow="encounter",
            profile={"DEFAULT_OVERALL_STRATEGY": "全自动战斗", "STRATEGY": [], "TASK_SPECIFIC_CONFIG": False},
            max_turns=1, max_auto_polls=32, time_event=dict(after_input=1, delay_ms=4000, frame=2))
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["snapshot"]["business"]["combats"], 1)
        self.assertEqual(r["backend_calls"], 1)
        self.assertEqual(r["time_event_count"], 1)
        self.assertFalse(r["mismatch"])

    def test_encounter_auto_wait_has_independent_timeout(self):
        for already_on in (False, True):
            enabled = self.turn_screen(**{"spellskill/CombatAutoEnable": (800, 1070)})
            screens = [enabled] if already_on else [self.turn_screen(**{"spellskill/CombatAutoDisable": (800, 1070)}), enabled]
            r = self.execute("encounter-auto-timeout-" + str(already_on), screens,
                [] if already_on else [dict(kind=0, x=850, y=1100)], workflow="encounter",
                profile={"DEFAULT_OVERALL_STRATEGY": "全自动战斗", "STRATEGY": [], "TASK_SPECIFIC_CONFIG": False},
                max_turns=1, max_auto_polls=3)
            self.assertEqual(r["snapshot"]["state"], "Interrupted", r)
            self.assertEqual(r["snapshot"]["sessions"][-1]["reason"], "combat.auto_progress_timeout", r)
            self.assertEqual(r["snapshot"]["business"]["combats"], 0)
            self.assertEqual(r["backend_calls"], 0 if already_on else 1)
            self.assertFalse(r["mismatch"])

    def test_encounter_stop_inside_auto_wait_sends_no_late_input(self):
        r = self.execute("encounter-auto-stop-wait", [self.turn_screen(**{"spellskill/CombatAutoEnable": (800, 1070)})], [],
            workflow="encounter", profile={"DEFAULT_OVERALL_STRATEGY": "全自动战斗", "STRATEGY": [], "TASK_SPECIFIC_CONFIG": False},
            max_turns=1, stop_at_node="Turn0Poll")
        self.assertTrue(r["stop_node_observed"], r)
        self.assertEqual(r["snapshot"]["state"], "UserStopped", r)
        self.assertEqual(r["backend_calls"], 0)
        self.assertEqual(r["snapshot"]["business"]["combats"], 0)

    def test_encounter_auto_off_resumes_from_a_new_observation(self):
        disabled = self.turn_screen(**{"spellskill/CombatAutoDisable": (800, 1070)})
        r = self.execute("encounter-auto-off", [disabled,
            self.turn_screen(**{"spellskill/CombatAutoEnable": (800, 1070)}), disabled, {"dungFlag": (50, 150)}],
            [dict(kind=0, x=850, y=1100), dict(kind=0, x=850, y=1100)], workflow="encounter",
            profile={"DEFAULT_OVERALL_STRATEGY": "全自动战斗", "STRATEGY": [], "TASK_SPECIFIC_CONFIG": False},
            max_turns=2, max_auto_polls=32, time_event=dict(after_input=1, delay_ms=4000, frame=2))
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 2)
        self.assertEqual(r["time_event_count"], 1)
        self.assertEqual(r["snapshot"]["business"]["combats"], 1)
        self.assertFalse(r["mismatch"])

    def test_encounter_auto_input_failure_never_waits_or_counts(self):
        r = self.execute("encounter-auto-rejected", [self.turn_screen(**{"spellskill/CombatAutoDisable": (800, 1070)})],
            [dict(kind=0, x=850, y=1100, reject=True)], workflow="encounter",
            profile={"DEFAULT_OVERALL_STRATEGY": "全自动战斗", "STRATEGY": [], "TASK_SPECIFIC_CONFIG": False})
        self.assertEqual(r["snapshot"]["state"], "Failed", r)
        self.assertEqual(r["backend_calls"], 1)
        self.assertEqual(r["snapshot"]["business"]["combats"], 0)

    def test_healing_initial_request_requires_recover_and_return(self):
        for story in (False, True):
            panel = {"trait": (200, 300), **({"story": (700, 820)} if story else {})}
            r = self.execute("heal-initial-" + str(story), [{"dungFlag": (50, 150)}, panel,
                {"recover": (250, 850)}, {"trait": (200, 300)}, {"dungFlag": (50, 150)}],
                [dict(kind=0, x=36, y=1425), dict(kind=0, x=725 if story else 830, y=850),
                 dict(kind=0, x=600, y=1200), dict(kind=5, key=4)], workflow="heal", profile={"RECOVER_WHEN_BEGINNING": True})
            self.assertEqual(r["snapshot"]["state"], "Completed", r)
            self.assertEqual(r["backend_calls"], 4)
            self.assertFalse(r["mismatch"])
            state = r["snapshot"]["business"]
            self.assertFalse(state["healing_required"])
            self.assertFalse(state["healing_active"])
            self.assertEqual(state["healing_sequence"], 1)
            self.assertEqual(state["last_confirmation"]["event"], "healing_completed")

    def test_healing_disabled_does_not_open_character(self):
        r = self.execute("heal-not-needed", [{"dungFlag": (50, 150)}], [], workflow="heal", profile={"RECOVER_WHEN_BEGINNING": False})
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 0)
        self.assertEqual(r["snapshot"]["business"]["healing_sequence"], 0)

    def test_healing_rotates_front_characters_and_seeks_recover(self):
        dungeon = {"dungFlag": (50, 150)}
        trait = {"trait": (200, 300)}
        r = self.execute("heal-rotate-seek", [dungeon, dungeon, dungeon, trait, trait,
            {"recover": (250, 850)}, trait, dungeon],
            [dict(kind=0, x=x, y=1425) for x in (36, 322, 608)] +
            [dict(kind=0, x=830, y=850), dict(kind=0, x=833, y=843),
             dict(kind=0, x=600, y=1200), dict(kind=5, key=4)],
            workflow="heal", profile={"RECOVER_WHEN_BEGINNING": True})
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 7)
        self.assertFalse(r["mismatch"])
        self.assertFalse(r["snapshot"]["business"]["healing_required"])

    def test_healing_unknown_panel_is_not_success(self):
        r = self.execute("heal-unknown-panel", [{"dungFlag": (50, 150)}, {}], [dict(kind=0, x=36, y=1425)],
            workflow="heal", profile={"RECOVER_WHEN_BEGINNING": True})
        self.assertEqual(r["snapshot"]["state"], "Failed", r)
        self.assertEqual(r["backend_calls"], 1)
        self.assertTrue(r["snapshot"]["business"]["healing_required"])
        self.assertEqual(r["snapshot"]["business"]["last_confirmation"]["event"], "healing_requested")

    def test_healing_encounter_interrupts_before_old_panel_input(self):
        for encounter in ("combatActive", "chestFlag", "RiseAgain"):
            r = self.execute("heal-interrupted-" + encounter, [{"dungFlag": (50, 150)},
                {"trait": (200, 300), encounter: (10, 5) if encounter == "combatActive" else (350, 450)}],
                [dict(kind=0, x=36, y=1425)], workflow="heal", profile={"RECOVER_WHEN_BEGINNING": True})
            self.assertEqual(r["snapshot"]["state"], "Interrupted", r)
            self.assertEqual(r["snapshot"]["sessions"][-1]["reason"], "supply.recover_interrupted")
            self.assertEqual(r["backend_calls"], 1)
            self.assertTrue(r["snapshot"]["business"]["healing_required"])
            self.assertEqual(r["snapshot"]["business"]["last_confirmation"]["event"], "healing_requested")
            self.assertFalse(r["mismatch"])

    def test_healing_input_failure_and_stop_keep_pending_request(self):
        for stop in (False, True):
            r = self.execute("heal-stop-" + str(stop), [{"dungFlag": (50, 150)}],
                [dict(kind=0, x=36, y=1425, **({"stay": True} if stop else {"reject": True}))], workflow="heal",
                profile={"RECOVER_WHEN_BEGINNING": True}, stop_after_first=stop)
            self.assertEqual(r["snapshot"]["state"], "UserStopped" if stop else "Failed", r)
            self.assertEqual(r["backend_calls"], 1)
            self.assertTrue(r["snapshot"]["business"]["healing_required"])
            self.assertEqual(r["snapshot"]["business"]["last_confirmation"]["event"], "healing_requested")

    def test_healing_panel_not_closed_never_claims_completion(self):
        panel = {"recover": (250, 850)}
        r = self.execute("heal-back-bounded", [panel] * 7,
            [dict(kind=0, x=600, y=1200)] + [dict(kind=5, key=4)] * 5,
            workflow="heal", profile={"RECOVER_WHEN_BEGINNING": True})
        self.assertEqual(r["snapshot"]["state"], "Interrupted", r)
        self.assertEqual(r["snapshot"]["sessions"][-1]["reason"], "supply.recover_panel_not_closed")
        self.assertEqual(r["backend_calls"], 6)
        self.assertTrue(r["snapshot"]["business"]["healing_required"])
        self.assertFalse(r["mismatch"])

    def test_dungeon_route_reenters_same_point_after_real_combat(self):
        profile = self.turn_profile(defend=True)
        profile.update(SKIP_COMBAT_RECOVER=True, SKIP_CHEST_RECOVER=True)
        base = {"mapFlag": (100, 100)}
        r = self.execute("route-combat", [{**base, "cursor_0": (480, 588)}, self.turn_screen(),
            {"dungFlag": (50, 150)}, {**base, "cursor_0": (680, 688)}],
            [dict(kind=0, x=700, y=700), dict(kind=0, x=513, y=1200), dict(kind=0, x=777, y=150)],
            workflow="dungeon-route", profile=profile,
            route_targets=[["position", [None], [500, 600]], ["position", [None], [700, 700]]])
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["snapshot"]["business"]["task_step"], 2)
        self.assertEqual(r["snapshot"]["business"]["combats"], 1)
        self.assertEqual(r["snapshot"]["generation"], 1)
        self.assertEqual(r["backend_calls"], 3)
        self.assertFalse(r["mismatch"])

    def test_dungeon_route_chest_combat_revokes_disarm_and_settles_both(self):
        profile = self.turn_profile(defend=True)
        profile.update(SKIP_COMBAT_RECOVER=True, SKIP_CHEST_RECOVER=True, WHO_WILL_OPEN_IT=1)
        base = {"mapFlag": (100, 100)}
        r = self.execute("route-chest-combat", [base, {"chestFlag": (330, 450)}, self.turn_screen(),
            {"dungFlag": (50, 150)}, {**base, "cursor_0": (480, 588)}],
            [dict(kind=0, x=500, y=600), dict(kind=0, x=350, y=462), dict(kind=0, x=513, y=1200), dict(kind=0, x=777, y=150)],
            workflow="dungeon-route", profile=profile, route_targets=[["position", [None], [500, 600]]])
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["snapshot"]["business"]["task_step"], 1)
        self.assertEqual(r["snapshot"]["business"]["combats"], 1)
        self.assertEqual(r["snapshot"]["business"]["chests"], 1)
        self.assertEqual(r["backend_calls"], 4)
        self.assertFalse(r["mismatch"])

    def test_dungeon_route_input_failure_does_not_advance_target(self):
        r = self.execute("route-input-rejected", [{"mapFlag": (100, 100)}], [dict(kind=0, x=500, y=600, reject=True)],
            workflow="dungeon-route", profile=self.turn_profile(defend=True), route_targets=[["position", [None], [500, 600]]])
        self.assertEqual(r["snapshot"]["state"], "Failed", r)
        self.assertEqual(r["snapshot"]["business"]["task_step"], 0)
        self.assertEqual(r["backend_calls"], 1)

    def test_dungeon_route_outside_and_revive_never_use_map_coordinates(self):
        # 新版接通复活消费者：仍禁止地图点，只允许当前 RiseAgain 上的确认点。
        for name in ("Inn", "RiseAgain"):
            screens = [{name: (350, 450)}]
            commands = []
            if name == "RiseAgain":
                screens.append({"Inn": (100, 400)})
                commands.append(dict(kind=0, x=370, y=462))
            r = self.execute("route-" + name, screens, commands, workflow="dungeon-route",
                profile=self.turn_profile(defend=True), route_targets=[["position", [None], [500, 600]]])
            self.assertEqual(r["snapshot"]["state"], "Completed", r)
            self.assertEqual(r["snapshot"]["business"]["task_step"], 0)
            self.assertEqual(r["backend_calls"], len(commands))
            self.assertFalse(r["mismatch"])
            self.assertEqual(r["snapshot"]["business"]["revivals"], len(commands))

    def test_revival_confirms_only_after_leaving_prompt(self):
        prompt, dungeon = {"RiseAgain": (350, 450)}, {"dungFlag": (50, 150)}
        for second in (False, True):
            commands = [dict(kind=0, x=370, y=462)]
            if second:
                commands.append(dict(kind=0, x=450, y=750))
            r = self.execute(f"revival-confirmed-{second}", [prompt] * len(commands) + [dungeon], commands,
                             workflow="revival", profile=self.turn_profile())
            self.assertEqual(r["snapshot"]["state"], "Completed", r)
            self.assertEqual(r["backend_calls"], len(commands))
            self.assertFalse(r["mismatch"])
            state = r["snapshot"]["business"]
            self.assertEqual((state["revivals"], state["revival_sequence"]), (1, 1))
            self.assertTrue(state["healing_required"])
            self.assertFalse(state["revival_pending"])
            self.assertEqual((state["combats"], state["chests"]), (0, 0))

    def test_revival_uncertainty_never_restarts_or_replays(self):
        prompt = {"RiseAgain": (350, 450)}
        cases = (("unchanged", [prompt] * 3, [dict(kind=0, x=370, y=462), dict(kind=0, x=450, y=750)]),
                 ("blocked", [prompt, {"retry": (400, 800)}], [dict(kind=0, x=370, y=462)]))
        for name, screens, commands in cases:
            r = self.execute("revival-" + name, screens, commands, workflow="revival",
                profile=self.turn_profile(), attach_recovery=True, force_instance=True, max_crashes=0)
            self.assert_uncertain_effect(r, "revival.outcome_unconfirmed", len(commands))
            self.assertEqual(r["snapshot"]["business"]["revivals"], 0)
            self.assertTrue(r["snapshot"]["business"]["revival_pending"])

    def test_revival_stop_and_reject_leave_unconfirmed_state(self):
        for stop in (False, True):
            r = self.execute(f"revival-stop-{stop}", [{"RiseAgain": (350, 450)}] * 2,
                [dict(kind=0, x=370, y=462, reject=not stop)], workflow="revival", stop_after_first=stop)
            self.assertEqual(r["snapshot"]["state"], "UserStopped" if stop else "Failed", r)
            self.assertEqual(r["backend_calls"], 1)
            self.assertFalse(r["mismatch"])
            self.assertEqual(r["snapshot"]["business"]["revivals"], 0)

    def test_revival_route_heals_after_defeat_without_counting_win(self):
        prompt, dungeon = {"RiseAgain": (350, 450)}, {"dungFlag": (50, 150)}
        trait, recover = {"trait": (200, 300)}, {"recover": (250, 850)}
        r = self.execute("route-revival-heal", [self.turn_screen(), prompt, dungeon, trait, recover, trait, dungeon,
            {"mapFlag": (100, 100), "cursor_0": (480, 588)}],
            [dict(kind=0, x=513, y=1200), dict(kind=0, x=370, y=462), dict(kind=0, x=36, y=1425),
             dict(kind=0, x=830, y=850), dict(kind=0, x=600, y=1200), dict(kind=5, key=4), dict(kind=0, x=777, y=150)],
            workflow="dungeon-route", profile=self.turn_profile(defend=True), route_targets=[["position", [None], [500, 600]]])
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 7)
        self.assertFalse(r["mismatch"])
        state = r["snapshot"]["business"]
        self.assertEqual((state["task_step"], state["combat_sequence"], state["revivals"], state["combats"]), (1, 1, 1, 0))
        self.assertFalse(state["healing_required"])

    def test_dungeon_route_heals_after_combat_before_navigation(self):
        profile = self.turn_profile(defend=True)
        profile.update(SKIP_COMBAT_RECOVER=False, RECOVER_WHEN_BEGINNING=False)
        dungeon, trait = {"dungFlag": (50, 150)}, {"trait": (200, 300)}
        r = self.execute("route-combat-heal", [self.turn_screen(), dungeon, trait, {"recover": (250, 850)}, trait,
            dungeon, {"mapFlag": (100, 100), "cursor_0": (480, 588)}],
            [dict(kind=0, x=513, y=1200), dict(kind=0, x=36, y=1425), dict(kind=0, x=830, y=850),
             dict(kind=0, x=600, y=1200), dict(kind=5, key=4), dict(kind=0, x=777, y=150)],
            workflow="dungeon-route", profile=profile, route_targets=[["position", [None], [500, 600]]])
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 6)
        self.assertFalse(r["mismatch"])
        state = r["snapshot"]["business"]
        self.assertEqual(state["task_step"], 1)
        self.assertEqual(state["combats"], 1)
        self.assertEqual(state["healing_sequence"], 1)
        self.assertFalse(state["healing_required"])
        self.assertEqual(r["snapshot"]["sessions"][0]["definition"]["time_limit_ms"], 1000000)

    def test_route_and_iteration_budgets_include_the_complete_chest_child(self):
        for quick in (False, True):
            for workflow in ("dungeon-route", "iteration"):
                r = self.execute(f"parent-budget-{workflow}-{quick}",
                    [{"mapFlag": (100, 100), "cursor_0": (480, 588)}], [], workflow=workflow,
                    profile={**self.turn_profile(defend=True), "QUICK_DISARM_CHEST": quick},
                    route_targets=[["position", [None], [500, 600]]])
                self.assertEqual(r["snapshot"]["state"], "Completed", r)
                self.assertEqual(r["backend_calls"], 0)
                budget = (900 if quick else 600) + 400 + (360 if workflow == "iteration" else 0)
                self.assertEqual(r["snapshot"]["sessions"][0]["definition"]["time_limit_ms"], budget * 1000)
                self.assertLessEqual(budget + 120, 1800)

    def test_workflow_session_budget_must_be_finite_and_positive(self):
        for case in ("zero-session-budget", "large-session-budget"):
            r = self.execute(case, [{}], [], invalid=case)
            self.assertEqual(r["error"], "COMPILE_SESSION_BUDGET_INVALID")
            self.assertEqual(r["backend_calls"], 0)

    def test_dungeon_route_map_does_not_eagerly_open_healing_panel(self):
        profile = self.turn_profile(defend=True)
        profile["RECOVER_WHEN_BEGINNING"] = True
        r = self.execute("route-map-initial-heal", [{"mapFlag": (100, 100), "cursor_0": (480, 588)}], [],
            workflow="dungeon-route", profile=profile, route_targets=[["position", [None], [500, 600]]])
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 0)
        self.assertTrue(r["snapshot"]["business"]["healing_required"])

    def test_dungeon_route_exit_prompt_stops_old_map_clicks(self):
        base = {"mapFlag": (100, 100), "harken": (480, 588)}
        r = self.execute("route-exit-prompt", [base, base, {"returnText": (400, 800)}],
            [dict(kind=0, x=500, y=600), dict(kind=0, x=136, y=1431)], workflow="dungeon-route",
            profile=self.turn_profile(defend=True), route_targets=[["harken", [None]]])
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 2)
        self.assertFalse(r["mismatch"])
        # 这是退出 StateDungeon，不是假装在地图上完成了最后一个任务点。
        self.assertEqual(r["snapshot"]["business"]["task_step"], 0)


    def test_iteration_position_exit_does_not_require_harken_name(self):
        base = {"mapFlag": (100, 100)}
        for outside in ("openworldmap", "worldmapflag", "returnText"):
            with self.subTest(outside=outside):
                r = self.execute("position-exit-" + outside, [base, base, {outside: (400, 800)}],
                    [dict(kind=0, x=500, y=600), dict(kind=0, x=136, y=1431)], workflow="dungeon-route",
                    profile=self.turn_profile(defend=True), route_targets=[["position", [None], [500, 600]]])
                self.assertEqual(r["snapshot"]["state"], "Completed", r)
                self.assertEqual(r["backend_calls"], 2)
                self.assertEqual(r["snapshot"]["business"]["task_step"], 0)

    def test_iteration_auto_retreat_accepts_return_prompt(self):
        start = {"dungFlag": (750, 300)}
        r = self.execute("auto-return-prompt", [start, {"returnText": (400, 800)}],
                         [dict(kind=0, x=770, y=312)], workflow="auto-route", auto_target="dungFlag")
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 1)

    def test_iteration_entry_route_and_exit_are_one_native_chain(self):
        base = {"mapFlag": (100, 100), "harken": (480, 588)}
        frames = [{"Inn": (400, 700), "Dist": (300, 500)}, {"GotoDung": (400, 700)}, base, base, {"returntoTown": (400, 800)}]
        commands = [dict(kind=0, x=320, y=512), dict(kind=0, x=420, y=712),
                    dict(kind=0, x=500, y=600), dict(kind=0, x=136, y=1431)]
        r = self.execute("iteration-entry", frames, commands, workflow="iteration", profile=self.turn_profile(defend=True),
                         route_targets=[["chest", [None]], ["harken", [None]]])
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 4)
        self.assertEqual(r["snapshot"]["completed_business_units"], 1)
        self.assertEqual(r["snapshot"]["business"]["task_step"], 1)
        self.assertEqual(r["snapshot"]["business"]["dungeons"], 0)

    def test_iteration_two_units_keep_encounter_and_reset_new_dungeon(self):
        profile = self.turn_profile(defend=True)
        profile.update(SKIP_COMBAT_RECOVER=True, SKIP_CHEST_RECOVER=True, ACTIVE_REST=False,
                       RECOVER_WHEN_BEGINNING=False, RELOAD_STRATEGY_WHEN="每次副本开始")
        base = {"mapFlag": (100, 100), "harken": (480, 588)}
        frames = [self.turn_screen(), {"dungFlag": (50, 150)}, base, base,
                  {"returntoTown": (400, 800), "Dist": (300, 500)}, {"GotoDung": (400, 700)},
                  self.turn_screen(), {"dungFlag": (50, 150)}, base, base, {"returntoTown": (400, 800)}]
        commands = [dict(kind=0, x=513, y=1200), dict(kind=0, x=777, y=150),
                    dict(kind=0, x=500, y=600), dict(kind=0, x=136, y=1431),
                    dict(kind=0, x=320, y=512), dict(kind=0, x=420, y=712),
                    dict(kind=0, x=513, y=1200), dict(kind=0, x=777, y=150),
                    dict(kind=0, x=500, y=600), dict(kind=0, x=136, y=1431)]
        r = self.execute("iteration-two", frames, commands, workflow="iteration", profile=profile,
                         normal_units=2, route_targets=[["chest", [None]], ["harken", [None]]])
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 10)
        self.assertEqual(r["snapshot"]["completed_business_units"], 2)
        self.assertEqual(r["snapshot"]["generation"], 2)
        state = r["snapshot"]["business"]
        self.assertEqual(state["combats"], 2)
        self.assertEqual(state["dungeons"], 1)
        self.assertEqual(state["task_step"], 1)
        self.assertEqual(state["supply_cycle"], 2)

    def test_iteration_failure_or_stop_prevents_next_unit(self):
        for stop in (False, True):
            base = {"mapFlag": (100, 100), "harken": (480, 588)}
            r = self.execute("iteration-stop" if stop else "iteration-fail", [base, base],
                [dict(kind=0, x=500, y=600, reject=not stop)], workflow="iteration", stop_after_first=stop,
                profile=self.turn_profile(defend=True), normal_units=2, route_targets=[["harken", [None]]])
            self.assertEqual(r["snapshot"]["state"], "UserStopped" if stop else "Failed", r)
            self.assertEqual(r["backend_calls"], 1)
            self.assertEqual(r["snapshot"]["completed_business_units"], 0)
            self.assertEqual(r["snapshot"]["generation"], 1)


    def test_interruption_map_selection_never_sends_stale_automove(self):
        base = {"mapFlag": (100, 100)}
        r = self.execute("interrupt-map", [base, {**base, "retry": (400, 800)},
            {**base, "cursor_0": (480, 588)}], [dict(kind=0, x=500, y=600), dict(kind=0, x=420, y=812)],
            workflow="dungeon-route", profile=self.turn_profile(defend=True),
            route_targets=[["position", [None], [500, 600]]])
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 2)
        self.assertFalse(r["mismatch"])
        self.assertEqual(r["snapshot"]["business"]["task_step"], 1)
        self.assertEqual(r["snapshot"]["generation"], 1)
        self.assertEqual(r["lifecycle_calls"], [])

    def test_interruption_skill_overlay_is_not_consumed_as_success(self):
        # 即使旧完成锚点同时可见，阻塞覆盖层也不能让一次开面板被当成技能成功。
        r = self.execute("interrupt-skill", [self.turn_screen(), {"dungFlag": (50, 150), "retry": (400, 800)}],
            [dict(kind=0, x=266, y=1054)], workflow="turn", profile=self.turn_profile())
        self.assertEqual(r["snapshot"]["state"], "Interrupted", r)
        self.assertEqual(r["snapshot"]["sessions"][-1]["reason"], "combat.common_screen_requires_dispatch")
        self.assertEqual(r["backend_calls"], 1)
        self.assertTrue(r["snapshot"]["business"]["has_prepared_skill"])
        self.assertFalse(r["mismatch"])

    def test_interruption_stop_and_backend_reject_are_not_normal_returns(self):
        base = {"mapFlag": (100, 100)}
        for stop in (False, True):
            r = self.execute("interrupt-stop" if stop else "interrupt-reject", [base, {**base, "retry": (400, 800)}],
                [dict(kind=0, x=500, y=600, reject=not stop)], workflow="dungeon-route", stop_after_first=stop,
                profile=self.turn_profile(defend=True), route_targets=[["position", [None], [500, 600]]])
            self.assertEqual(r["snapshot"]["state"], "UserStopped" if stop else "Failed", r)
            self.assertEqual(r["backend_calls"], 1)
            self.assertEqual(r["snapshot"]["business"]["task_step"], 0)

    def test_interruption_chest_reenters_choice_not_old_disarm(self):
        choose = {"whowillopenit": (330, 450)}
        r = self.execute("interrupt-chest", [{"chestFlag": (330, 450)}, {**choose, "retry": (400, 800)}, choose,
            {"chestOpening": (330, 450)}, {"dungFlag": (50, 150)}, {"mapFlag": (100, 100), "cursor_0": (480, 588)}],
            [dict(kind=0, x=350, y=462), dict(kind=0, x=420, y=812), dict(kind=0, x=258, y=1161),
             dict(kind=0, x=515, y=934), dict(kind=0, x=777, y=150)], workflow="dungeon-route",
            profile={**self.turn_profile(defend=True), "WHO_WILL_OPEN_IT": 1, "SKIP_CHEST_RECOVER": True},
            route_targets=[["position", [None], [500, 600]]])
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 5)
        self.assertEqual(r["snapshot"]["business"]["chests"], 1)
        self.assertFalse(r["mismatch"])

    def test_interruption_heal_reenters_existing_character_panel(self):
        trait = {"trait": (200, 300)}
        r = self.execute("interrupt-heal", [{"dungFlag": (50, 150)}, {**trait, "retry": (400, 800)}, trait,
            {"recover": (250, 850)}, trait, {"dungFlag": (50, 150)},
            {"mapFlag": (100, 100), "cursor_0": (480, 588)}],
            [dict(kind=0, x=36, y=1425), dict(kind=0, x=420, y=812), dict(kind=0, x=830, y=850),
             dict(kind=0, x=600, y=1200), dict(kind=5, key=4), dict(kind=0, x=777, y=150)], workflow="dungeon-route",
            profile={**self.turn_profile(defend=True), "RECOVER_WHEN_BEGINNING": True},
            route_targets=[["position", [None], [500, 600]]])
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 6)
        self.assertEqual(r["snapshot"]["business"]["healing_sequence"], 1)
        self.assertFalse(r["snapshot"]["business"]["healing_required"])
        self.assertFalse(r["mismatch"])

    @staticmethod
    def wall_profile(enabled=True):
        return {**WorkflowTests.turn_profile(defend=True), "BYPASS_THE_WALL": enabled,
                "RECOVER_WHEN_BEGINNING": False, "SKIP_CHEST_RECOVER": True, "SKIP_COMBAT_RECOVER": True}

    def test_unknown_window_frozen_route_never_clicks(self):
        r = self.execute("unknown-window-frozen", [{}], [], workflow="dungeon-route",
            profile=self.wall_profile(False), route_targets=[["position", [None], [500, 600]]])
        self.assertEqual(r["snapshot"]["state"], "Interrupted", r)
        self.assertEqual(r["snapshot"]["sessions"][-1]["reason"], "dungeon.unknown_static_window")
        self.assertEqual(r["backend_calls"], 0)
        self.assertEqual(r["lifecycle_calls"], [])
        self.assertTrue(r["snapshot"]["quiescent"])
        self.assertEqual(r["snapshot"]["business"]["task_step"], 0)

    def dark_options(self, **extra):
        options = dict(workflow="dark-light", quest_catalog=str(ROOT / "packs/wvd/parameters/legacy-quests.json"),
            profile={**self.wall_profile(False), "RECOVER_WHEN_BEGINNING": True},
            extra_images=["darklight", "darklight_lightIt"])
        options.update(extra)
        return options

    def mining_options(self, **extra):
        options = dict(workflow="mining", quest_catalog=str(ROOT / "packs/wvd/parameters/legacy-quests.json"),
            profile=self.wall_profile(False), extra_images=["FFXI/GCN", "FFXI/ZONE2", "FFXI/FFXIStone", "leaveDung",
                "FFXI/org_position", "FFXI/receive", "FFXI/needpickaxe", "FFXI/nothingToDig", "FFXI/nothingToDig2"] +
                ["FFXI/org_" + name for name in ("fine", "high", "mid", "low", "refine", "alter", "sliver", "ouro", "lesser_full", "full")])
        options.update(extra)
        return options

    def mining_scenario(self, reward="fine", repeated=False):
        site = {"dungFlag": (50, 150), "FFXI/org_position": (710, 100)}
        receive = {**site, "FFXI/receive": (330, 700)}
        if reward != "unknown": receive["FFXI/org_" + reward] = (480, 780)
        end = {**site, "FFXI/nothingToDig": (400, 700)}
        frames = [site, receive]
        commands = [dict(kind=0, x=450, y=600)]
        if repeated:
            frames += [site, receive]
            commands += [dict(kind=0, x=450, y=600)] * 2
        frames += [end, {"leaveDung": (400, 700)}, {"returnText": (400, 700)}, {"openworldmap": (400, 700)}]
        commands += [dict(kind=0, x=450, y=600), dict(kind=0, x=1, y=1),
                     dict(kind=0, x=420, y=712), dict(kind=0, x=420, y=712)]
        return frames, commands

    def test_mining_reward_and_cold_start_complete_only_after_exit(self):
        frames, commands = self.mining_scenario()
        for cold in (False, True):
            r = self.execute("mining-cycle-" + str(cold), ([{}] if cold else []) + frames, commands,
                **self.mining_options(attach_recovery=cold, initial_connection="closed" if cold else "ready"))
            self.assertEqual(r["snapshot"]["state"], "Completed", r)
            self.assertEqual(r["backend_calls"], 5)
            self.assertFalse(r["mismatch"])
            self.assertEqual(r["snapshot"]["business"]["mining"]["rewards"]["fine"], 1)
            self.assertEqual(r["snapshot"]["business"]["mining"]["completed_cycles"], 1)
            self.assertEqual(r["snapshot"]["business"]["dungeons"], 0)
            self.assertEqual(r["lifecycle_calls"], ["RestartInstance", "EnsureVpn", "StartApplication"] if cold else [])

    def test_mining_unknown_and_repeated_equal_rewards_are_not_confused(self):
        for unknown in (False, True):
            label = "unknown" if unknown else "fine"
            frames, commands = self.mining_scenario(label, repeated=not unknown)
            r = self.execute("mining-counts-" + label, frames, commands, **self.mining_options())
            self.assertEqual(r["snapshot"]["state"], "Completed", r)
            self.assertEqual(r["backend_calls"], 5 if unknown else 7)
            self.assertFalse(r["mismatch"])
            counts = r["snapshot"]["business"]["mining"]["rewards"]
            self.assertEqual(counts[label], 1 if unknown else 2)
            self.assertEqual(sum(counts.values()), counts[label])

    def test_mining_reward_page_that_does_not_close_is_not_counted_twice(self):
        frames, _ = self.mining_scenario()
        r = self.execute("mining-reward-stuck", [frames[1], frames[1]], [dict(kind=0, x=450, y=600)], **self.mining_options())
        self.assertEqual(r["snapshot"]["state"], "Failed", r)
        self.assertIn("POSTCONDITION_TIMEOUT", str(r["snapshot"]))
        self.assertEqual(r["backend_calls"], 1)
        self.assertFalse(r["mismatch"])
        state = r["snapshot"]["business"]["mining"]
        self.assertEqual(state["rewards"]["fine"], 1)
        self.assertEqual(state["completed_cycles"], 0)

    def test_mining_stop_and_rejected_dig_never_record_reward(self):
        frames, commands = self.mining_scenario()
        for stopped in (False, True):
            r = self.execute("mining-stop-" + str(stopped), frames[:2], [{**commands[0], "reject": not stopped}],
                **self.mining_options(stop_after_first=stopped))
            self.assertEqual(r["snapshot"]["state"], "UserStopped" if stopped else "Failed", r)
            self.assertEqual(r["backend_calls"], 1)
            self.assertFalse(r["mismatch"])
            self.assertEqual(sum(r["snapshot"]["business"]["mining"]["rewards"].values()), 0)

    def test_mining_pickaxe_refill_assembles_then_rests(self):
        site = {"dungFlag": (50, 150), "FFXI/org_position": (710, 100)}
        inn = {"Inn": (400, 700), "guild": (500, 700)}
        party = {"PartyManagementTitle": (200, 300), "FFXI/FFXIStone": (300, 700)}
        frames = [site, {**site, "FFXI/needpickaxe": (400, 700)}, {"leaveDung": (400, 700)},
            {"returnText": (400, 700)}, {"openworldmap": (400, 700)},
            {"worldmapflag": (50, 300), "City_RoyalCityLuknalia": (400, 700)}, inn,
            {"Edit": (400, 700)}, {"PartyManagement": (400, 700)}, party,
            {**party, "AssembleParty": (400, 850)}, {**party, "OK": (400, 950)}, party, inn,
            {"Stay": (400, 700)}, {"Economy": (400, 700)}, {"OK": (400, 700)}, {"Stay": (400, 700)}, inn]
        commands = [dict(kind=0, x=x, y=y) for x, y in [(450, 600), (1, 1), (420, 712), (420, 712),
            (420, 712), (420, 712), (520, 712), (420, 712), (420, 712), (320, 712), (420, 862), (420, 962)]]
        commands += [dict(kind=5, key=4)] + [dict(kind=0, x=420, y=712)] * 4 + [dict(kind=5, key=4)]
        r = self.execute("mining-refill", frames, commands, **self.mining_options())
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 18)
        self.assertFalse(r["mismatch"])
        self.assertEqual(r["snapshot"]["business"]["inn_rests"], 1)
        self.assertFalse(r["snapshot"]["business"]["mining"]["refill_pending"])
        self.assertEqual(r["snapshot"]["business"]["mining"]["completed_cycles"], 1)

    def bounty_options(self, report=False, **extra):
        return dict(workflow="bounty-visit", report=report, profile=self.wall_profile(False),
            extra_images=["guild", "guildRequest", "guildFeatured", "Bounties", "CompletionReported", "EdgeOfTown"], **extra)

    def fishing_cast_options(self, **extra):
        return dict(workflow="fishing-cast", extra_images=["fishing/cast", "fishing/striking", "fishing/nobait", "fishing/8bait"], **extra)

    def fishing_reward_options(self, **extra):
        names = ["CloseFishInfo", "cast", "striking", "size_small", "size_average", "size_large", "鲈鱼", "雅罗", "鲶鱼", "鳟鱼", "鳗鱼", "三文鱼", "杂鱼"]
        return dict(workflow="fishing-reward", extra_images=["fishing/" + name for name in names], **extra)

    def test_fishing_reward_classification_unknowns_and_species_roi(self):
        for name, markers, size, species in [
            ("large-salmon", {"fishing/size_large": (300, 700), "fishing/三文鱼": (300, 1150)}, "大", "三文鱼"),
            ("species-outside-roi", {"fishing/size_small": (300, 700), "fishing/鲈鱼": (300, 1000)}, "小", "未收录"),
            ("size-unknown", {"fishing/鲶鱼": (300, 1150)}, None, None)]:
            page = {"fishing/CloseFishInfo": (400, 1400), **markers}
            r = self.execute("fishing-reward-" + name, [page, {"fishing/cast": (400, 1300)}],
                [dict(kind=0, x=420, y=1412)], **self.fishing_reward_options())
            self.assertEqual(r["snapshot"]["state"], "Completed", r)
            self.assertEqual(r["backend_calls"], 1)
            fish = r["snapshot"]["business"]["fishing"]
            self.assertEqual(fish["caught"], 1)
            self.assertFalse(fish["reward_pending"])
            self.assertEqual(fish["unclassified_size"], int(size is None))
            if size: self.assertEqual(fish["fishinfo"][size][species], 1)

    def test_fishing_reward_stuck_rejected_and_stopped_do_not_count(self):
        page = {"fishing/CloseFishInfo": (400, 1400), "fishing/size_average": (300, 700), "fishing/雅罗": (300, 1150)}
        for mode in ("stuck", "reject", "stop"):
            command = dict(kind=0, x=420, y=1412, reject=mode == "reject")
            r = self.execute("fishing-reward-" + mode, [page, page if mode == "stuck" else {"fishing/cast": (400, 1300)}],
                [command], **self.fishing_reward_options(stop_after_first=mode == "stop"))
            self.assertEqual(r["snapshot"]["state"], "UserStopped" if mode == "stop" else "Failed", r)
            self.assertEqual(r["backend_calls"], 1)
            fish = r["snapshot"]["business"]["fishing"]
            self.assertEqual(fish["caught"], 0)
            self.assertTrue(fish["reward_pending"])

    def test_fishing_reward_closed_page_without_receipt_is_not_success(self):
        r = self.execute("fishing-reward-no-receipt", [{"fishing/cast": (400, 1300)}], [], **self.fishing_reward_options())
        self.assertEqual(r["snapshot"]["state"], "Interrupted", r)
        self.assertEqual(r["backend_calls"], 0)
        self.assertEqual(r["snapshot"]["business"]["fishing"]["caught"], 0)

    def test_fishing_round_cast_reel_and_collect_uses_roi_and_new_frames(self):
        frames, commands = self.fishing_cast_scenario()
        # ROI外的浮标图案不应阻止拉杆；中央水域保持全黑，真实算法应返回NoHit。
        frames[-1]["fishing/bobber"] = (100, 200)
        frames.append({"fishing/CloseFishInfo": (400, 1400), "fishing/size_large": (300, 700), "fishing/三文鱼": (300, 1150)})
        frames.append({"fishing/cast": (400, 1300)})
        commands += [dict(kind=1, x=450, y=700, x2=450, y2=50, duration=100), dict(kind=0, x=420, y=1412)]
        options = self.fishing_reward_options()
        options["workflow"] = "fishing-round"
        options["extra_images"] += ["fishing/nobait", "fishing/8bait", "fishing/bobber"]
        r = self.execute("fishing-round-near", frames, commands, **options)
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 10)
        self.assertFalse(r["mismatch"])
        fish = r["snapshot"]["business"]["fishing"]
        self.assertEqual(fish["caught"], 1)
        self.assertEqual(fish["cast_sequence"], 1)
        self.assertEqual(fish["failed"], 0)
        self.assertFalse(fish["waiting"])
        self.assertEqual(fish["fishinfo"]["大"]["三文鱼"], 1)

    def test_fishing_seek_stops_as_soon_as_cast_page_appears(self):
        dungeon = {"dungFlag": (50, 150)}
        r = self.execute("fishing-seek-found", [dungeon, dungeon, {"fishing/cast": (400, 1300)}],
            [dict(kind=0, x=250, y=1200), dict(kind=1, x=250, y=1200, x2=850, y2=1200, duration=100)],
            workflow="fishing-seek")
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 2)
        self.assertFalse(r["mismatch"])

    def test_fishing_seek_unknown_and_already_fishing_do_not_rotate(self):
        for name, page, expected in [("unknown", {}, "Interrupted"), ("already", {"fishing/striking": (400, 1300)}, "Completed")]:
            r = self.execute("fishing-seek-" + name, [page], [], workflow="fishing-seek")
            self.assertEqual(r["snapshot"]["state"], expected, r)
            self.assertEqual(r["backend_calls"], 0)

    def test_fishing_round_real_bobber_waits_300_seconds_before_failed_cast(self):
        options = self.fishing_reward_options()
        options.update(workflow="fishing-round", real_bobber=True)
        options["extra_images"] += ["fishing/nobait", "fishing/8bait", "fishing/bobber"]
        r = self.execute("fishing-round-timeout", [{"fishing/striking": (400, 1300), "fishing/bobber": (400, 700)},
            {"fishing/cast": (400, 1300)}], [dict(kind=0, x=420, y=1312)], **options)
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 1)
        self.assertFalse(r["mismatch"])
        self.assertGreaterEqual(r["snapshot"]["business"]["elapsed_seconds"], 300)
        fish = r["snapshot"]["business"]["fishing"]
        self.assertEqual(fish["failed"], 1)
        self.assertEqual(fish["caught"], 0)
        self.assertFalse(fish["waiting"])

    def test_fishing_round_common_download_returns_to_same_round(self):
        frames, commands = self.fishing_cast_scenario()
        frames.insert(0, {"startdownload": (240, 920)})
        commands.insert(0, dict(kind=0, x=260, y=932))
        frames.extend([{"fishing/CloseFishInfo": (400, 1400), "fishing/size_large": (300, 700), "fishing/三文鱼": (300, 1150)},
            {"fishing/cast": (400, 1300)}])
        commands.extend([dict(kind=1, x=450, y=700, x2=450, y2=50, duration=100), dict(kind=0, x=420, y=1412)])
        options = self.fishing_reward_options()
        options["workflow"] = "fishing-round"
        options["extra_images"] += ["fishing/nobait", "fishing/8bait", "fishing/bobber"]
        r = self.execute("fishing-round-download", frames, commands, **options)
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 11)
        self.assertFalse(r["mismatch"])
        self.assertEqual(r["snapshot"]["business"]["fishing"]["caught"], 1)
        self.assertEqual(len(r["snapshot"]["sessions"]), 1)
        self.assertEqual(r["lifecycle_calls"], [])

    def test_fishing_round_unknown_page_has_90_second_window_without_input(self):
        options = self.fishing_reward_options()
        options["workflow"] = "fishing-round"
        options["extra_images"] += ["fishing/nobait", "fishing/8bait", "fishing/bobber"]
        r = self.execute("fishing-round-unknown", [{}], [], **options)
        self.assertEqual(r["snapshot"]["state"], "Interrupted", r)
        self.assertEqual(r["snapshot"]["sessions"][-1]["reason"], "quest.fishing_unknown_timeout")
        self.assertEqual(r["backend_calls"], 0)
        self.assertGreaterEqual(r["snapshot"]["business"]["elapsed_seconds"], 90)

    @staticmethod
    def fishing_cast_scenario(far=False):
        ready = {"fishing/cast": (400, 1300), "fishing/8bait": (550, 1490)}
        frames = [ready.copy() for _ in range(8)] + [{"fishing/striking": (400, 1300)}]
        commands = [dict(kind=1, x=50 if i < 5 else 850, y=1200, x2=850 if i < 5 else 50, y2=1200, duration=100) for i in range(7)]
        commands.append(dict(kind=1, x=400, y=1200, x2=450, y2=1250, duration=2250 if far else 4000))
        return frames, commands

    def test_fishing_cast_near_and_far_preserve_all_durations(self):
        for far in (False, True):
            frames, commands = self.fishing_cast_scenario(far)
            r = self.execute("fishing-cast-" + str(far), frames, commands, **self.fishing_cast_options(far=far))
            self.assertEqual(r["snapshot"]["state"], "Completed", r)
            self.assertEqual(r["backend_calls"], 8)
            self.assertFalse(r["mismatch"])

    def test_fishing_cast_empty_bait_or_unknown_never_sends_swipes(self):
        for name, frame in [("empty", {"fishing/cast": (400, 1300), "fishing/nobait": (550, 1490)}), ("unknown", {})]:
            r = self.execute("fishing-cast-" + name, [frame], [], **self.fishing_cast_options())
            self.assertEqual(r["snapshot"]["state"], "Interrupted", r)
            self.assertEqual(r["backend_calls"], 0)
            if name == "empty": self.assertEqual(r["snapshot"]["sessions"][-1]["reason"], "quest.fishing_bait_required")

    def test_fishing_cast_tied_zero_and_eight_scores_do_not_refill(self):
        frames, commands = self.fishing_cast_scenario()
        for frame in frames[:-1]:
            frame.pop("fishing/8bait")
            frame["fishing/nobait"] = (550, 1490)
        r = self.execute("fishing-cast-tied-bait", frames, commands,
            **self.fishing_cast_options(mod_images={"fishing/8bait": "fishing/nobait"}, mod_only_images=["fishing/8bait"]))
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 8)
        self.assertEqual(r["image_sources"]["images"]["fishing/8bait.png"]["source"], "mod")

    def test_fishing_cast_reject_and_stop_cancel_the_remaining_swipes(self):
        for stop in (False, True):
            frames, commands = self.fishing_cast_scenario()
            commands[0]["reject"] = not stop
            r = self.execute("fishing-cast-stop-" + str(stop), frames[:2], commands[:1],
                **self.fishing_cast_options(stop_after_first=stop))
            self.assertEqual(r["snapshot"]["state"], "UserStopped" if stop else "Failed", r)
            self.assertEqual(r["backend_calls"], 1)

    def test_fishing_cast_changed_scene_cannot_continue_adjusting(self):
        frames, commands = self.fishing_cast_scenario()
        r = self.execute("fishing-cast-scene-changed", [frames[0], {"Inn": (400, 700)}], commands[:1], **self.fishing_cast_options())
        self.assertEqual(r["snapshot"]["state"], "Failed", r)
        self.assertEqual(r["backend_calls"], 1)

    def fishing_cycle_options(self, **extra):
        options = self.fishing_reward_options()
        options.update(workflow="fishing-cycle", profile=self.wall_profile(False),
            quest_catalog=str(ROOT / "packs/wvd/parameters/legacy-quests.json"),
            aliases={"returntoTown.png": "returntotown.png"})
        options["extra_images"] += ["fishing/nobait", "fishing/8bait", "fishing/bobber", "fishing/quit",
            "fishing/startfishing", "fishing/iconbait", "fishing/baitbox", "itemList", "transfer",
            "whowillyougiveitto", "DH", "DH-R6"]
        options.update(extra)
        return options

    @staticmethod
    def fishing_supply_scenario():
        frames = [{"fishing/cast": (400, 1300), "fishing/nobait": (550, 1490), "fishing/quit": (100, 1400)}]
        commands = []
        def advance(command, page):
            commands.append(command)
            frames.append(page)
        def click(x, y, page):
            advance(dict(kind=0, x=x, y=y), page)
        def point(x, y):
            swipe = dict(kind=1, x=700, y=250, x2=100, y2=1200, duration=400)
            advance(swipe.copy(), {"mapFlag": (100, 100)})
            click(x, y, {"mapFlag": (100, 100)})
            click(136, 1431, {"dungFlag": (50, 150)})
            reached = {"mapFlag": (100, 100), "cursor_0": (x - 20, y - 12)}
            click(777, 150, reached)
            advance(swipe.copy(), reached)
        click(120, 1412, {"dungFlag": (50, 150)})
        click(777, 150, {"mapFlag": (100, 100)})
        point(818, 928)
        click(860, 1150, {"itemList": (400, 700)})
        click(135, 1294, {"fishing/iconbait": (100, 700)})
        box = {"whowillyougiveitto": (300, 200), "fishing/baitbox": (400, 700)}
        click(759, 712, box)
        for _ in range(70):
            click(420, 712, box.copy())
        advance(dict(kind=5, key=4), {"Inn": (400, 700), "DH": (200, 500)})
        boundaries = [len(commands)]
        click(220, 512, {"DH-R6": (400, 700)})
        click(420, 712, {"GotoDung": (400, 700)})
        click(420, 712, {"mapFlag": (100, 100)})
        point(339, 555)
        click(120, 112, {"dungFlag": (50, 150), "fishing/startfishing": (400, 700)})
        click(420, 712, {"fishing/cast": (400, 1300), "fishing/8bait": (550, 1490)})
        boundaries.append(len(commands))
        return frames, commands, boundaries

    def test_fishing_supply_returns_to_water_then_catches_in_next_unit(self):
        frames, commands, boundaries = self.fishing_supply_scenario()
        cast_frames, cast_commands = self.fishing_cast_scenario()
        frames.extend(cast_frames[1:])
        commands.extend(cast_commands)
        frames.extend([{"fishing/CloseFishInfo": (400, 1400), "fishing/size_large": (300, 700), "fishing/三文鱼": (300, 1150)},
            {"fishing/cast": (400, 1300), "fishing/8bait": (550, 1490)}])
        commands.extend([dict(kind=1, x=450, y=700, x2=450, y2=50, duration=100), dict(kind=0, x=420, y=1412)])
        r = self.execute("fishing-supply-full", frames, commands, **self.fishing_cycle_options(normal_units=3))
        self.assertEqual(boundaries, [81, 91])
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 101)
        self.assertFalse(r["mismatch"])
        self.assertEqual(len(r["snapshot"]["sessions"]), 3)
        fish = r["snapshot"]["business"]["fishing"]
        self.assertEqual(fish["transfer_inputs_confirmed"], 70)
        self.assertEqual(fish["refill_trips_completed"], 1)
        self.assertEqual(fish["refill_phase"], 0)
        self.assertEqual(fish["caught"], 1)
        self.assertEqual(fish["fishinfo"]["大"]["三文鱼"], 1)

    def test_fishing_supply_rejected_transfer_preserves_intent_and_stops(self):
        frames, commands, _ = self.fishing_supply_scenario()
        # 第一笔实际转交是第11次输入；拒绝发生后不得继续后面的69次。
        commands[10]["reject"] = True
        r = self.execute("fishing-supply-rejected", frames[:12], commands[:11], **self.fishing_cycle_options())
        self.assertEqual(r["snapshot"]["state"], "Failed", r)
        self.assertEqual(r["backend_calls"], 11)
        self.assertFalse(r["mismatch"])
        fish = r["snapshot"]["business"]["fishing"]
        self.assertTrue(fish["transfer_pending"])
        self.assertEqual(fish["transfer_inputs_confirmed"], 0)
        self.assertEqual(fish["refill_trips_completed"], 0)

    def test_fishing_supply_unknown_entry_does_not_touch_inventory(self):
        r = self.execute("fishing-supply-unknown", [{}], [], **self.fishing_cycle_options())
        self.assertEqual(r["snapshot"]["state"], "Interrupted", r)
        self.assertEqual(r["backend_calls"], 0)
        self.assertFalse(r["mismatch"])
        self.assertEqual(r["snapshot"]["business"]["fishing"]["refill_phase"], 0)

    def test_jier_dialogue_scoped_choice_and_bondmate_close(self):
        option = {"bounty/cuthimdown": (400, 600), "dialogueChoices/nope": (400, 1000)}
        close = {"bondmate_close": (400, 900)}
        r = self.execute("jier-dialogue-bondmate", [option, close, {"dungFlag": (50, 150)}],
            [dict(kind=0, x=420, y=612), dict(kind=0, x=420, y=912)], workflow="common", jier_dialogue=True,
            extra_images=["bounty/cuthimdown", "bondmate_close"])
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 2)
        self.assertFalse(r["mismatch"])
        self.assertEqual(r["snapshot"]["business"]["special_dialogues_completed"], 1)
        self.assertFalse(r["snapshot"]["business"]["special_dialogue_pending"])

    def featured_options(self, **extra):
        return dict(workflow="featured-request", extra_images=["guild", "guildRequest", "guildFeatured",
            "LBC/request", "SSC/Request", "request_accepted"], **extra)

    def featured_scenario(self, bull=True, accepted=None):
        frames, commands = self.inn_sequence()
        frames[-1]["guild"] = (200, 500)
        target = "LBC/request" if bull else "SSC/Request"
        listing = {"guildFeatured": (100, 300), target: (400, 800)}
        if accepted is not None: listing["request_accepted"] = accepted
        def advance(command, page):
            commands.append(command)
            frames.append(page)
        advance(dict(kind=0, x=220, y=512), {"guildRequest": (400, 500)})
        advance(dict(kind=0, x=420, y=512), {"guildFeatured": (100, 300)})
        advance(dict(kind=0, x=120, y=312), listing)
        for _ in range(3 if bull else 1):
            advance(dict(kind=1, x=150, y=1000 if bull else 1300, x2=150, y2=200, duration=400), listing)
        selected = not bull or accepted is None or accepted[1] < 612
        if selected:
            advance(dict(kind=0, x=686 if bull else 720, y=1069 if bull else 962), {"guildRequest": (400, 500)})
        advance(dict(kind=5, key=4), {"Inn": (400, 700)})
        return frames, commands, selected

    def test_featured_requests_keep_scroll_bias_and_accepted_roi(self):
        for name, bull, accepted in [("bull", True, None), ("already", True, (400, 1100)),
                ("outside", True, (400, 200)), ("golden", False, (400, 1100))]:
            frames, commands, selected = self.featured_scenario(bull, accepted)
            r = self.execute("featured-" + name, frames, commands, **self.featured_options(bull=bull))
            self.assertEqual(r["snapshot"]["state"], "Completed", r)
            self.assertEqual(r["backend_calls"], len(commands))
            self.assertFalse(r["mismatch"])
            state = r["snapshot"]["business"]
            self.assertEqual(state["inn_rests"], 1)
            self.assertEqual(state["featured_visit"]["selections_confirmed"], int(selected))
            self.assertEqual(state["featured_visit"]["visits_completed"], 1)
            self.assertFalse(state["featured_visit"]["pending"])

    def test_featured_rejected_or_stopped_selection_does_not_replay(self):
        for stop in (False, True):
            frames, commands, _ = self.featured_scenario()
            commands[-2] = dict(commands[-2], reject=not stop)
            options = self.featured_options(**({"stop_after_calls": len(commands) - 1} if stop else {}))
            r = self.execute("featured-stop-" + str(stop), frames[:-1], commands[:-1], **options)
            self.assertEqual(r["snapshot"]["state"], "UserStopped" if stop else "Failed", r)
            self.assertEqual(r["backend_calls"], len(commands) - 1)
            self.assertFalse(r["mismatch"])
            self.assertTrue(r["snapshot"]["business"]["featured_visit"]["pending"])
            self.assertEqual(r["snapshot"]["business"]["featured_visit"]["visits_completed"], 0)

    def test_featured_unknown_page_never_opens_inn_or_selects_request(self):
        r = self.execute("featured-unknown", [{}], [], **self.featured_options())
        self.assertEqual(r["snapshot"]["state"], "Interrupted", r)
        self.assertEqual(r["backend_calls"], 0)
        self.assertFalse(r["snapshot"]["business"]["featured_visit"]["active"])

    def test_jier_dialogue_not_enabled_cannot_choose_special_option(self):
        r = self.execute("jier-dialogue-unbound", [{"bounty/cuthimdown": (400, 600)}], [], workflow="common",
            extra_images=["bounty/cuthimdown", "bondmate_close"])
        self.assertEqual(r["snapshot"]["state"], "Interrupted", r)
        self.assertEqual(r["backend_calls"], 0)
        self.assertEqual(r["snapshot"]["business"]["special_dialogues_completed"], 0)

    def test_golden_dialogue_order_and_bondmate_close_are_task_scoped(self):
        r = self.execute("golden-dialogue-sequence",
            [{"SSC/dotdotdot": (400, 600), "SSC/shadow": (400, 1000)}, {"SSC/shadow": (400, 1000)},
             {"bondmate_close": (400, 900)}, {"dungFlag": (50, 150)}],
            [dict(kind=0, x=420, y=612), dict(kind=0, x=420, y=1012), dict(kind=0, x=420, y=912)],
            workflow="common", golden_dialogue=True, extra_images=["SSC/dotdotdot", "SSC/shadow", "bondmate_close"])
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 3)
        self.assertFalse(r["mismatch"])
        self.assertEqual(r["snapshot"]["business"]["special_dialogues_completed"], 2)
        self.assertFalse(r["snapshot"]["business"]["special_dialogue_pending"])

    def test_causality_disables_then_enables_with_rgb_and_fixed_roi_stop(self):
        symbol = {"LBC/symbolofalliance": (100, 200)}
        frames = [{"CSC": (300, 700), "leap": (400, 900)},
            {**symbol, "didnottakethequest": (400, 700)}, symbol,
            {**symbol, "LBC/EnaWasSaved": (400, 800)}, symbol, symbol, {"leap": (400, 900)}]
        commands = [dict(kind=0, x=320, y=712), dict(kind=0, x=420, y=712),
            dict(kind=1, x=150, y=500, x2=150, y2=400, duration=400), dict(kind=0, x=420, y=812),
            dict(kind=1, x=150, y=400, x2=150, y2=500, duration=400), dict(kind=5, key=4)]
        r = self.execute("causality-settings", frames, commands, workflow="causality")
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 6)
        self.assertFalse(r["mismatch"])

    def test_causality_fast_visible_leap_does_not_open_settings(self):
        r = self.execute("causality-fast", [{"cursedWheelTitle": (200, 100), "GhostsOfYore": (300, 900)},
            {"cursedWheelTitle": (200, 100), "leap": (400, 900)}, {"Inn": (400, 700)}],
            [dict(kind=0, x=320, y=912), dict(kind=0, x=420, y=912)], workflow="time-leap", leap_target="GhostsOfYore", causality=True)
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 2)
        self.assertFalse(r["mismatch"])

    def test_causality_changed_roi_requires_another_scroll_before_closing(self):
        symbol = {"LBC/symbolofalliance": (100, 200)}
        # 两个方向都先有大区域变化，再保持不变。变化位于旧固定ROI内且不遮挡页签。
        frames = [symbol] * 5 + [{"leap": (400, 900)}]
        commands = [dict(kind=1, x=150, y=500, x2=150, y2=400, duration=400)] * 2
        commands += [dict(kind=1, x=150, y=400, x2=150, y2=500, duration=400)] * 2
        commands += [dict(kind=5, key=4)]
        regions = {str(i): [[200, 500, 500, 500, value]] for i, value in ((1, 100), (2, 100), (3, 200), (4, 200))}
        self.assertGreater(100 * 500 * 500 / (255 * 757 * 1068), .006)
        r = self.execute("causality-changing", frames, commands, workflow="causality", frame_rectangles=regions)
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 5)
        self.assertFalse(r["mismatch"], r.get("mismatch_detail"))

    def test_causality_slow_leap_completes_settings_before_leap(self):
        title = {"cursedWheelTitle": (200, 100)}
        chapter = {**title, "cursedwheel_impregnableFortress": (300, 500)}
        target = {**title, "GhostsOfYore": (300, 900)}
        menu = {**title, "CSC": (300, 700), "leap": (400, 900)}
        symbol = {"LBC/symbolofalliance": (100, 200)}
        frames = [title] * 10 + [chapter, target, menu,
            {**symbol, "didnottakethequest": (400, 700)}, symbol,
            {**symbol, "LBC/EnaWasSaved": (400, 800)}, symbol, symbol, menu, {"Inn": (400, 700)}]
        commands = [dict(kind=0, x=105, y=230)] * 10
        commands += [dict(kind=0, x=320, y=512), dict(kind=0, x=320, y=912),
            dict(kind=0, x=320, y=712), dict(kind=0, x=420, y=712),
            dict(kind=1, x=150, y=500, x2=150, y2=400, duration=400), dict(kind=0, x=420, y=812),
            dict(kind=1, x=150, y=400, x2=150, y2=500, duration=400), dict(kind=5, key=4),
            dict(kind=0, x=420, y=912)]
        r = self.execute("causality-slow", frames, commands, workflow="time-leap", leap_target="GhostsOfYore", causality=True)
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 19)
        self.assertFalse(r["mismatch"], r.get("mismatch_detail"))

    def test_causality_stop_or_rejection_never_scrolls_or_leaps_after_open(self):
        for stop in (False, True):
            r = self.execute("causality-stop-" + str(stop),
                [{"CSC": (300, 700), "leap": (400, 900)}, {"LBC/symbolofalliance": (100, 200)}],
                [dict(kind=0, x=320, y=712, reject=not stop)], workflow="causality",
                **({"stop_after_calls": 1} if stop else {}))
            self.assertEqual(r["snapshot"]["state"], "UserStopped" if stop else "Failed", r)
            self.assertEqual(r["backend_calls"], 1)
            self.assertFalse(r["mismatch"])

    def test_causality_unknown_postcondition_and_missing_template_are_not_success(self):
        for missing in (False, True):
            r = self.execute("causality-invalid-" + str(missing),
                [{"CSC": (300, 700), "leap": (400, 900)}, {}], [dict(kind=0, x=320, y=712)], workflow="causality",
                **({"omit_image": "LBC/symbolofalliance.png"} if missing else {}))
            self.assertEqual(r["backend_calls"], 0 if missing else 1)
            if missing:
                self.assertEqual(r["publish_error"], "COMPILE_IMAGE_NOT_IN_MANIFEST:image/LBC/symbolofalliance.png")
                self.assertEqual(r["connections"], 0)
            else:
                self.assertEqual(r["snapshot"]["state"], "Failed", r)
                self.assertFalse(r["mismatch"])
                self.assertEqual(r["snapshot"]["sessions"][-1]["reason"], "POSTCONDITION_TIMEOUT")

    def repel_forces_options(self, **extra):
        profile = extra.pop("profile", {})
        profile.update(DEFAULT_OVERALL_STRATEGY="全自动战斗", STRATEGY=[], REST_INTERVEL=1)
        options = self.scorpion_options(profile=profile, **extra)
        options["workflow"] = "repel-forces"
        options["extra_images"] += ["7thDist", "icanstillgo", "letswithdraw", "leaveDung"]
        return options

    def repel_forces_scenario(self):
        frames, commands = self.inn_sequence()
        frames[-1] = {"Inn": (400, 700), "TradeWaterway": (300, 800)}
        events = []
        def advance(command, page):
            commands.append(command); frames.append(page)
        def click(x, y, page): advance(dict(kind=0, x=x, y=y), page)
        click(320, 812, {"7thDist": (400, 700)})
        click(420, 712, {"dungFlag": (50, 150)})
        page = {"mapFlag": (100, 100)}
        click(777, 150, page)
        def point(x, y, upper):
            swipe = dict(kind=1, x=100, y=250 if upper else 1200, x2=700, y2=1200 if upper else 250, duration=400)
            advance(swipe.copy(), page)
            click(x, y, page)
            click(136, 1431, {"dungFlag": (50, 150)})
            reached = {**page, "cursor_0": (x-20, y-12)}
            click(777, 150, reached)
            advance(swipe.copy(), reached)
        point(559, 599, False); point(186, 813, False)
        prompt = {"icanstillgo": (300, 700), "letswithdraw": (400, 900)}
        click(1, 1, prompt)
        for _ in range(2):
            click(320, 712, self.turn_screen(**{"spellskill/CombatAutoDisable": (800, 1070)}))
            click(850, 1100, self.turn_screen(**{"spellskill/CombatAutoEnable": (800, 1070)}))
            # 先真实确认Auto，再由“该输入之后经过四秒”的独立事件结束动画。
            # 重复capture不改变剧情；双战各自必须产生对应的Auto输入。
            frames.append(prompt)
            events.append(dict(after_input=len(commands), delay_ms=4000, frame=len(frames)-1))
        click(420, 912, {"dungFlag": (50, 150)})
        click(777, 150, page)
        point(612, 448, True)
        click(1, 1, {"returnText": (400, 700)})
        click(420, 712, {"Inn": (400, 700)})
        return frames, commands, events

    def test_repel_forces_full_two_battles_then_withdraw_and_return(self):
        frames, commands, events = self.repel_forces_scenario()
        r = self.execute("repel-forces-full", frames, commands, time_events=events, **self.repel_forces_options())
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 32)
        self.assertEqual(r["time_event_count"], 2)
        self.assertFalse(r["mismatch"], r.get("mismatch_detail"))
        self.assertEqual(len(r["snapshot"]["sessions"]), 3)
        self.assertEqual(r["snapshot"]["business"]["repel_forces"]["completed_cycles"], 1)
        self.assertEqual(r["snapshot"]["business"]["combats"], 2)
        self.assertEqual(r["snapshot"]["business"]["inn_rests"], 1)

    def test_repel_forces_stop_or_rejection_preserves_first_battle_intent(self):
        for stop in (False, True):
            frames, commands, _ = self.repel_forces_scenario()
            commands[19] = dict(commands[19], reject=not stop)
            r = self.execute("repel-forces-stop-" + str(stop), frames[:21], commands[:20],
                **self.repel_forces_options(**({"stop_after_calls": 20} if stop else {})))
            self.assertEqual(r["snapshot"]["state"], "UserStopped" if stop else "Failed", r)
            self.assertEqual(r["backend_calls"], 20)
            self.assertFalse(r["mismatch"])
            self.assertTrue(r["snapshot"]["business"]["repel_forces"]["pending"])
            self.assertEqual(r["snapshot"]["business"]["repel_forces"]["confirmed_battles"], 0)

    def steel_trial_options(self, **extra):
        options = self.scorpion_options(**extra)
        options["workflow"] = "steel-trial"
        options["extra_images"] += ["gradeexam", "Steel", "ready", "noneed", "quit", "bondmate_close"]
        return options

    def steel_trial_scenario(self):
        frames = [{"guild": (400, 700), "Inn": (200, 500)}]
        commands = []
        def advance(command, page):
            commands.append(command)
            frames.append(page)
        def click(x, y, page): advance(dict(kind=0, x=x, y=y), page)
        click(420, 712, {"guildRequest": (400, 700)})
        click(420, 712, {"gradeexam": (400, 700)})
        click(420, 712, {"Steel": (400, 700)})
        click(726, 970, {"ready": (500, 900)})
        page = {"mapFlag": (100, 100)}
        click(520, 912, page)
        for x, y, upper in ((131, 769, True), (827, 447, True), (131, 769, True), (719, 1080, False)):
            swipe = dict(kind=1, x=100, y=250 if upper else 1200, x2=700, y2=1200 if upper else 250, duration=400)
            advance(swipe.copy(), page)
            click(x, y, page)
            click(136, 1431, {"dungFlag": (50, 150)})
            reached = {**page, "cursor_0": (x-20, y-12)}
            click(777, 150, reached)
            advance(swipe.copy(), reached)
        click(1, 1, {"quit": (500, 900)})
        click(520, 912, {"Inn": (400, 700)})
        sleep, inputs = self.inn_sequence()
        frames += sleep[1:]
        commands += inputs
        return frames, commands

    def test_steel_trial_full_route_dialogue_and_rest_even_when_general_rest_disabled(self):
        frames, commands = self.steel_trial_scenario()
        r = self.execute("steel-trial-full", frames, commands,
            **self.steel_trial_options(profile=dict(ACTIVE_REST=False, REST_INTERVEL=1)))
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 32)
        self.assertFalse(r["mismatch"], r.get("mismatch_detail"))
        self.assertEqual(r["snapshot"]["business"]["steel_trial"]["completed_cycles"], 1)
        self.assertEqual(r["snapshot"]["business"]["inn_rests"], 1)
        self.assertEqual(r["snapshot"]["business"]["special_dialogues_completed"], 2)

    def test_steel_trial_rejected_or_stopped_selection_never_starts_route(self):
        for stop in (False, True):
            frames, commands = self.steel_trial_scenario()
            commands[3] = dict(commands[3], reject=not stop)
            r = self.execute("steel-trial-stop-" + str(stop), frames[:5], commands[:4],
                **self.steel_trial_options(**({"stop_after_calls": 4} if stop else {})))
            self.assertEqual(r["snapshot"]["state"], "UserStopped" if stop else "Failed", r)
            self.assertEqual(r["backend_calls"], 4)
            self.assertFalse(r["mismatch"])
            self.assertTrue(r["snapshot"]["business"]["steel_trial"]["pending"])
            self.assertEqual(r["snapshot"]["business"]["steel_trial"]["completed_cycles"], 0)

    def bull_cave_options(self, rest, **extra):
        profile = extra.pop("profile", {})
        profile.update(ACTIVE_REST=rest)
        options = self.scorpion_options(profile=profile, **extra)
        options["workflow"] = "bull-cave"
        options["extra_images"] += ["LBC/LBC", "LBC/LBC_quit", "LBC/request", "SSC/Request", "request_accepted",
            "CSC", "didnottakethequest", "LBC/symbolofalliance", "LBC/EnaWasSaved"]
        return options

    def bull_cave_scenario(self, rest):
        frames = [{"cursedWheelTitle": (200, 100), "GhostsOfYore": (300, 900)}]
        commands = []
        def advance(command, page):
            commands.append(command)
            frames.append(page)
        def click(x, y, page): advance(dict(kind=0, x=x, y=y), page)
        click(320, 912, {"cursedWheelTitle": (200, 100), "leap": (400, 900)})
        click(420, 912, {"Inn": (400, 700), "intoWorldMap": (300, 600)})
        click(320, 612, {"worldmapflag": (80, 100), "City_RoyalCityLuknalia": (132, 1352)})
        click(152, 1364, {"Inn": (400, 700), "guild": (200, 500)})
        visit, inputs, _ = self.featured_scenario()
        frames += visit[1:]
        commands += inputs
        frames[-1]["intoWorldMap"] = (300, 600)
        routes = [[(134, 342, "left-up")], [(500, 395, "right-up"), (340, 1027, "right-down")]] if rest else [[(134, 342, "left-up"), (500, 395, "right-up"), (340, 1027, "right-down")]]
        gestures = {"left-up": (100, 250, 700, 1200), "right-up": (700, 250, 100, 1200), "right-down": (700, 1200, 100, 250)}
        for index, route in enumerate(routes):
            click(320, 612, {"worldmapflag": (80, 100), "LBC/LBC": (400, 700)})
            page = {"mapFlag": (100, 100)}
            click(420, 712, {"dungFlag": (50, 150)})
            click(777, 150, page)
            for x, y, direction in route:
                sx, sy, tx, ty = gestures[direction]
                swipe = dict(kind=1, x=sx, y=sy, x2=tx, y2=ty, duration=400)
                advance(swipe.copy(), page)
                click(x, y, page)
                click(136, 1431, {"dungFlag": (50, 150)})
                reached = {**page, "cursor_0": (x - 20, y - 12)}
                if (x, y) == route[-1][:2]: reached["LBC/LBC_quit"] = (400, 700)
                click(777, 150, reached)
                advance(swipe.copy(), reached)
            click(420, 712, reached)
            click(136, 1431, {"Inn": (400, 700), "intoWorldMap": (300, 600)})
            if rest and index == 0:
                sleep, inputs = self.inn_sequence()
                frames += sleep[1:]
                commands += inputs
                frames[-1]["intoWorldMap"] = (300, 600)
        return frames, commands

    def test_bull_cave_both_rest_routes_keep_request_and_separate_inn_receipts(self):
        for rest in (False, True):
            frames, commands = self.bull_cave_scenario(rest)
            r = self.execute("bull-cave-full-" + str(rest), frames, commands, **self.bull_cave_options(rest))
            self.assertEqual(r["snapshot"]["state"], "Completed", r)
            self.assertEqual(r["backend_calls"], 47 if rest else 37)
            self.assertFalse(r["mismatch"])
            self.assertEqual(r["snapshot"]["business"]["bull_cave"]["completed_cycles"], 1)
            self.assertEqual(r["snapshot"]["business"]["inn_rests"], 2 if rest else 1)
            self.assertEqual(len(r["snapshot"]["sessions"]), 3 if rest else 2)

    def test_bull_cave_stop_and_rejected_leap_keep_pending(self):
        for stop in (False, True):
            frames, commands = self.bull_cave_scenario(False)
            r = self.execute("bull-cave-stop-" + str(stop), frames[:2], [dict(commands[0], reject=not stop)],
                **self.bull_cave_options(False, stop_after_first=stop))
            self.assertEqual(r["snapshot"]["state"], "UserStopped" if stop else "Failed", r)
            self.assertEqual(r["backend_calls"], 1)
            self.assertFalse(r["mismatch"])
            self.assertTrue(r["snapshot"]["business"]["bull_cave"]["leap_pending"])

    def gold_income_options(self, **extra):
        options = self.scorpion_options(**extra)
        options["workflow"] = "gold-income"
        options["extra_images"] += ["FortressArrival", "fastforward"] + ["7000G/" + x for x in
            ("illgonow", "olddist", "iminhungry", "royalcapital", "why", "leavethechild", "icantagreewithU", "illgo", "noeasytask")]
        return options

    def gold_income_scenario(self, hungry=False):
        frames = [{"cursedWheelTitle": (200, 100), "FortressArrival": (300, 900)}]
        commands = []
        def click(x, y, page):
            commands.append(dict(kind=0, x=x, y=y))
            frames.append(page)
        def choice(name): return {"7000G/" + name: (400, 700)}
        fast = {"fastforward": (700, 1400)}
        world = {"intoWorldMap": (300, 600)}
        click(320, 912, {"cursedWheelTitle": (200, 100), "leap": (400, 900)})
        click(420, 912, {"Inn": (400, 700), **world})
        click(320, 612, {"worldmapflag": (80, 100), "City_RoyalCityLuknalia": (132, 1352)})
        click(152, 1364, {"Inn": (400, 700), "guild": (200, 500)})
        click(220, 512, choice("illgonow"))
        click(420, 712, choice("iminhungry" if hungry else "olddist"))
        if hungry: click(420, 712, choice("olddist"))
        click(420, 712, fast)
        click(1, 1, fast)
        click(1, 1, choice("royalcapital"))
        click(420, 712, world)
        for person, (x, y) in enumerate(((450, 1111), (200, 1180), (680, 1200))):
            click(x, y, fast)
            click(1, 1, choice("why"))
            click(420, 712, choice("leavethechild") if person == 2 else world)
        click(420, 712, choice("icantagreewithU"))
        click(420, 712, choice("illgo"))
        click(420, 712, choice("noeasytask"))
        click(420, 712, {"ruins": (100, 300)})
        return frames, commands

    def test_gold_income_full_story_preserves_all_three_people_and_estimate(self):
        for hungry in (False, True):
            frames, commands = self.gold_income_scenario(hungry)
            r = self.execute("gold-income-full-" + str(hungry), frames, commands, **self.gold_income_options())
            self.assertEqual(r["snapshot"]["state"], "Completed", r)
            self.assertEqual(r["backend_calls"], 24 if hungry else 23)
            self.assertFalse(r["mismatch"])
            self.assertEqual(r["snapshot"]["business"]["gold_income"]["estimated_income"], 7000)
            self.assertEqual(r["snapshot"]["business"]["gold_income"]["completed_cycles"], 1)

    def test_gold_income_rejected_or_stopped_leap_preserves_intent_without_income(self):
        for stop in (False, True):
            frames, commands = self.gold_income_scenario()
            r = self.execute("gold-income-stop-" + str(stop), frames[:2], [dict(commands[0], reject=not stop)],
                **self.gold_income_options(stop_after_first=stop))
            self.assertEqual(r["snapshot"]["state"], "UserStopped" if stop else "Failed", r)
            self.assertEqual(r["backend_calls"], 1)
            self.assertFalse(r["mismatch"])
            self.assertTrue(r["snapshot"]["business"]["gold_income"]["pending"])
            self.assertEqual(r["snapshot"]["business"]["gold_income"]["estimated_income"], 0)

    def sandman_options(self, **extra):
        options = self.scorpion_options(**extra)
        options["workflow"] = "sandman"
        options["extra_images"] += ["stair_fortress3f", "impregnableFortress", "fortressb3f", "harken2",
            "requestToRescueTheDuke", "sandman/sandman_1", "sandman/sandman_2", "sandman/sandman_bondmate", "bondmate_close"]
        return options

    def sandman_scenario(self, bond):
        page = {"mapFlag": (100, 100), "stair_fortress3f": (200, 300)}
        frames, commands = [page], []
        def advance(command, after):
            commands.append(command)
            frames.append(after)
        def click(x, y, after): advance(dict(kind=0, x=x, y=y), after)
        swipe = dict(kind=1, x=100, y=1200, x2=700, y2=250, duration=400)
        for i, (x, y) in enumerate(((133, 814), (238, 1076), (450, 924))):
            advance(swipe.copy(), page)
            click(x, y, page)
            if bond and i == 0:
                click(136, 1431, {"sandman/sandman_1": (400, 600)})
                click(420, 612, {"sandman/sandman_2": (400, 700)})
                click(420, 712, {"sandman/sandman_bondmate": (400, 800)})
                click(420, 812, {"bondmate_close": (400, 900)})
                click(420, 912, {"dungFlag": (50, 150)})
            else:
                click(136, 1431, {"dungFlag": (50, 150)})
            reached = {**page, "cursor_0": (x - 20, y - 12)}
            click(777, 150, reached)
            advance(swipe.copy(), reached)
        # 未到达时图标中心仍和模板相同；中心被光标覆盖才表示已到达，不能提前合成。
        gate = {**page, "harken2": (400, 700)}
        advance(swipe.copy(), gate)
        click(420, 712, gate)
        city = {"Inn": (400, 700), "ruins": (100, 300)}
        click(136, 1431, city)
        if bond:
            for target in ("requestToRescueTheDuke", "Triumph"):
                rest, inputs = self.inn_sequence()
                frames += rest[1:]
                commands += inputs
                frames[-1]["ruins"] = (100, 300)
                click(120, 312, {"cursedWheelTitle": (200, 100), target: (300, 900)})
                click(320, 912, {"cursedWheelTitle": (200, 100), "leap": (400, 900)})
                click(420, 912, city)
        return frames, commands

    def test_sandman_without_bondmate_finishes_visit_without_rest_or_leap(self):
        frames, commands = self.sandman_scenario(False)
        r = self.execute("sandman-no-bond", frames, commands,
            **self.sandman_options())
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 18)
        self.assertFalse(r["mismatch"])
        self.assertEqual(r["snapshot"]["business"]["sandman"]["completed_cycles"], 0)
        self.assertEqual(r["snapshot"]["business"]["inn_rests"], 0)

    def test_sandman_bondmate_route_two_rests_and_ordered_leaps(self):
        frames, commands = self.sandman_scenario(True)
        r = self.execute("sandman-bond", frames, commands,
            **self.sandman_options())
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 38)
        self.assertFalse(r["mismatch"])
        state = r["snapshot"]["business"]
        self.assertEqual(state["sandman"]["completed_cycles"], 1)
        self.assertEqual(state["inn_rests"], 2)
        self.assertEqual(state["special_dialogues_completed"], 3)
        self.assertFalse(state["sandman"]["leap_pending"])

    def test_sandman_rejected_and_stopped_map_input_does_not_count_bondmate(self):
        for stop in (False, True):
            frames, commands = self.sandman_scenario(False)
            r = self.execute("sandman-stop-" + str(stop), frames[:2], [dict(commands[0], reject=not stop)],
                **self.sandman_options(stop_after_first=stop))
            self.assertEqual(r["snapshot"]["state"], "UserStopped" if stop else "Failed", r)
            self.assertEqual(r["backend_calls"], 1)
            self.assertFalse(r["mismatch"])
            self.assertEqual(r["snapshot"]["business"]["sandman"]["completed_cycles"], 0)

    def golden_options(self, **extra):
        options = self.scorpion_options(**extra)
        options["workflow"] = "golden-chest"
        options["extra_images"] += ["SSC/Leap", "SSC/Request", "SSC/SSC", "SSC/SSC_quit",
            "SSC/trapdeactived", "SSC/dotdotdot", "SSC/shadow", "bondmate_close", "specialRequest"]
        return options

    def test_golden_two_units_follow_route_when_all_four_chest_searches_are_empty(self):
        frames = [{"cursedWheel": (400, 700)}]
        commands = []
        def advance(command, page):
            commands.append(command)
            frames.append(page)
        def click(x, y, page): advance(dict(kind=0, x=x, y=y), page)
        click(420, 712, {"SSC/Leap": (300, 900)})
        click(320, 912, {"OK": (400, 900)})
        click(420, 912, {"Inn": (400, 700), "intoWorldMap": (300, 600)})
        click(320, 612, {"worldmapflag": (80, 100), "City_RoyalCityLuknalia": (132, 1352)})
        click(152, 1364, {"Inn": (400, 700), "guild": (200, 500)})
        visit, inputs, _ = self.featured_scenario(False)
        frames += visit[1:]
        commands += inputs
        frames[-1]["intoWorldMap"] = (300, 600)
        boundary = len(commands)
        click(320, 612, {"worldmapflag": (80, 100), "SSC/SSC": (400, 700)})
        dung = {"dungFlag": (50, 150)}
        click(420, 712, dung)
        advance(dict(kind=1, x=450, y=1050, x2=450, y2=850, duration=400), dung)
        click(445, 721, {"SSC/trapdeactived": (400, 700)})
        click(1, 1, dung)
        page = {"mapFlag": (100, 100)}
        click(777, 150, page)
        left_up = dict(kind=1, x=100, y=250, x2=700, y2=1200, duration=400)
        for x, y in ((719, 1088), (346, 874)):
            advance(left_up, page)
            click(x, y, page)
            click(136, 1431, dung)
            reached = {**page, "cursor_0": (x - 20, y - 12)}
            click(777, 150, reached)
            advance(left_up, reached)
        for swipe in ((100, 250, 700, 1200), (700, 250, 100, 1200),
                      (700, 1200, 100, 250), (100, 1200, 700, 250)):
            advance(dict(kind=1, x=swipe[0], y=swipe[1], x2=swipe[2], y2=swipe[3], duration=400), page)
        exit_page = {**page, "SSC/SSC_quit": (400, 700)}
        advance(dict(kind=1, x=700, y=1200, x2=100, y2=250, duration=400), exit_page)
        click(420, 712, exit_page)
        click(136, 1431, {"Inn": (400, 700)})
        r = self.execute("golden-route-empty-chests", frames, commands, **self.golden_options())
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], len(commands))
        self.assertFalse(r["mismatch"])
        self.assertEqual([row["inputs"]["backend_called"] for row in r["snapshot"]["sessions"]],
                         [boundary, len(commands) - boundary])
        state = r["snapshot"]["business"]
        self.assertEqual(state["golden_chest"]["completed_cycles"], 1)
        self.assertEqual(state["chests"], 0)
        self.assertEqual(state["task_step"], 6)
        self.assertEqual(state["featured_visit"]["visits_completed"], 1)
        self.assertEqual(state["inn_rests"], 1)

    def test_golden_stop_or_rejected_leap_preserves_intent(self):
        for stop in (False, True):
            r = self.execute("golden-stop-" + str(stop), [{"cursedWheel": (400, 700)}, {"SSC/Leap": (300, 900)}],
                [dict(kind=0, x=420, y=712, reject=not stop)], **self.golden_options(stop_after_first=stop))
            self.assertEqual(r["snapshot"]["state"], "UserStopped" if stop else "Failed", r)
            self.assertEqual(r["backend_calls"], 1)
            self.assertFalse(r["mismatch"])
            self.assertTrue(r["snapshot"]["business"]["golden_chest"]["leap_pending"])
            self.assertEqual(r["snapshot"]["business"]["golden_chest"]["completed_cycles"], 0)

    def test_jier_dialogue_precedes_karma_without_mutating_karma(self):
        r = self.execute("jier-dialogue-karma-priority",
            [{"bounty/cuthimdown": (400, 600), "ambush": (400, 1000)}, {"dungFlag": (50, 150)}],
            [dict(kind=0, x=420, y=612)], workflow="common", jier_dialogue=True,
            extra_images=["bounty/cuthimdown", "bondmate_close"])
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 1)
        self.assertFalse(r["mismatch"])
        self.assertEqual(r["snapshot"]["business"]["special_dialogues_completed"], 1)
        self.assertEqual(r["snapshot"]["business"]["karma_sequence"], 0)

    def test_jier_dialogue_rejected_or_stopped_choice_keeps_pending(self):
        for stop in (False, True):
            r = self.execute("jier-dialogue-stop-" + str(stop), [{"bounty/cuthimdown": (400, 600)}, {"dungFlag": (50, 150)}],
                [dict(kind=0, x=420, y=612, reject=not stop)], workflow="common", jier_dialogue=True,
                extra_images=["bounty/cuthimdown", "bondmate_close"], stop_after_first=stop)
            self.assertEqual(r["snapshot"]["state"], "UserStopped" if stop else "Failed", r)
            self.assertEqual(r["backend_calls"], 1)
            self.assertFalse(r["mismatch"])
            self.assertTrue(r["snapshot"]["business"]["special_dialogue_pending"])
            self.assertEqual(r["snapshot"]["business"]["special_dialogues_completed"], 0)

    def test_jier_dialogue_normal_scene_and_outside_close_roi_not_clicked(self):
        for name, frames, commands, expected in [
            ("normal", [{"Inn": (400, 700), "bounty/cuthimdown": (400, 600)}], [], "Completed"),
            ("close-outside", [{"bounty/cuthimdown": (400, 600)}, {"bondmate_close": (100, 900)}], [dict(kind=0, x=420, y=612)], "Failed")]:
            r = self.execute("jier-dialogue-" + name, frames, commands, workflow="common", jier_dialogue=True,
                extra_images=["bounty/cuthimdown", "bondmate_close"])
            self.assertEqual(r["snapshot"]["state"], expected, r)
            self.assertEqual(r["backend_calls"], len(commands))
            self.assertFalse(r["mismatch"])
            self.assertEqual(r["snapshot"]["business"]["special_dialogues_completed"], 0)

    def test_jier_full_leap_city_two_positions_harken_report_and_rest(self):
        frames = [{"cursedWheelTitle": (200, 100), "requestToRescueTheDuke": (300, 900)}]
        commands = []
        def advance(command, page):
            commands.append(command)
            frames.append(page)
        def click(x, y, page): advance(dict(kind=0, x=x, y=y), page)
        click(320, 912, {"cursedWheelTitle": (200, 100), "leap": (400, 900)})
        click(420, 912, {"Inn": (100, 400), "intoWorldMap": (300, 600)})
        click(320, 612, {"worldmapflag": (80, 100), "City_RoyalCityLuknalia": (132, 1352)})
        click(152, 1364, {"Inn": (400, 700), "guild": (200, 500)})
        click(220, 512, {"guildRequest": (400, 700)})
        click(420, 712, {"Bounties": (300, 800)})
        click(320, 812, {"Bounties": (300, 800)})
        advance(dict(kind=5, key=4), {"EdgeOfTown": (100, 300), "beginningAbyss": (400, 700)})
        click(420, 712, {"B4FLabyrinth": (400, 700)})
        click(420, 712, {"GotoDung": (400, 700)})
        click(420, 712, {"mapFlag": (100, 100)})
        swipe = dict(kind=1, x=100, y=1200, x2=700, y2=250, duration=400)
        for x, y in [(452, 545), (452, 1026)]:
            advance(swipe.copy(), {"mapFlag": (100, 100)})
            click(x, y, {"mapFlag": (100, 100)})
            click(136, 1431, {"dungFlag": (50, 150)})
            reached = {"mapFlag": (100, 100), "cursor_0": (x - 20, y - 12)}
            click(777, 150, reached)
            advance(swipe.copy(), reached)
        gate = {"mapFlag": (100, 100), "harken": (400, 700)}
        advance(dict(kind=1, x=100, y=250, x2=700, y2=1200, duration=400), gate)
        click(420, 712, gate)
        click(136, 1431, {"Inn": (400, 700), "guild": (200, 500)})
        click(220, 512, {"guildRequest": (400, 700)})
        click(420, 712, {"Bounties": (300, 800)})
        click(320, 812, {"CompletionReported": (500, 900)})
        click(520, 912, {"Bounties": (300, 800)})
        advance(dict(kind=5, key=4), {"guildRequest": (400, 700)})
        advance(dict(kind=5, key=4), {"EdgeOfTown": (100, 300), "Inn": (400, 700)})
        for name in ("Stay", "Economy", "OK", "Stay"): click(420, 712, {name: (400, 700)})
        advance(dict(kind=5, key=4), {"Inn": (400, 700)})
        options = self.scorpion_options()
        options["workflow"] = "jier"
        options["extra_images"] += ["requestToRescueTheDuke", "B4FLabyrinth", "bounty/cuthimdown", "bondmate_close"]
        r = self.execute("jier-full", frames, commands, **options)
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 35)
        self.assertFalse(r["mismatch"])
        self.assertEqual(len(r["snapshot"]["sessions"]), 3)
        self.assertEqual(r["snapshot"]["business"]["bounty_reports"], 1)
        self.assertEqual(r["snapshot"]["business"]["bounty_cycle"]["completed_cycles"], 1)
        self.assertEqual(r["snapshot"]["business"]["inn_rests"], 1)

    def scorpion_options(self, **extra):
        profile = self.wall_profile(False)
        profile.update(ACTIVE_BEAUTIFUL_ORE=True)
        profile.update(extra.pop("profile", {}))
        return dict(workflow="scorpion", quest_catalog=str(ROOT / "packs/wvd/parameters/legacy-quests.json"),
            profile=profile, aliases={"returntoTown.png": "returntotown.png"},
            extra_images=["beginningAbyss", "B2FTemple", "B5FWarpedOnesNest", "guild", "guildRequest",
                "guildFeatured", "Bounties", "CompletionReported", "leaveDung", "cursedWheelTitle", "cursedWheel",
                "cursedWheelTapRight", "leap", "ruins", "BeautifulOre", "Triumph", "GhostsOfYore",
                "cursedwheel_dhi", "cursedwheel_impregnableFortress"], **extra)

    @staticmethod
    def scorpion_scenario(hands=False):
        frames = [{"cursedWheelTitle": (200, 100), "BeautifulOre": (300, 900)}]
        commands = []
        def advance(command, frame):
            commands.append(command)
            frames.append(frame)
        def click(x, y, frame):
            advance(dict(kind=0, x=x, y=y), frame)
        click(320, 912, {"cursedWheelTitle": (200, 100), "leap": (400, 900)})
        click(420, 912, {"Inn": (400, 700), "guild": (200, 500)})
        click(220, 512, {"guildRequest": (400, 700)})
        click(420, 712, {"Bounties": (300, 800)})
        click(320, 812, {"Bounties": (300, 800)})
        entry = {"EdgeOfTown": (100, 300), "beginningAbyss": (400, 700)}
        advance(dict(kind=5, key=4), entry)
        boundaries = [len(commands)]
        for route in range(2 if hands else 1):
            click(420, 712, {"B5FWarpedOnesNest" if route else "B2FTemple": (400, 700)})
            click(420, 712, {"GotoDung": (400, 700)})
            click(420, 712, {"mapFlag": (100, 100)})
            points = [(454, 662, (100, 250, 700, 1200)), (135, 714, (100, 250, 700, 1200))] if route else [
                (505, 760, (100, 1200, 700, 250)), (506, 821, (100, 250, 700, 1200))]
            for x, y, swipe in points:
                move = dict(kind=1, x=swipe[0], y=swipe[1], x2=swipe[2], y2=swipe[3], duration=400)
                advance(move, {"mapFlag": (100, 100)})
                click(x, y, {"mapFlag": (100, 100)})
                click(136, 1431, {"dungFlag": (50, 150)})
                reached = {"mapFlag": (100, 100), "cursor_0": (x - 20, y - 12)}
                click(777, 150, reached)
                advance(move, reached)
            city = {"guild": (200, 500)}
            if hands and not route: city.update(entry)
            advance(dict(kind=5, key=4), city)
            boundaries.append(len(commands))
        for report in range(2 if hands else 1):
            click(220, 512, {"guildRequest": (400, 700)})
            click(420, 712, {"Bounties": (300, 800)})
            click(320, 812, {"CompletionReported": (500, 900)})
            click(520, 912, {"Bounties": (300, 800)})
            advance(dict(kind=5, key=4), {"guildRequest": (400, 700)})
            city = {"EdgeOfTown": (100, 300), "Inn": (400, 700)}
            if hands and not report: city["guild"] = (200, 500)
            advance(dict(kind=5, key=4), city)
        for name in ("Stay", "Economy", "OK", "Stay"):
            click(420, 712, {name: (400, 700)})
        advance(dict(kind=5, key=4), {"Inn": (400, 700)})
        boundaries.append(len(commands))
        return frames, commands, boundaries

    def test_scorpion_complete_routes_deliver_each_bounty_and_rest(self):
        for hands in (False, True):
            frames, commands, boundaries = self.scorpion_scenario(hands)
            r = self.execute("scorpion-full-" + str(hands), frames, commands, **self.scorpion_options(hands=hands))
            self.assertEqual(r["snapshot"]["state"], "Completed", r)
            self.assertEqual(r["backend_calls"], len(commands))
            self.assertFalse(r["mismatch"])
            state = r["snapshot"]["business"]
            self.assertEqual(state["bounty_cycle"]["completed_cycles"], 1)
            self.assertEqual(state["bounty_reports"], 2 if hands else 1)
            self.assertEqual(state["inn_rests"], 1)
            self.assertEqual(state["dungeons"], 1)
            self.assertEqual(len(r["snapshot"]["sessions"]), len(boundaries))

    def test_scorpion_leap_preference_and_stop_never_deliver(self):
        for ore, triumph, target in [(True, True, "BeautifulOre"), (False, True, "Triumph"), (False, False, "GhostsOfYore")]:
            first = {"cursedWheelTitle": (200, 100), target: (300, 900)}
            r = self.execute("scorpion-start-" + target, [first, {"cursedWheelTitle": (200, 100), "leap": (400, 900)}],
                [dict(kind=0, x=320, y=912)], **self.scorpion_options(profile={"ACTIVE_BEAUTIFUL_ORE": ore,
                    "ACTIVE_TRIUMPH": triumph}, stop_after_first=True))
            self.assertEqual(r["snapshot"]["state"], "UserStopped", r)
            self.assertEqual(r["backend_calls"], 1)
            self.assertTrue(r["snapshot"]["business"]["bounty_cycle"]["transfer_pending"])
            self.assertEqual(r["snapshot"]["business"]["bounty_reports"], 0)

    def test_scorpion_unknown_entry_never_fakes_a_cycle(self):
        r = self.execute("scorpion-unknown", [{}], [], **self.scorpion_options())
        self.assertEqual(r["snapshot"]["state"], "Interrupted", r)
        self.assertEqual(r["backend_calls"], 0)
        self.assertEqual(r["snapshot"]["business"]["bounty_cycle"]["completed_cycles"], 0)

    @staticmethod
    def bounty_report_scenario():
        frames = [{"guild": (200, 500)}, {"guildRequest": (400, 700)}, {"Bounties": (300, 800)},
                  {"CompletionReported": (500, 900)}, {"Bounties": (300, 800)},
                  {"guildRequest": (400, 700)}, {"EdgeOfTown": (400, 700)}]
        actions = [dict(kind=0, x=x, y=y) for x, y in [(220, 512), (420, 712), (320, 812), (520, 912)]]
        actions += [dict(kind=5, key=4), dict(kind=5, key=4)]
        return frames, actions

    def test_bounty_reveal_is_not_a_report(self):
        frames, actions = self.bounty_report_scenario()
        r = self.execute("bounty-reveal", frames[:3] + [frames[4], frames[-1]], actions[:3] + [dict(kind=5, key=4)],
            **self.bounty_options())
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 4)
        self.assertFalse(r["mismatch"])
        self.assertEqual(r["snapshot"]["business"]["bounty_reports"], 0)
        self.assertEqual(r["snapshot"]["business"]["bounty_reveals"], 1)

    def test_bounty_report_requires_exit_and_two_visits_have_two_receipts(self):
        frames, actions = self.bounty_report_scenario()
        for twice in (False, True):
            screens, commands = [dict(frame) for frame in frames], list(actions)
            if twice:
                screens[-1].update(frames[0])
                screens += frames[1:]
                commands += actions
            r = self.execute("bounty-report-" + str(twice), screens, commands,
                **self.bounty_options(report=True, normal_units=2 if twice else 1))
            self.assertEqual(r["snapshot"]["state"], "Completed", r)
            self.assertEqual(r["backend_calls"], len(commands))
            self.assertFalse(r["mismatch"])
            self.assertEqual(r["snapshot"]["business"]["bounty_reports"], 2 if twice else 1)
            self.assertFalse(r["snapshot"]["business"]["bounty_report_pending"])

    def test_bounty_menu_scroll_uses_confirmed_menu(self):
        frames = [{"guildFeatured": (200, 500)}, {"CompletionReported": (500, 900)}, {"EdgeOfTown": (400, 700)}]
        actions = [dict(kind=1, x=600, y=1400, x2=300, y2=1400, duration=400), dict(kind=0, x=520, y=912)]
        r = self.execute("bounty-scroll", frames, actions, **self.bounty_options(report=True))
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 2)
        self.assertFalse(r["mismatch"])
        self.assertEqual(r["snapshot"]["business"]["bounty_reports"], 1)

    def test_bounty_unchanged_report_or_stop_never_claims_reward(self):
        report = {"CompletionReported": (500, 900)}
        for stop in (False, True):
            r = self.execute("bounty-pending-" + str(stop), [report, report], [dict(kind=0, x=520, y=912)],
                **self.bounty_options(report=True, stop_after_first=stop))
            self.assertEqual(r["snapshot"]["state"], "UserStopped" if stop else "Failed", r)
            self.assertEqual(r["backend_calls"], 1)
            self.assertFalse(r["mismatch"])
            self.assertEqual(r["snapshot"]["business"]["bounty_reports"], 0)
            self.assertTrue(r["snapshot"]["business"]["bounty_report_pending"])

    def test_bounty_unknown_page_never_swipes(self):
        r = self.execute("bounty-unknown", [{}], [], **self.bounty_options(report=True))
        self.assertEqual(r["snapshot"]["state"], "Interrupted", r)
        self.assertEqual(r["backend_calls"], 0)
        self.assertEqual(r["snapshot"]["business"]["bounty_reports"], 0)

    def manual_options(self, **extra):
        return dict(workflow="manual-separation", quest_catalog=str(ROOT / "packs/wvd/parameters/legacy-quests.json"),
            profile=self.wall_profile(False), large_templates=["harken"],
            aliases={"returntoTown.png": "returntotown.png"},
            extra_images=["stair_2", "stair_3", "COS/COS", "COS/COSB2F", "leaveDung", "cursedWheelTitle",
                "cursedWheel", "cursedWheelTapRight", "leap", "ruins", "BeautifulOre", "cursedwheel_dhi"], **extra)

    @staticmethod
    def manual_scenario():
        frames, actions = [{"mapFlag": (100, 100)}], []
        def advance(action, frame):
            actions.append(action)
            frames.append(frame)
        def point(name, x, y, swipe):
            advance(dict(kind=1, x=swipe[0], y=swipe[1], x2=swipe[2], y2=swipe[3], duration=400), {"mapFlag": (100, 100)})
            advance(dict(kind=0, x=x, y=y), {"mapFlag": (100, 100)})
            advance(dict(kind=0, x=136, y=1431), {"dungFlag": (50, 150)})
            reached = {"mapFlag": (100, 100), "cursor_0": (x - 20, y - 12)}
            if name != "position": reached[name] = (400, 600)
            advance(dict(kind=0, x=777, y=150), reached)
            advance(dict(kind=1, x=swipe[0], y=swipe[1], x2=swipe[2], y2=swipe[3], duration=400), reached)
        point("stair_2", 827, 547, (100, 1200, 700, 250))
        advance(dict(kind=1, x=100, y=1200, x2=700, y2=250, duration=400), {"mapFlag": (100, 100), "harken": (400, 700)})
        advance(dict(kind=0, x=440, y=740), {"mapFlag": (100, 100), "harken": (400, 700)})
        advance(dict(kind=0, x=136, y=1431), {"returnText": (400, 700)})
        advance(dict(kind=5, key=4), {"EdgeOfTown": (400, 700)})
        advance(dict(kind=5, key=4), {"Inn": (400, 700)})
        for label in ("Stay", "Economy", "OK", "Stay"):
            advance(dict(kind=0, x=420, y=712), {label: (400, 700)})
        advance(dict(kind=5, key=4), {"Inn": (400, 700), "cursedWheelTitle": (200, 100), "BeautifulOre": (300, 900)})
        advance(dict(kind=0, x=320, y=912), {"cursedWheelTitle": (200, 100), "leap": (400, 900)})
        advance(dict(kind=0, x=420, y=912), {"EdgeOfTown": (400, 600), "COS/COS": (300, 700)})
        first_unit_inputs = len(actions)
        advance(dict(kind=0, x=320, y=712), {"COS/COSB2F": (400, 700)})
        advance(dict(kind=0, x=420, y=712), {"dungFlag": (50, 150)})
        advance(dict(kind=0, x=777, y=150), {"mapFlag": (100, 100)})
        point("stair_3", 720, 822, (100, 250, 700, 1200))
        point("position", 79, 447, (100, 250, 700, 1200))
        return frames, actions, first_unit_inputs

    def test_manual_separation_full_two_units(self):
        frames, actions, first = self.manual_scenario()
        r = self.execute("manual-full", frames, actions, **self.manual_options())
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], len(actions))
        self.assertFalse(r["mismatch"])
        state = r["snapshot"]["business"]
        self.assertTrue(state["manual_separation"]["completed"])
        self.assertEqual(state["inn_rests"], 1)
        self.assertEqual(state["dungeons"], 0)
        sessions = r["snapshot"]["sessions"]
        self.assertEqual(len(sessions), 2)
        self.assertEqual([session["engine_status"] for session in sessions], [3000, 3000])
        self.assertTrue(all(session["quiescent"] for session in sessions))
        self.assertFalse(sessions[0]["business"]["manual_separation"]["completed"])
        self.assertTrue(sessions[1]["business"]["manual_separation"]["completed"])
        self.assertEqual(first, 17)
        self.assertEqual([row["target"] for row in r["task_plan"][0]["route"]], ["stair_2", "harken"])
        self.assertEqual([row["target"] for row in r["task_plan"][1]["route"]], ["stair_3", "position"])

    def test_manual_separation_stop_and_rejection_preserve_incomplete_route(self):
        frames, actions, _ = self.manual_scenario()
        for stop in (False, True):
            r = self.execute("manual-stop-" + str(stop), frames[:2], [{**actions[0], "reject": not stop}],
                **self.manual_options(stop_after_first=stop))
            self.assertEqual(r["snapshot"]["state"], "UserStopped" if stop else "Failed", r)
            self.assertEqual(r["backend_calls"], 1)
            self.assertFalse(r["mismatch"])
            self.assertFalse(r["snapshot"]["business"]["manual_separation"]["completed"])

    def test_manual_separation_premature_exit_is_not_harken_completion(self):
        frames, actions, _ = self.manual_scenario()
        r = self.execute("manual-premature", frames[:3] + [{"Inn": (400, 700)}], actions[:3], **self.manual_options())
        self.assertEqual(r["snapshot"]["state"], "Interrupted", r)
        self.assertEqual(r["backend_calls"], 3)
        self.assertFalse(r["mismatch"])
        self.assertEqual(r["snapshot"]["sessions"][0]["reason"], "quest.manual_route_incomplete")
        self.assertEqual(r["snapshot"]["business"]["inn_rests"], 0)

    def test_time_leap_visible_target_uses_fast_path(self):
        r = self.execute("time-leap-fast", [{"cursedWheelTitle": (200, 100), "BeautifulOre": (300, 700)},
            {"leap": (400, 700)}, {"Inn": (400, 700)}],
            [dict(kind=0, x=320, y=712), dict(kind=0, x=420, y=712)],
            workflow="time-leap", leap_target="BeautifulOre", leap_chapter="cursedwheel_dhi")
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 2)
        self.assertFalse(r["mismatch"])

    def test_time_leap_reset_chapter_and_ordered_scrolling(self):
        title = {"cursedWheelTitle": (200, 100)}
        chapter = {**title, "cursedwheel_dhi": (300, 700)}
        frames = [title] + [title] * 9 + [chapter, title] + [title] * 3
        actions = [dict(kind=0, x=105, y=230)] * 10 + [dict(kind=0, x=320, y=712)]
        actions += [dict(kind=1, x=450, y=1200, x2=450, y2=200, duration=400)] * 3
        frames += [{**title, "BeautifulOre": (300, 700)}, {"leap": (400, 700)}, {"dungFlag": (50, 150)}]
        actions += [dict(kind=1, x=50, y=1200, x2=50, y2=1300, duration=400),
                    dict(kind=0, x=320, y=712), dict(kind=0, x=420, y=712)]
        r = self.execute("time-leap-scroll", frames, actions, workflow="time-leap", leap_target="BeautifulOre",
                         leap_chapter="cursedwheel_dhi")
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 17)
        self.assertFalse(r["mismatch"])

    def test_time_leap_stop_and_rejection_never_complete(self):
        for stop in (False, True):
            r = self.execute("time-leap-stop-" + str(stop),
                [{"cursedWheelTitle": (200, 100), "BeautifulOre": (300, 700)}, {"leap": (400, 700)}],
                [dict(kind=0, x=320, y=712, reject=not stop)], workflow="time-leap", leap_target="BeautifulOre",
                leap_chapter="cursedwheel_dhi", stop_after_first=stop)
            self.assertEqual(r["snapshot"]["state"], "UserStopped" if stop else "Failed", r)
            self.assertEqual(r["backend_calls"], 1)
            self.assertFalse(r["mismatch"])

    def test_time_leap_unknown_after_click_and_unrelated_city_do_not_complete(self):
        r = self.execute("time-leap-unknown-post", [{"cursedWheelTitle": (200, 100), "BeautifulOre": (300, 700)},
            {"leap": (400, 700)}, {}], [dict(kind=0, x=320, y=712), dict(kind=0, x=420, y=712)],
            workflow="time-leap", leap_target="BeautifulOre", leap_chapter="cursedwheel_dhi")
        self.assertEqual(r["snapshot"]["state"], "Failed", r)
        self.assertIn("POSTCONDITION_TIMEOUT", str(r["snapshot"]))
        self.assertEqual(r["backend_calls"], 2)
        self.assertFalse(r["mismatch"])
        r = self.execute("time-leap-unrelated-city", [{"Inn": (400, 700)}], [],
            workflow="time-leap", leap_target="BeautifulOre", leap_chapter="cursedwheel_dhi")
        self.assertEqual(r["snapshot"]["state"], "Interrupted", r)
        self.assertEqual(r["backend_calls"], 0)

    def test_time_leap_download_respects_frozen_permission(self):
        frames = [{"startdownload": (300, 920)}, {"cursedWheelTitle": (200, 100), "BeautifulOre": (300, 700)},
                  {"leap": (400, 700)}, {"Inn": (400, 700)}]
        for allowed in (False, True):
            actions = [dict(kind=0, x=320, y=932), dict(kind=0, x=320, y=712), dict(kind=0, x=420, y=712)] if allowed else []
            r = self.execute("time-leap-download-" + str(allowed), frames, actions,
                workflow="time-leap", leap_target="BeautifulOre", leap_chapter="cursedwheel_dhi", allow_download=allowed)
            self.assertEqual(r["snapshot"]["state"], "Completed" if allowed else "Interrupted", r)
            self.assertEqual(r["backend_calls"], len(actions))
            self.assertFalse(r["mismatch"])
            if not allowed:
                self.assertEqual(r["snapshot"]["sessions"][0]["reason"], "boot.download_permission_missing")

    def test_mining_eot_and_mark_navigation_reach_the_verified_site(self):
        frames, actions = self.mining_scenario()
        # 独立沿用旧EOT事件顺序：前置GCN、关闭覆盖层、EVENT、活动、ZONE2。
        # 地图标记输入之后才出现寻路结束提示；截图次数不能使场景前进。
        entry = [{"FFXI/GCN": (400, 700)}, {"EVENT": (200, 500)}, {"EVENT": (200, 500)},
                 {"FFXI/EVENT_GCN": (300, 700)}, {"openworldmap": (300, 100), "FFXI/ZONE2": (400, 700)},
                 {"dungFlag": (50, 150), "mark_auto": (740, 300)}]
        frames[0]["theRouteToTheDestinationCannotBeFound"] = (300, 700)
        inputs = [dict(kind=0, x=x, y=y) for x, y in
                  [(420, 712), (1, 1), (220, 512), (320, 712), (420, 712), (760, 312)]]
        r = self.execute("mining-eot", entry + frames, inputs + actions, **self.mining_options())
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 11)
        self.assertFalse(r["mismatch"])
        self.assertEqual(r["snapshot"]["business"]["mining"]["rewards"]["fine"], 1)
        self.assertEqual(r["snapshot"]["business"]["mining"]["completed_cycles"], 1)

    def test_mining_continuation_starts_a_new_cycle_without_reusing_reward(self):
        first, actions = self.mining_scenario()
        second, later = self.mining_scenario()
        first[-1]["openworldmap"] = (300, 100)
        first[-1]["FFXI/GCN"] = (400, 700)
        entry = [{"EVENT": (200, 500)}, {"EVENT": (200, 500)},
                 {"FFXI/EVENT_GCN": (300, 700)}, {"openworldmap": (300, 100), "FFXI/ZONE2": (400, 700)},
                 {"dungFlag": (50, 150), "mark_auto": (740, 300)}]
        second[0]["theRouteToTheDestinationCannotBeFound"] = (300, 700)
        inputs = [dict(kind=0, x=x, y=y) for x, y in
                  [(420, 712), (1, 1), (220, 512), (320, 712), (420, 712), (760, 312)]]
        r = self.execute("mining-two-cycles", first + entry + second, actions + inputs + later,
            **self.mining_options(normal_units=2))
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 16)
        self.assertFalse(r["mismatch"])
        self.assertEqual(len(r["snapshot"]["sessions"]), 2)
        state = r["snapshot"]["business"]
        self.assertEqual(state["mining"]["rewards"]["fine"], 2)
        self.assertEqual(state["mining"]["completed_cycles"], 2)
        self.assertEqual(state["dungeons"], 0)

    def test_mining_mid_run_unknown_recovers_without_fabricating_reward(self):
        frames, actions = self.mining_scenario()
        # 第一镐只进入未知页。生命周期端口显式重建现场后，真正挖到的下一份矿才计数。
        r = self.execute("mining-mid-recovery", [frames[0], {}] + frames, [actions[0]] + actions,
            **self.mining_options(attach_recovery=True, restart_frame=2, restart_action=1))
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 6)
        self.assertFalse(r["mismatch"])
        state = r["snapshot"]["business"]
        self.assertEqual(state["crashes"], 1)
        self.assertEqual(state["mining"]["rewards"]["fine"], 1)
        self.assertEqual(state["mining"]["completed_cycles"], 1)
        self.assertEqual(state["dungeons"], 0)
        self.assertEqual(len(r["snapshot"]["sessions"]), 2)

    def test_mining_mid_run_pause_freeze_recovers_to_a_new_dig(self):
        frames, actions = self.mining_scenario()
        commands = [actions[0]] + [dict(kind=0, x=450, y=760)] * 6 + actions
        r = self.execute("mining-pause-recovery", [frames[0]] + [{} for _ in range(7)] + frames, commands,
            **self.mining_options(attach_recovery=True, restart_frame=8, restart_action=7, pause_frames=list(range(1, 8))))
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 12)
        self.assertFalse(r["mismatch"])
        state = r["snapshot"]["business"]
        self.assertEqual(state["crashes"], 1)
        self.assertEqual(state["mining"]["rewards"]["fine"], 1)
        self.assertEqual(state["mining"]["completed_cycles"], 1)
        self.assertEqual(r["snapshot"]["sessions"][0]["reason"], "pause.physics_frozen")
        self.assertEqual(len(r["snapshot"]["sessions"]), 2)

    def test_mining_wrong_mark_position_never_digs(self):
        r = self.execute("mining-wrong-position", [{"dungFlag": (50, 150),
            "theRouteToTheDestinationCannotBeFound": (300, 700), "FFXI/org_position": (100, 400)}], [],
            **self.mining_options())
        self.assertEqual(r["snapshot"]["state"], "Interrupted", r)
        self.assertEqual(r["snapshot"]["sessions"][0]["reason"], "quest.mining_position_not_confirmed")
        self.assertEqual(r["backend_calls"], 0)
        self.assertFalse(r["mismatch"])
        self.assertEqual(sum(r["snapshot"]["business"]["mining"]["rewards"].values()), 0)

    def test_mining_all_reward_classes_use_published_mod_and_roi(self):
        names = ("fine", "high", "mid", "low", "refine", "alter", "sliver", "ouro", "lesser_full", "full")
        original, _ = self.mining_scenario()
        site = original[0]
        frames, commands = [site], []
        for i, name in enumerate(names):
            reward_image = "alternate_fine" if name == "fine" else "FFXI/org_" + name
            frames += [{**site, "FFXI/receive": (330, 700), reward_image: (480, 780)}, site]
            commands += [dict(kind=0, x=450, y=600)] * 2
        # 第十一页仅有ROI外的已知矿物；仍应归为unknown，不受界面其它区域干扰。
        frames += [{**site, "FFXI/receive": (330, 700), "FFXI/org_high": (480, 200)}] + original[2:]
        commands += [dict(kind=0, x=450, y=600)] * 2 + [dict(kind=0, x=1, y=1)] + [dict(kind=0, x=420, y=712)] * 2
        options = self.mining_options(mod_images={"FFXI/org_fine": "alternate_fine"}, mod_only_images=["FFXI/org_fine"])
        options["extra_images"] += ["alternate_fine"]
        r = self.execute("mining-all-rewards", frames, commands, **options)
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 25)
        self.assertFalse(r["mismatch"])
        mining = r["snapshot"]["business"]["mining"]
        self.assertEqual(mining["rewards"], {name: 1 for name in (*names, "unknown")})
        self.assertEqual(mining["reward_sequence"], 11)
        self.assertEqual(mining["completed_cycles"], 1)
        self.assertEqual(r["image_sources"]["images"]["FFXI/org_fine.png"]["source"], "mod")

    def dark_scenario(self, heal=False):
        dungeon = {"dungFlag": (50, 150), "darklight": (400, 700)}
        light = {"darklight_lightIt": (400, 700)}
        frames = [dungeon, dungeon, light, self.turn_screen(), dungeon, dungeon]
        commands = [dict(kind=0, x=x, y=y) for x, y in [(1, 1), (420, 712), (420, 712), (513, 1200), (1, 1)]]
        if heal:
            frames += [{"trait": (200, 300)}, {"recover": (250, 850)}, {"trait": (200, 300)}, dungeon]
            commands += [dict(kind=0, x=36, y=1425), dict(kind=0, x=830, y=850),
                         dict(kind=0, x=600, y=1200), dict(kind=1, key=4)]
        frames += [light, {"Inn": (400, 700)}]
        commands += [dict(kind=0, x=420, y=712)] * 2
        return frames, commands

    def test_dark_light_cycle_and_cold_start(self):
        frames, commands = self.dark_scenario()
        for cold in (False, True):
            r = self.execute("dark-light-cycle-" + str(cold), ([{}] if cold else []) + frames, commands,
                **self.dark_options(attach_recovery=cold, initial_connection="closed" if cold else "ready"))
            self.assertEqual(r["snapshot"]["state"], "Completed", r)
            self.assertEqual(r["backend_calls"], 7)
            self.assertFalse(r["mismatch"])
            self.assertEqual(r["snapshot"]["generation"], 2 if cold else 1)
            self.assertEqual(r["lifecycle_calls"], ["EnsureVpn", "StopApplication", "StartApplication"] if cold else [])
            for field, expected in {"dungeons": 0, "combats": 1, "chests": 0, "inn_rests": 0,
                                    "dark_light_active": False, "task_step": 0, "need_initial_recover": False}.items():
                self.assertEqual(r["snapshot"]["business"][field], expected, field)
            self.assertTrue(r["snapshot"]["quiescent"])

    def test_dark_light_heals_after_combat_not_on_initial_entry(self):
        frames, commands = self.dark_scenario(heal=True)
        r = self.execute("dark-light-heal", frames, commands, **self.dark_options(
            profile={**self.wall_profile(False), "RECOVER_WHEN_BEGINNING": True, "SKIP_COMBAT_RECOVER": False}))
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 11)
        self.assertFalse(r["mismatch"])
        self.assertFalse(r["snapshot"]["business"]["healing_required"])
        self.assertEqual(r["snapshot"]["business"]["dungeons"], 0)

    def test_dark_light_stop_and_rejection_never_complete(self):
        frames, commands = self.dark_scenario()
        for stopped in (False, True):
            r = self.execute("dark-light-stop-" + str(stopped), frames[:2],
                [{**commands[0], "reject": not stopped}], **self.dark_options(stop_after_first=stopped))
            self.assertEqual(r["snapshot"]["state"], "UserStopped" if stopped else "Failed", r)
            self.assertEqual(r["backend_calls"], 1)
            self.assertFalse(r["mismatch"])
            self.assertTrue(r["snapshot"]["business"]["dark_light_active"])
            self.assertEqual(r["snapshot"]["business"]["dungeons"], 0)

    def test_dark_light_unchanged_confirmation_is_not_replayed(self):
        frames, commands = self.dark_scenario()
        r = self.execute("dark-light-unchanged", frames[:3] + [frames[2]], commands[:3],
            **self.dark_options(attach_recovery=True))
        self.assertEqual(r["snapshot"]["state"], "Failed", r)
        self.assertIn("POSTCONDITION_TIMEOUT", str(r["snapshot"]))
        self.assertEqual(r["backend_calls"], 3)
        self.assertEqual(r["lifecycle_calls"], [])
        self.assertFalse(r["mismatch"])

    def test_dark_light_already_in_city_does_not_start_dungeon(self):
        r = self.execute("dark-light-city", [{"Inn": (400, 700)}], [], **self.dark_options())
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 0)
        self.assertEqual(r["snapshot"]["business"]["dungeons"], 0)
        self.assertFalse(r["snapshot"]["business"]["dark_light_active"])

    def test_unknown_window_restart_enters_new_generation_before_completion(self):
        r = self.execute("unknown-window-recovery", [{}, {"mapFlag": (100, 100), "cursor_0": (480, 588)}], [],
            workflow="dungeon-route", profile=self.wall_profile(False),
            route_targets=[["position", [None], [500, 600]]], attach_recovery=True)
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 0)
        self.assertEqual(r["lifecycle_calls"], ["EnsureVpn", "StopApplication", "StartApplication"])
        self.assertEqual(r["snapshot"]["generation"], 2)
        self.assertEqual(r["snapshot"]["sessions"][0]["reason"], "dungeon.unknown_static_window")
        self.assertTrue(all(s["quiescent"] for s in r["snapshot"]["sessions"]))
        self.assertEqual(r["snapshot"]["business"]["task_step"], 1)

    def test_unknown_window_known_city_is_not_frozen(self):
        r = self.execute("unknown-window-known", [{"Inn": (400, 700)}], [], workflow="dungeon-route",
            profile=self.wall_profile(False), route_targets=[["position", [None], [500, 600]]])
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 0)
        self.assertEqual(r["lifecycle_calls"], [])
        self.assertEqual(r["snapshot"]["business"]["task_step"], 0)

    def test_unknown_limit_requests_recovery_before_static_window(self):
        for limit in (0, 2):
            r = self.execute("unknown-limit-" + str(limit), [{}], [], workflow="dungeon-route",
                profile={**self.wall_profile(False), "MAX_TRY_LIMIT": limit},
                route_targets=[["position", [None], [500, 600]]])
            self.assertEqual(r["snapshot"]["state"], "Interrupted", r)
            self.assertEqual(r["snapshot"]["sessions"][-1]["reason"], "dungeon.unknown_try_limit")
            self.assertEqual(r["backend_calls"], 0)
            self.assertEqual(r["lifecycle_calls"], [])
            self.assertEqual(r["snapshot"]["business"]["task_step"], 0)
            self.assertTrue(r["snapshot"]["quiescent"])

    def test_unknown_limit_does_not_override_known_scene_even_at_zero(self):
        r = self.execute("unknown-limit-known", [{"Inn": (400, 700)}], [], workflow="dungeon-route",
            profile={**self.wall_profile(False), "MAX_TRY_LIMIT": 0},
            route_targets=[["position", [None], [500, 600]]])
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 0)
        self.assertEqual(r["lifecycle_calls"], [])

    def test_auto_map_stopped_mark_is_confirmed_on_map_without_repressing_auto(self):
        r = self.execute("auto-map-mark", [{"dungFlag": (50, 150), "mark_auto": (760, 350)},
            {"dungFlag": (50, 150)}, {"mapFlag": (100, 100), "mark_auto": (400, 700)}],
            [dict(kind=0, x=800, y=390), dict(kind=0, x=777, y=150)], workflow="dungeon-route",
            profile=self.wall_profile(False), route_targets=[["mark_auto", [None]]],
            large_templates=["mark_auto"], focused_map_templates={"2": ["mark_auto"]})
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 2)
        self.assertFalse(r["mismatch"])
        self.assertEqual(r["snapshot"]["business"]["task_step"], 1)

    def test_auto_map_disabled_chest_uses_map_confirmation(self):
        r = self.execute("auto-map-chest", [{"dungFlag": (50, 150), "chest_auto": (760, 350)},
            {"dungFlag": (50, 150)}, {"mapFlag": (100, 100), "chest_auto": (400, 700)}],
            [dict(kind=0, x=800, y=390), dict(kind=0, x=777, y=150)], workflow="dungeon-route",
            profile=self.wall_profile(False), route_targets=[["chest_auto", [None]]],
            large_templates=["chest_auto"], focused_map_templates={"2": ["chest_auto"]})
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 2)
        self.assertFalse(r["mismatch"])
        self.assertEqual(r["snapshot"]["business"]["task_step"], 1)

    def test_auto_map_stop_or_failed_map_open_never_advances_target(self):
        for stop in (False, True):
            r = self.execute("auto-map-stop-" + str(stop), [{"dungFlag": (50, 150), "mark_auto": (760, 350)},
                {"dungFlag": (50, 150)}, {"mapFlag": (100, 100)}],
                [dict(kind=0, x=800, y=390), dict(kind=0, x=777, y=150, reject=True)],
                workflow="dungeon-route", profile=self.wall_profile(False), route_targets=[["mark_auto", [None]]],
                large_templates=["mark_auto"], stop_after_first=stop)
            self.assertEqual(r["snapshot"]["state"], "UserStopped" if stop else "Failed", r)
            self.assertEqual(r["backend_calls"], 1 if stop else 2)
            self.assertEqual(r["snapshot"]["business"]["task_step"], 0)
            self.assertFalse(r["mismatch"])

    @staticmethod
    def giant_scenario(rest=True):
        # 参数来自固定旧 case，而场景变化只由预期输入触发，不靠截图次数推进。
        source = subprocess.check_output(["git", "show", "6585f4075f5714ab522aa582993860c09af912c1:src/script.py"], cwd=ROOT.parent)
        tree = ast.parse(source.decode("utf-8"))
        branch = next(node for node in ast.walk(tree) if isinstance(node, ast.match_case)
            and isinstance(node.pattern, ast.MatchValue) and isinstance(node.pattern.value, ast.Constant)
            and node.pattern.value.value == "gaintKiller")
        assignment = next(node for statement in branch.body for node in ast.walk(statement)
            if isinstance(node, ast.Assign) and any(isinstance(target, ast.Attribute) and target.attr == "_EOT" for target in node.targets))
        entries = ast.literal_eval(assignment.value)
        city = {"Inn": (400, 700), "EdgeOfTown": (300, 1000)}
        frames, actions, focused = [city], [], {}
        def advance(command, frame):
            actions.append(command)
            frames.append(dict(frame))
        def click(x, y, frame):
            advance(dict(kind=0, x=x, y=y), frame)
        click(320, 1012, {entries[0][1]: (300, 700)})
        click(1, 1, {entries[0][1]: (300, 700)})
        click(320, 712, {entries[1][1]: (300, 700)})
        click(320, 712, {"GotoDung": (300, 700)})
        click(320, 712, {"mapFlag": (100, 100)})
        swipe = dict(kind=1, x=100, y=250, x2=700, y2=1200, duration=400)
        advance(swipe, {"mapFlag": (100, 100)})
        click(560, 982, {"mapFlag": (100, 100)})
        click(136, 1431, {"dungFlag": (50, 150)})
        reached = {"mapFlag": (100, 100), "cursor_0": (540, 970), "harken2": (400, 700)}
        click(777, 150, reached)
        advance(swipe, reached)
        advance(swipe, reached)
        click(440, 740, {"mapFlag": (100, 100), "harken2": (400, 700)})
        click(136, 1431, {"dungFlag": (50, 150)})
        exit_map = {"mapFlag": (100, 100), "harken2": (400, 700), "leaveDung": (600, 900)}
        click(777, 150, exit_map)
        focused[str(len(frames) - 1)] = ["harken2"]
        advance(swipe, exit_map)
        focused[str(len(frames) - 1)] = ["harken2"]
        click(620, 912, {"returnText": (300, 700)})
        click(320, 712, city)
        if rest:
            click(420, 712, {"Stay": (300, 700)})
            click(320, 712, {"Economy": (300, 700)})
            click(320, 712, {"OK": (300, 700)})
            click(320, 712, {"Stay": (300, 700)})
            advance(dict(kind=5, key=4), city)
        return entries, frames, actions, focused

    def giant_options(self, **extra):
        return dict(workflow="giant", quest_catalog=str(ROOT / "packs/wvd/parameters/legacy-quests.json"),
            profile={**self.wall_profile(False), "REST_INTERVEL": 1, "ACTIVE_REST": False},
            extra_images=["impregnableFortress", "fortressb7f", "harken2", "returntotown", "leaveDung"],
            aliases={"returntoTown.png": "returntotown.png"},
            large_templates=["harken2"], **extra)

    def test_giant_full_cycle_and_cold_start(self):
        entries, frames, actions, focused = self.giant_scenario()
        for cold in (False, True):
            result = self.execute("giant-cycle-" + str(cold), ([{}] if cold else []) + frames, actions,
                **self.giant_options(attach_recovery=cold, initial_connection="closed" if cold else "ready",
                    focused_map_templates={str(int(k) + int(cold)): v for k, v in focused.items()}))
            self.assertEqual(result["snapshot"]["state"], "Completed", result)
            self.assertEqual(result["backend_calls"], len(actions))
            self.assertFalse(result["mismatch"])
            self.assertEqual([row["target"] for row in result["task_plan"]["entry_steps"]], [row[1] for row in entries])
            self.assertNotIn("_EOT", result["task_plan"]["source"])
            state = result["snapshot"]["business"]
            self.assertEqual(state["dungeons"], 1)
            self.assertEqual(state["giant_cycles_completed"], 1)
            self.assertEqual(state["inn_rests"], 1)
            self.assertEqual(state["crashes"], int(cold))

    def test_giant_second_cycle_uses_special_rest_interval(self):
        _, first, actions, focused = self.giant_scenario()
        _, second, more, second_focus = self.giant_scenario(rest=False)
        offset = len(first) - 1
        focused.update({str(int(k) + offset): v for k, v in second_focus.items()})
        result = self.execute("giant-two-cycles", first + second[1:], actions + more,
            **self.giant_options(normal_units=2, focused_map_templates=focused))
        self.assertEqual(result["snapshot"]["state"], "Completed", result)
        self.assertEqual(result["backend_calls"], len(actions) + len(more))
        self.assertFalse(result["mismatch"])
        state = result["snapshot"]["business"]
        self.assertEqual(state["dungeons"], 2)
        self.assertEqual(state["giant_cycles_completed"], 2)
        self.assertEqual(state["inn_rests"], 1)
        self.assertEqual(result["snapshot"]["completed_business_units"], 2)

    def test_giant_stop_and_input_failure_do_not_finish_cycle(self):
        _, frames, actions, _ = self.giant_scenario()
        for stopped in (False, True):
            result = self.execute("giant-stop-" + str(stopped), frames[:2],
                [{**actions[0], "reject": not stopped}], **self.giant_options(stop_after_first=stopped))
            self.assertEqual(result["snapshot"]["state"], "UserStopped" if stopped else "Failed", result)
            self.assertEqual(result["backend_calls"], 1)
            self.assertFalse(result["mismatch"])
            self.assertEqual(result["snapshot"]["business"]["dungeons"], 1)
            self.assertEqual(result["snapshot"]["business"]["giant_cycles_completed"], 0)
            self.assertEqual(result["snapshot"]["business"]["inn_rests"], 0)

    @staticmethod
    def trap_scenario():
        # 只读 AST 提取固定源码中的 TargetInfo 常量，不执行旧 Python 业务。
        source = subprocess.check_output(["git", "show", "6585f4075f5714ab522aa582993860c09af912c1:src/script.py"], cwd=ROOT.parent)
        tree = ast.parse(source.decode("utf-8"))
        branch = next(node for node in ast.walk(tree) if isinstance(node, ast.match_case)
            and isinstance(node.pattern, ast.MatchValue) and isinstance(node.pattern.value, ast.Constant)
            and node.pattern.value.value == "fortress-B8F_trap")
        call = next(node for statement in branch.body for node in ast.walk(statement)
            if isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and node.func.id == "StateDungeon")
        targets = [[ast.literal_eval(value) for value in node.args] for node in call.args[0].elts]
        frames, commands, focused = [{"mapFlag": (100, 100)}], [], {}
        def advance(command, screen):
            commands.append(command)
            frames.append(dict(screen))
        for row in targets:
            name = row[0]
            if name == "mark_auto":
                advance(dict(kind=5, key=4), {"dungFlag": (50, 150), name: (760, 350)})
                advance(dict(kind=0, x=800, y=390), {"dungFlag": (50, 150)})
                advance(dict(kind=0, x=777, y=150), {"mapFlag": (100, 100), name: (400, 700)})
                focused[str(len(frames) - 1)] = [name]
                continue
            swipe = {"左上": (100, 250, 700, 1200), "右下": (700, 1200, 100, 250), "左下": (100, 1200, 700, 250)}[row[1]]
            x, y = row[2]
            advance(dict(kind=1, x=swipe[0], y=swipe[1], x2=swipe[2], y2=swipe[3], duration=400), {"mapFlag": (100, 100)})
            advance(dict(kind=0, x=x, y=y), {"mapFlag": (100, 100)})
            advance(dict(kind=0, x=136, y=1431), {"dungFlag": (50, 150)})
            after = {"mapFlag": (100, 100), "cursor_0": (x - 20, y - 12)}
            if name.startswith("stair"):
                after[name] = (400, 600)
            advance(dict(kind=0, x=777, y=150), after)
            # 旧 StateMap_FindSwipeClick 每次重入先拖动，再判断是否到点/换层。
            # 移动停止后的二次开图也需要这次输入，不能在夹具中省略。
            advance(dict(kind=1, x=swipe[0], y=swipe[1], x2=swipe[2], y2=swipe[3], duration=400), after)
        return targets, frames, commands, focused

    def test_trap_full_route_and_cold_recovery_preserve_source_order(self):
        targets, frames, commands, focused = self.trap_scenario()
        for cold in (False, True):
            keyframes = {str(int(key) + int(cold)): value for key, value in focused.items()}
            r = self.execute("trap-route-" + str(cold), ([{}] if cold else []) + frames, commands,
                workflow="fortress-trap", quest_catalog=str(ROOT / "packs/wvd/parameters/legacy-quests.json"),
                profile=self.wall_profile(False), large_templates=["mark_auto"], focused_map_templates=keyframes,
                extra_images=["stair_fortress1f", "stair_fortressGate"],
                attach_recovery=cold, initial_connection="closed" if cold else "ready")
            self.assertEqual(r["snapshot"]["state"], "Completed", r)
            self.assertEqual(r["backend_calls"], len(commands))
            self.assertFalse(r["mismatch"])
            self.assertEqual(r["cursor"], len(frames) - 1 + int(cold))
            self.assertEqual([row["target"] for row in r["task_plan"]["route"]], [row[0] for row in targets])
            self.assertEqual([row["position"] for row in r["task_plan"]["route"]], [row[2] if len(row) > 2 else None for row in targets])
            self.assertNotIn("_TARGETINFOLIST", r["task_plan"]["source"])
            self.assertEqual(r["snapshot"]["business"]["task_step"], 7)
            self.assertEqual(r["snapshot"]["business"]["dungeons"], 1)
            self.assertEqual(r["snapshot"]["business"]["trap_cycles_completed"], 1)
            self.assertEqual(r["snapshot"]["business"]["crashes"], int(cold))

    def test_trap_stop_and_failed_swipe_preserve_attempt_without_completion(self):
        _, frames, commands, focused = self.trap_scenario()
        for stop in (False, True):
            first = {**commands[0], "reject": not stop}
            r = self.execute("trap-stop-" + str(stop), frames[:2], [first], workflow="fortress-trap",
                quest_catalog=str(ROOT / "packs/wvd/parameters/legacy-quests.json"), profile=self.wall_profile(False),
                extra_images=["stair_fortress1f", "stair_fortressGate"], large_templates=["mark_auto"], stop_after_first=stop)
            self.assertEqual(r["snapshot"]["state"], "UserStopped" if stop else "Failed", r)
            self.assertEqual(r["backend_calls"], 1)
            self.assertFalse(r["mismatch"])
            self.assertEqual(r["snapshot"]["business"]["dungeons"], 1)
            self.assertEqual(r["snapshot"]["business"]["trap_cycles_completed"], 0)
            self.assertEqual(r["snapshot"]["business"]["task_step"], 0)

    def test_trap_mid_route_restart_rebuilds_local_route_without_recounting_cycle(self):
        _, frames, commands, focused = self.trap_scenario()
        fault_index = [i + 1 for i, command in enumerate(commands)
                       if command == dict(kind=0, x=136, y=1431)][2]
        self.assertEqual(commands[fault_index - 1], dict(kind=0, x=136, y=1431))
        fault_frames = frames[:fault_index] + [{"mapFlag": (100, 100), "AutoMove": (300, 700)}]
        restart_frame = len(fault_frames)
        focus = {**focused, **{str(int(key) + restart_frame): value for key, value in focused.items()}}
        r = self.execute("trap-mid-restart", fault_frames + frames, commands[:fault_index] + commands,
            workflow="fortress-trap", quest_catalog=str(ROOT / "packs/wvd/parameters/legacy-quests.json"),
            profile=self.wall_profile(False), large_templates=["mark_auto"], focused_map_templates=focus,
            extra_images=["stair_fortress1f", "stair_fortressGate"], attach_recovery=True,
            restart_frame=restart_frame, restart_action=fault_index)
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], fault_index + len(commands))
        self.assertFalse(r["mismatch"])
        self.assertEqual(r["snapshot"]["sessions"][0]["reason"], "navigation.automove_physics_frozen")
        self.assertEqual(r["snapshot"]["sessions"][0]["business"]["task_step"], 3)
        self.assertEqual(r["snapshot"]["business"]["task_step"], 7)
        self.assertEqual(r["snapshot"]["business"]["dungeons"], 1)
        self.assertEqual(r["snapshot"]["business"]["trap_cycles_completed"], 1)
        self.assertEqual(r["snapshot"]["business"]["crashes"], 1)
        self.assertEqual(r["snapshot"]["generation"], 2)

    def test_trap_normal_continuation_counts_two_distinct_cycles(self):
        _, frames, commands, focused = self.trap_scenario()
        offset = len(frames) - 1
        focus = {**focused, **{str(int(key) + offset): value for key, value in focused.items()}}
        r = self.execute("trap-two-cycles", frames + frames[1:], commands + commands,
            workflow="fortress-trap", quest_catalog=str(ROOT / "packs/wvd/parameters/legacy-quests.json"),
            profile=self.wall_profile(False), large_templates=["mark_auto"], focused_map_templates=focus,
            extra_images=["stair_fortress1f", "stair_fortressGate"], normal_units=2)
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 2 * len(commands))
        self.assertFalse(r["mismatch"])
        self.assertEqual(r["snapshot"]["completed_business_units"], 2)
        self.assertEqual(r["snapshot"]["business"]["dungeons"], 2)
        self.assertEqual(r["snapshot"]["business"]["trap_cycles_completed"], 2)
        self.assertEqual(r["snapshot"]["business"]["crashes"], 0)
        self.assertEqual(r["lifecycle_calls"], [])

    def test_trap_early_city_return_is_not_task_completion(self):
        _, frames, commands, _ = self.trap_scenario()
        r = self.execute("trap-early-return", frames[:3] + [{"Inn": (400, 700)}], commands[:3],
            workflow="fortress-trap", quest_catalog=str(ROOT / "packs/wvd/parameters/legacy-quests.json"),
            profile=self.wall_profile(False), large_templates=["mark_auto"],
            extra_images=["stair_fortress1f", "stair_fortressGate"])
        self.assertEqual(r["snapshot"]["state"], "Interrupted", r)
        self.assertEqual(r["backend_calls"], 3)
        self.assertFalse(r["mismatch"])
        self.assertEqual(r["snapshot"]["sessions"][-1]["reason"], "quest.trap_route_incomplete")
        self.assertEqual(r["snapshot"]["business"]["trap_cycles_completed"], 0)
        self.assertEqual(r["snapshot"]["business"]["task_step"], 0)

    def test_karma_confirmed_updates_only_new_profile_and_revises_once(self):
        for value, symbol, after in [("+0", "ambush", "+2"), ("-1", "ambush", "1"), ("+1", "ignore", "+0")]:
            r = self.execute("karma-" + value, [{"ambush": (300, 700), "ignore": (500, 700)},
                {"Inn": (400, 700)}], [dict(kind=0, x=320 if symbol == "ambush" else 520, y=712)],
                workflow="common", profile={"KARMA_ADJUST": value}, karma_profile=True)
            self.assertEqual(r["snapshot"]["state"], "Completed", r)
            self.assertEqual(r["backend_calls"], 1)
            self.assertFalse(r["mismatch"])
            self.assertEqual(r["profile_after"]["values"]["KARMA_ADJUST"], after)
            self.assertEqual(r["profile_before"]["legacy_document"], r["profile_after"]["legacy_document"])
            effect = r["snapshot"]["business"]["karma_effect"]
            self.assertEqual((effect["before"], effect["after"], effect["save_status"]), (value, after, "Saved"))
            self.assertEqual(effect["operation_id"], r["profile_after"]["last_business_update"]["operation_id"])
            self.assertEqual(effect["profile_revision"], r["profile_after"]["revision"])
            self.assertNotEqual(r["profile_before"]["revision"], r["profile_after"]["revision"])

    def test_karma_save_failure_keeps_effect_and_never_replays_input(self):
        for fault in ("conflict", "lock", "replace"):
            r = self.execute("karma-save-" + fault, [{"ambush": (300, 700)}, {"Inn": (400, 700)}],
                [dict(kind=0, x=320, y=712)], workflow="common", karma_profile=True, karma_save_fault=fault)
            self.assertEqual(r["snapshot"]["state"], "Failed", r)
            self.assertIn("PROFILE_SAVE_FAILED", r["snapshot"]["reason"])
            self.assertEqual(r["backend_calls"], 1)
            self.assertEqual(r["cursor"], 1)
            self.assertFalse(r["mismatch"])
            effect = r["snapshot"]["business"]["karma_effect"]
            self.assertEqual((effect["before"], effect["after"], effect["save_status"]), ("+0", "+2", "Failed"))
            self.assertGreater(effect["frame_id"], 0)
            self.assertEqual(r["profile_after"]["values"]["KARMA_ADJUST"], "+9" if fault == "conflict" else "+0")
            self.assertNotIn("last_business_update", r["profile_after"])

    def test_karma_missing_writer_or_invalid_value_never_inputs(self):
        for name, binding, value, error in [("unbound", False, "+0", "KARMA_PROFILE_NOT_BOUND"),
                                            ("invalid", True, "bad", "KARMA_VALUE_INVALID")]:
            r = self.execute("karma-" + name, [{"ambush": (300, 700)}], [], workflow="common",
                karma_profile=binding, profile={"KARMA_ADJUST": value})
            self.assertEqual(r["snapshot"]["state"], "Failed", r)
            self.assertIn(error, r["snapshot"]["reason"])
            self.assertEqual(r["backend_calls"], 0)
            self.assertIsNone(r["snapshot"]["business"]["karma_effect"])

    def test_karma_stop_or_rejected_input_never_saves(self):
        for stop in (False, True):
            r = self.execute("karma-stop-" + str(stop), [{"ambush": (300, 700)}, {"Inn": (400, 700)}],
                [dict(kind=0, x=320, y=712, reject=not stop)], workflow="common", karma_profile=True, stop_after_first=stop)
            self.assertEqual(r["snapshot"]["state"], "UserStopped" if stop else "Failed", r)
            self.assertEqual(r["backend_calls"], 1)
            self.assertEqual(r["profile_before"], r["profile_after"])
            self.assertIsNone(r["snapshot"]["business"]["karma_effect"])

    def test_karma_unchanged_or_unknown_result_is_not_automatically_retried(self):
        for name, after in [("unchanged", {"ambush": (300, 700)}), ("unknown", {})]:
            r = self.execute("karma-" + name, [{"ambush": (300, 700)}, after],
                [dict(kind=0, x=320, y=712)], workflow="common", karma_profile=True, attach_recovery=True)
            self.assertEqual(r["snapshot"]["state"], "Failed" if name == "unknown" else "Interrupted", r)
            self.assertEqual(r["snapshot"]["reason"], "POSTCONDITION_TIMEOUT" if name == "unknown" else "RECOVERY_REQUIRED")
            self.assertEqual(r["snapshot"]["sessions"][-1]["reason"],
                "POSTCONDITION_TIMEOUT" if name == "unknown" else "karma.choice_outcome_unconfirmed")
            self.assertEqual(r["backend_calls"], 1)
            self.assertEqual(r["lifecycle_calls"], [])
            self.assertEqual(r["profile_before"], r["profile_after"])
            self.assertIsNone(r["snapshot"]["business"]["karma_effect"])
            self.assertTrue(r["snapshot"]["business"]["karma_pending"])

    def test_karma_retry_overlay_is_not_evidence_of_success(self):
        r = self.execute("karma-retry", [{"ambush": (300, 700)}, {"retry": (300, 700), "dungFlag": (50, 150)}],
            [dict(kind=0, x=320, y=712)], workflow="common", karma_profile=True, attach_recovery=True)
        self.assertEqual(r["snapshot"]["state"], "Interrupted", r)
        self.assertEqual(r["backend_calls"], 1)
        self.assertEqual(r["lifecycle_calls"], [])
        self.assertEqual(r["profile_before"], r["profile_after"])
        self.assertIsNone(r["snapshot"]["business"]["karma_effect"])
        self.assertEqual(r["snapshot"]["sessions"][-1]["reason"], "karma.choice_outcome_unconfirmed")

    def test_wall_bypass_runs_three_actions_once_after_restart_then_reaches_target(self):
        dungeon = {"dungFlag": (50, 150)}
        target = {"mapFlag": (100, 100), "cursor_0": (480, 588)}
        r = self.execute("wall-after-restart", [{}, dungeon, dungeon, dungeon, dungeon, target],
            [dict(kind=1, x=300, y=950, x2=600, y2=950, duration=400),
             dict(kind=0, x=27, y=950), dict(kind=0, x=853, y=950), dict(kind=0, x=777, y=150)],
            workflow="dungeon-route", profile=self.wall_profile(), attach_recovery=True, initial_connection="closed",
            route_targets=[["position", [None], [500, 600]]])
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 4)
        self.assertFalse(r["mismatch"])
        self.assertTrue(r["snapshot"]["business"]["bypass_after_restart"])
        self.assertEqual(r["snapshot"]["business"]["wall_bypass_step"], 3)
        self.assertEqual(r["snapshot"]["business"]["task_step"], 1)

    def test_wall_bypass_never_runs_before_restart_or_when_disabled_or_in_quest(self):
        dungeon = {"dungFlag": (50, 150)}
        target = {"mapFlag": (100, 100), "cursor_0": (480, 588)}
        for name, enabled, cold, kind in [("before", True, False, "dungeon"),
                                          ("disabled", False, True, "dungeon"), ("quest", True, True, "quest")]:
            frames = ([{}] if cold else []) + [dungeon, target]
            r = self.execute("wall-skip-" + name, frames, [dict(kind=0, x=777, y=150)],
                workflow="dungeon-route", profile=self.wall_profile(enabled), attach_recovery=cold,
                initial_connection="closed" if cold else "ready", route_type=kind,
                route_targets=[["position", [None], [500, 600]]])
            self.assertEqual(r["snapshot"]["state"], "Completed", r)
            self.assertEqual(r["backend_calls"], 1)
            self.assertFalse(r["mismatch"])
            self.assertEqual(r["snapshot"]["business"]["wall_bypass_step"], 0 if cold else 3)

    def test_wall_bypass_retry_overlay_does_not_repeat_turn(self):
        dungeon = {"dungFlag": (50, 150)}
        target = {"mapFlag": (100, 100), "cursor_0": (480, 588)}
        r = self.execute("wall-retry", [{}, dungeon, {**dungeon, "retry": (300, 700)},
            dungeon, dungeon, dungeon, target],
            [dict(kind=1, x=300, y=950, x2=600, y2=950, duration=400), dict(kind=0, x=320, y=712),
             dict(kind=0, x=27, y=950), dict(kind=0, x=853, y=950), dict(kind=0, x=777, y=150)],
            workflow="dungeon-route", profile=self.wall_profile(), attach_recovery=True, initial_connection="closed",
            route_targets=[["position", [None], [500, 600]]])
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 5)
        self.assertFalse(r["mismatch"])
        self.assertEqual(r["snapshot"]["business"]["wall_bypass_sequence"], 1)

    def test_wall_bypass_combat_interrupt_resumes_only_remaining_actions(self):
        dungeon = {"dungFlag": (50, 150)}
        target = {"mapFlag": (100, 100), "cursor_0": (480, 588)}
        r = self.execute("wall-combat", [{}, dungeon, self.turn_screen(), dungeon, dungeon, dungeon, target],
            [dict(kind=1, x=300, y=950, x2=600, y2=950, duration=400), dict(kind=0, x=513, y=1200),
             dict(kind=0, x=27, y=950), dict(kind=0, x=853, y=950), dict(kind=0, x=777, y=150)],
            workflow="dungeon-route", profile=self.wall_profile(), attach_recovery=True, initial_connection="closed",
            route_targets=[["position", [None], [500, 600]]])
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 5)
        self.assertFalse(r["mismatch"])
        self.assertEqual(r["snapshot"]["business"]["combats"], 1)
        self.assertEqual(r["snapshot"]["business"]["wall_bypass_step"], 3)

    def test_wall_bypass_stop_or_reject_never_completes_pending_phase(self):
        dungeon = {"dungFlag": (50, 150)}
        for stop in (False, True):
            r = self.execute("wall-stop-" + str(stop), [{}, dungeon, dungeon],
                [dict(kind=1, x=300, y=950, x2=600, y2=950, duration=400, reject=not stop)],
                workflow="dungeon-route", profile=self.wall_profile(), attach_recovery=True, initial_connection="closed",
                stop_after_first=stop, route_targets=[["position", [None], [500, 600]]])
            self.assertEqual(r["snapshot"]["state"], "UserStopped" if stop else "Failed", r)
            self.assertEqual(r["backend_calls"], 1)
            self.assertEqual(r["snapshot"]["business"]["wall_bypass_step"], 0)
            self.assertFalse(r["snapshot"]["business"]["bypass_after_restart"])

    def test_global_prompt_sandman_then_retry_returns_without_extra_input(self):
        r = self.execute("global-sandman-retry", [{"sandman_recover": (350, 850)},
            {"retry": (300, 700)}, {"dungFlag": (50, 150)}],
            [dict(kind=0, x=370, y=862), dict(kind=0, x=320, y=712)], workflow="common")
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 2)
        self.assertFalse(r["mismatch"])
        self.assertEqual(r["snapshot"]["business"]["revivals"], 0)
        self.assertEqual(r["lifecycle_calls"], [])

    def test_global_prompt_blessing_closes_secondary_confirmation(self):
        for secondary in (False, True):
            initial = {"blessing": (300, 550)}
            if secondary:
                initial["combatClose"] = (550, 950)
            r = self.execute("global-blessing-" + str(secondary), [initial, {"dungFlag": (50, 150)}],
                [dict(kind=0, x=570 if secondary else 320, y=962 if secondary else 562)], workflow="common")
            self.assertEqual(r["snapshot"]["state"], "Completed", r)
            self.assertEqual(r["backend_calls"], 1)
            self.assertFalse(r["mismatch"])

    def test_global_prompt_blessing_selection_can_open_confirmation_before_closing(self):
        blessing = {"blessing": (300, 550)}
        r = self.execute("global-blessing-confirmation", [blessing,
            {**blessing, "combatClose": (550, 950)}, {"dungFlag": (50, 150)}],
            [dict(kind=0, x=320, y=562), dict(kind=0, x=570, y=962)], workflow="common")
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 2)
        self.assertFalse(r["mismatch"])

    def test_global_prompt_unchanged_is_bounded(self):
        for marker in ("sandman_recover", "blessing"):
            r = self.execute("global-stuck-" + marker, [{marker: (300, 550)}] * 7,
                [dict(kind=0, x=320, y=562)] * 6, workflow="common")
            self.assertEqual(r["snapshot"]["state"], "Interrupted", r)
            self.assertEqual(r["snapshot"]["sessions"][-1]["reason"], "global." + marker + ".unchanged")
            self.assertEqual(r["backend_calls"], 6)
            self.assertFalse(r["mismatch"])

    def test_global_prompt_stop_and_rejection_preserve_no_business_success(self):
        for stop in (False, True):
            r = self.execute("global-stop-" + str(stop), [{"sandman_recover": (300, 550)},
                {"dungFlag": (50, 150)}], [dict(kind=0, x=320, y=562, reject=not stop)],
                workflow="common", stop_after_first=stop)
            self.assertEqual(r["snapshot"]["state"], "UserStopped" if stop else "Failed", r)
            self.assertEqual(r["backend_calls"], 1)
            self.assertEqual(r["snapshot"]["business"]["revivals"], 0)
            self.assertEqual(r["snapshot"]["business"]["combats"], 0)

    def test_global_prompt_close_marker_alone_does_not_authorize_input(self):
        r = self.execute("global-close-negative", [{"Inn": (100, 400), "combatClose": (550, 950)}], [], workflow="common")
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 0)
        r = self.execute("global-missing", [{"Inn": (100, 400)}], [], workflow="common", omit_image="blessing.png")
        self.assertIn("COMPILE_IMAGE_NOT_IN_MANIFEST", r["publish_error"])
        self.assertEqual(r["connections"], 0)

    def test_global_prompt_iteration_returns_to_same_task_point(self):
        base = {"mapFlag": (100, 100), "cursor_0": (480, 588)}
        r = self.execute("global-iteration", [{**base, "sandman_recover": (300, 550)}, base],
            [dict(kind=0, x=320, y=562)], workflow="iteration", profile=self.turn_profile(defend=True),
            route_targets=[["position", [None], [500, 600]]])
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["snapshot"]["business"]["task_step"], 1)
        self.assertEqual(r["snapshot"]["generation"], 1)
        self.assertEqual(r["backend_calls"], 1)
        self.assertEqual(r["lifecycle_calls"], [])

    def test_party_defeat_acknowledges_marker_without_inventing_combat_behavior(self):
        r = self.execute("defeat-to-revival", [{"multipeopledead": (300, 500), "skull": (400, 900)},
            {"RiseAgain": (350, 450)}], [dict(kind=0, x=420, y=912)], workflow="common")
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 1)
        self.assertFalse(r["mismatch"])
        self.assertTrue(r["snapshot"]["business"]["suicide_requested"])
        self.assertEqual(r["snapshot"]["business"]["revivals"], 0)

    def test_party_defeat_marker_on_world_map_never_clicks_skull(self):
        r = self.execute("defeat-world-negative", [{"worldmapflag": (50, 150),
            "multipeopledead": (300, 500), "skull": (400, 900)}], [], workflow="common")
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 0)
        self.assertFalse(r["snapshot"]["business"]["suicide_requested"])

    def test_party_defeat_stops_or_rejects_and_never_claims_revival(self):
        scene = {"multipeopledead": (300, 500), "skull": (400, 900)}
        for stopped in (False, True):
            r = self.execute(f"defeat-stop-{stopped}", [scene, scene],
                [dict(kind=0, x=420, y=912, reject=not stopped)], workflow="common", stop_after_first=stopped)
            self.assertEqual(r["snapshot"]["state"], "UserStopped" if stopped else "Failed", r)
            self.assertEqual(r["backend_calls"], 1)
            self.assertFalse(r["mismatch"])
            self.assertTrue(r["snapshot"]["business"]["suicide_requested"])
            self.assertEqual(r["snapshot"]["business"]["revivals"], 0)

    def test_party_defeat_six_unchanged_clicks_have_a_finite_exit(self):
        scene = {"multipeopledead": (300, 500), "skull": (400, 900)}
        r = self.execute("defeat-unchanged", [scene] * 7, [dict(kind=0, x=420, y=912)] * 6, workflow="common")
        self.assertEqual(r["snapshot"]["state"], "Interrupted", r)
        self.assertEqual(r["backend_calls"], 6)
        self.assertFalse(r["mismatch"])
        self.assertEqual(r["snapshot"]["sessions"][-1]["reason"], "party.defeat_prompt_unchanged")

    def test_party_defeat_preserves_single_prompt_priority_and_overlay_handoff(self):
        multiple = {"multipeopledead": (300, 500), "skull": (400, 900)}
        r = self.execute("defeat-single-retry", [{**multiple, "someonedead": (350, 600)}, multiple,
            {"retry": (300, 700)}, {"dungFlag": (50, 150)}],
            [dict(kind=0, x=450, y=800), dict(kind=0, x=420, y=912), dict(kind=0, x=320, y=712)], workflow="common")
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 3)
        self.assertFalse(r["mismatch"])
        self.assertFalse(r["snapshot"]["business"]["death_prompt_pending"])
        self.assertTrue(r["snapshot"]["business"]["suicide_requested"])
        self.assertEqual(r["snapshot"]["business"]["revivals"], 0)

    def test_party_death_clears_after_first_or_fifth_without_followup_click(self):
        for attempts in (1, 5):
            dead = {"someonedead": (350, 600)}
            r = self.execute(f"death-clears-{attempts}", [dead] * attempts + [{"dungFlag": (50, 150)}],
                [dict(kind=0, x=450, y=800)] * attempts, workflow="common")
            self.assertEqual(r["snapshot"]["state"], "Completed", r)
            self.assertEqual(r["backend_calls"], attempts)
            self.assertFalse(r["mismatch"])
            business = r["snapshot"]["business"]
            self.assertEqual(business["death_prompt_sequence"], 1)
            self.assertFalse(business["death_prompt_pending"])
            self.assertEqual(business["revivals"], 0)
            self.assertEqual(business["combats"], 0)

    def test_party_death_map_city_and_character_symbols_never_authorize_center(self):
        for symbol in ("worldmapflag", "Inn", "mapFlag", "dungFlag", "trait"):
            r = self.execute("death-negative-" + symbol,
                [{symbol: (50, 150), "someonedead": (350, 600)}], [], workflow="common")
            self.assertEqual(r["snapshot"]["state"], "Completed", r)
            self.assertEqual(r["backend_calls"], 0)
            self.assertEqual(r["snapshot"]["business"]["death_prompt_sequence"], 0)

    def test_party_death_stuck_stop_and_rejected_input_preserve_pending(self):
        dead = {"someonedead": (350, 600)}
        for mode in ("stuck", "stop", "reject"):
            attempts = 5 if mode == "stuck" else 1
            r = self.execute("death-" + mode, [dead] * (attempts + 1),
                [dict(kind=0, x=450, y=800, reject=mode == "reject")] * attempts,
                workflow="common", stop_after_first=mode == "stop")
            self.assertEqual(r["snapshot"]["state"],
                {"stuck": "Interrupted", "stop": "UserStopped", "reject": "Failed"}[mode], r)
            self.assertEqual(r["backend_calls"], attempts)
            self.assertFalse(r["mismatch"])
            self.assertTrue(r["snapshot"]["business"]["death_prompt_pending"])
            self.assertEqual(r["snapshot"]["business"]["revivals"], 0)
            if mode == "stuck":
                self.assertEqual(r["snapshot"]["sessions"][-1]["reason"], "party.death_prompt_unchanged")

    def test_party_death_retry_overlay_returns_without_second_death_click(self):
        r = self.execute("death-retry", [{"someonedead": (350, 600)}, {"retry": (300, 700)},
            {"dungFlag": (50, 150)}], [dict(kind=0, x=450, y=800), dict(kind=0, x=320, y=712)], workflow="common")
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 2)
        self.assertFalse(r["mismatch"])
        # 普通插入后到达已知正常场景，补齐清除回执，但不重复执行死亡点击。
        self.assertFalse(r["snapshot"]["business"]["death_prompt_pending"])

    def test_party_death_iteration_resumes_the_original_task(self):
        r = self.execute("death-iteration", [{"someonedead": (350, 600)},
            {"mapFlag": (100, 100), "cursor_0": (480, 588)}], [dict(kind=0, x=450, y=800)],
            workflow="iteration", profile=self.turn_profile(defend=True), route_targets=[["position", [None], [500, 600]]])
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 1)
        self.assertFalse(r["mismatch"])
        self.assertEqual(r["snapshot"]["business"]["task_step"], 1)
        self.assertEqual(r["snapshot"]["business"]["death_prompt_sequence"], 1)
        self.assertEqual(r["lifecycle_calls"], [])

    def test_pause_precedes_ready_and_accepts_the_sixth_resume(self):
        ready = {"dungFlag": (50, 150)}
        for attempts in (1, 6):
            r = self.execute(f"pause-clears-{attempts}", [ready] * (attempts + 1),
                [dict(kind=0, x=450, y=760)] * attempts, workflow="common",
                pause_frames=list(range(attempts)))
            self.assertEqual(r["snapshot"]["state"], "Completed", r)
            self.assertEqual(r["backend_calls"], attempts)
            self.assertFalse(r["mismatch"])
            self.assertEqual(r["lifecycle_calls"], [])

    def test_pause_six_ineffective_inputs_request_physics_recovery(self):
        r = self.execute("pause-frozen", [{"dungFlag": (50, 150)}] * 7,
            [dict(kind=0, x=450, y=760)] * 6, workflow="common", pause_frames=list(range(7)))
        self.assertEqual(r["snapshot"]["state"], "Interrupted", r)
        self.assertEqual(r["snapshot"]["reason"], "RECOVERY_REQUIRED")
        self.assertEqual(r["snapshot"]["sessions"][-1]["reason"], "pause.physics_frozen")
        self.assertEqual(r["backend_calls"], 6)
        self.assertFalse(r["mismatch"])

    def test_pause_character_and_detail_negatives_never_click_center(self):
        for symbol, point in (("trait", (100, 900)), ("recover", (100, 900)),
                              ("spellskill/skillDetail", (100, 900)), ("close", (350, 1450))):
            r = self.execute("pause-negative-" + symbol.replace("/", "-"),
                [{"dungFlag": (50, 150), symbol: point}], [], workflow="common", pause_frames=[0])
            self.assertEqual(r["snapshot"]["state"], "Completed", r)
            self.assertEqual(r["backend_calls"], 0)

    def test_pause_stop_and_rejected_input_do_not_continue(self):
        for stopped in (False, True):
            r = self.execute(f"pause-stop-{stopped}", [{"dungFlag": (50, 150)}] * 2,
                [dict(kind=0, x=450, y=760, reject=not stopped)], workflow="common",
                pause_frames=[0, 1], stop_after_first=stopped)
            self.assertEqual(r["snapshot"]["state"], "UserStopped" if stopped else "Failed", r)
            self.assertEqual(r["backend_calls"], 1)
            self.assertFalse(r["mismatch"])

    def test_common_resume_overlay_precedes_ready_game(self):
        r = self.execute("common-resume", [{"dungFlag": (50, 150), "resume": (400, 800)}, {"dungFlag": (50, 150)}],
                         [dict(kind=0, x=420, y=812)], workflow="common")
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 1)
        self.assertEqual(r["lifecycle_calls"], [])

    def assert_uncertain_effect(self, result, reason, calls):
        self.assertEqual(result["snapshot"]["state"], "Interrupted", result)
        self.assertEqual(result["snapshot"]["sessions"][-1]["reason"], reason)
        self.assertEqual(result["backend_calls"], calls)
        self.assertEqual(result["snapshot"]["generation"], 1)
        self.assertEqual(result["lifecycle_calls"], [])
        self.assertFalse(result["mismatch"])

    def test_image_source_baseline_precedes_alias_and_mod(self):
        city = "City_RoyalCityLuknalia"
        result = self.execute("image-base-first", [{"worldmapflag": (80, 100), city: (132, 1352)}, {"Inn": (100, 400)}],
            [dict(kind=0, x=152, y=1364)], aliases={city + ".png": "Alternate.png"},
            extra_images=["Alternate"], mod_images={city: "Inn"})
        self.assertEqual(result["snapshot"]["state"], "Completed", result)
        self.assertEqual(result["backend_calls"], 1)
        selected = result["image_sources"]["images"][city + ".png"]
        self.assertEqual((selected["source"], selected["path"]), ("baseline", "image/" + city + ".png"))

    def test_image_source_alias_precedes_mod_original(self):
        city = "City_RoyalCityLuknalia"
        result = self.execute("image-alias-first", [{"worldmapflag": (80, 100), "Alternate": (132, 1352)}, {"Inn": (100, 400)}],
            [dict(kind=0, x=152, y=1364)], aliases={city + ".png": "Alternate.png"},
            extra_images=["Alternate"], mod_only_images=[city], mod_images={city: city})
        self.assertEqual(result["snapshot"]["state"], "Completed", result)
        self.assertEqual(result["backend_calls"], 1)
        selected = result["image_sources"]["images"][city + ".png"]
        self.assertEqual((selected["source"], selected["path"]), ("baseline", "image/Alternate.png"))

    def test_image_source_mod_is_copied_before_runtime(self):
        city = "City_RoyalCityLuknalia"
        result = self.execute("image-mod-snapshot", [{"worldmapflag": (80, 100), city: (132, 1352)}, {"Inn": (100, 400)}],
            [dict(kind=0, x=152, y=1364)], mod_only_images=[city], mod_images={city: city}, mutate_mod_after_publish=True)
        self.assertEqual(result["snapshot"]["state"], "Completed", result)
        self.assertEqual(result["backend_calls"], 1)
        self.assertFalse(result["mismatch"])
        selected = result["image_sources"]["images"][city + ".png"]
        self.assertEqual(selected["source"], "mod")
        self.assertEqual(selected["sha256"], digest(self.root / "image-mod-snapshot/compiled/image" / (city + ".png")))
        self.assertNotEqual(selected["sha256"], digest(self.root / "image-mod-snapshot/private-mod/image" / (city + ".png")))

    def test_image_source_corrupt_baseline_is_not_hidden_by_mod(self):
        city = "City_RoyalCityLuknalia"
        result = self.execute("image-base-corrupt", [{"worldmapflag": (80, 100), city: (132, 1352)}], [],
            bad_base_images=[city], mod_images={city: city})
        self.assertEqual(result["snapshot"]["state"], "Failed", result)
        self.assertEqual(result["backend_calls"], 0)
        self.assertIn("WVD_TEMPLATE_DECODE_INVALID", str(result["snapshot"]))

    def test_image_source_changed_mod_is_rejected_before_connect(self):
        city = "City_RoyalCityLuknalia"
        result = self.execute("image-mod-changed", [{"worldmapflag": (80, 100), city: (132, 1352)}], [],
            mod_only_images=[city], mod_images={city: city}, corrupt_mod_before_publish=True)
        self.assertEqual(result["connections"], 0)
        self.assertEqual(result["backend_calls"], 0)
        self.assertIn("HASH", result["publish_error"])

    def test_uncertain_enemy_input_never_restarts_or_consumes(self):
        detail = self.turn_screen(**{"spellskill/skillDetail": (350, 950), "next": (500, 300)})
        result = self.execute("uncertain-enemy", [self.turn_screen(), detail,
            {"dungFlag": (50, 150), "retry": (400, 800)}],
            [dict(kind=0, x=266, y=1054), dict(kind=0, x=440, y=392)],
            workflow="turn", profile=self.turn_profile(), attach_recovery=True, force_restart=True, max_crashes=0)
        self.assert_uncertain_effect(result, "combat.skill_outcome_unconfirmed", 2)
        business = result["snapshot"]["business"]
        self.assertTrue(business["has_prepared_skill"])
        self.assertEqual(len(business["strategy"]["current"]["skill_settings"]), 2)

    def test_uncertain_defend_is_not_a_normal_return(self):
        result = self.execute("uncertain-defend", [self.turn_screen(),
            {**self.turn_screen("B"), "retry": (400, 800)}], [dict(kind=0, x=513, y=1200)],
            workflow="turn", profile=self.turn_profile(defend=True), attach_recovery=True)
        self.assert_uncertain_effect(result, "combat.skill_outcome_unconfirmed", 1)
        self.assertTrue(result["snapshot"]["business"]["has_prepared_skill"])

    def test_uncertain_heal_and_return_do_not_repeat_recover(self):
        trait = {"trait": (200, 300)}
        for during_back in (False, True):
            with self.subTest(during_back=during_back):
                frames = [{"dungFlag": (50, 150)}, trait, {"recover": (250, 850)}]
                inputs = [dict(kind=0, x=36, y=1425), dict(kind=0, x=830, y=850), dict(kind=0, x=600, y=1200)]
                if during_back:
                    frames.append(trait)
                    inputs.append(dict(kind=5, key=4))
                frames.append({"dungFlag": (50, 150), "retry": (400, 800)})
                result = self.execute("uncertain-heal-" + str(during_back), frames, inputs,
                    workflow="heal", profile={"RECOVER_WHEN_BEGINNING": True}, attach_recovery=True,
                    force_restart=True, max_crashes=0)
                self.assert_uncertain_effect(result, "supply.healing_outcome_unconfirmed", len(inputs))
                self.assertTrue(result["snapshot"]["business"]["healing_required"])
                self.assertEqual(result["snapshot"]["business"]["healing_sequence"], 1)

    def test_uncertain_disarm_does_not_count_or_restart(self):
        result = self.execute("uncertain-disarm", [{"whowillopenit": (330, 450)},
            {"chestOpening": (330, 450)}, {"dungFlag": (50, 150), "retry": (400, 800)}],
            [dict(kind=0, x=258, y=1161), dict(kind=0, x=515, y=934)],
            workflow="chest", character=1, attach_recovery=True, force_restart=True, max_crashes=0)
        self.assert_uncertain_effect(result, "chest.disarm_outcome_unconfirmed", 2)
        self.assertEqual(result["snapshot"]["business"]["chests"], 0)

    def test_uncertain_backend_reject_remains_failed(self):
        detail = self.turn_screen(**{"spellskill/skillDetail": (350, 950), "next": (500, 300)})
        result = self.execute("uncertain-reject", [self.turn_screen(), detail],
            [dict(kind=0, x=266, y=1054), dict(kind=0, x=440, y=392, reject=True)],
            workflow="turn", profile=self.turn_profile(), attach_recovery=True)
        self.assertEqual(result["snapshot"]["state"], "Failed", result)
        self.assertEqual(result["backend_calls"], 2)
        self.assertEqual(result["lifecycle_calls"], [])
        self.assertTrue(result["snapshot"]["business"]["has_prepared_skill"])

    def test_dialogue_all_legacy_default_options(self):
        # 候选集合来自冻结 Git 资源，而非 C++ 列表，避免少迁一项仍测试全绿。
        for index, name in enumerate(self.default_dialogues):
            with self.subTest(option=name):
                result = self.execute("dialogue-option-" + str(index),
                    [{"dialogueChoices/" + name: (300, 700)}, {"Inn": (400, 700)}],
                    [dict(kind=0, x=320, y=712)], workflow="common")
                self.assertEqual(result["snapshot"]["state"], "Completed", result)
                self.assertEqual(result["backend_calls"], 1)
                self.assertFalse(result["mismatch"])
                self.assertEqual(result["lifecycle_calls"], [])

    def test_dialogue_order_and_multiple_pages(self):
        first, second = ("dialogueChoices/" + name for name in self.default_dialogues[:2])
        result = self.execute("dialogue-ordered-pages", [{first: (200, 600), second: (500, 800)},
            {second: (500, 800)}, {"Inn": (400, 700)}],
            [dict(kind=0, x=220, y=612), dict(kind=0, x=520, y=812)], workflow="common")
        self.assertEqual(result["snapshot"]["state"], "Completed", result)
        self.assertEqual(result["backend_calls"], 2)
        self.assertFalse(result["mismatch"])
        self.assertEqual(result["lifecycle_calls"], [])

    def test_dialogue_does_not_override_normal_scene(self):
        for scene in ("Inn", "trait", "recover"):
            with self.subTest(scene=scene):
                result = self.execute("dialogue-normal-" + scene,
                    [{scene: (400, 700), "dialogueChoices/nope": (200, 900)}], [], workflow="common")
                self.assertEqual(result["snapshot"]["state"], "Completed", result)
                self.assertEqual(result["backend_calls"], 0)

    def test_dialogue_unconfirmed_effect_is_not_replayed(self):
        option = {"dialogueChoices/nope": (300, 700)}
        result = self.execute("dialogue-unchanged", [option],
            [dict(kind=0, x=320, y=712, stay=True)], workflow="common",
            attach_recovery=True, force_restart=True, max_crashes=0)
        self.assert_uncertain_effect(result, "dialogue.choice_outcome_unconfirmed", 1)
        result = self.execute("dialogue-unknown-post", [option, {}],
            [dict(kind=0, x=320, y=712)], workflow="common", attach_recovery=True)
        self.assertEqual(result["snapshot"]["state"], "Failed", result)
        self.assertEqual(result["backend_calls"], 1)
        self.assertEqual(result["lifecycle_calls"], [])
        self.assertFalse(result["mismatch"])

    def test_dialogue_stop_and_rejected_click(self):
        for stopped in (False, True):
            with self.subTest(stopped=stopped):
                result = self.execute("dialogue-stop-" + str(stopped),
                    [{"dialogueChoices/nope": (300, 700)}, {"Inn": (400, 700)}],
                    [dict(kind=0, x=320, y=712, reject=not stopped)], workflow="common",
                    stop_after_first=stopped, attach_recovery=True)
                self.assertEqual(result["snapshot"]["state"], "UserStopped" if stopped else "Failed", result)
                self.assertEqual(result["backend_calls"], 1)
                self.assertEqual(result["lifecycle_calls"], [])
                self.assertFalse(result["mismatch"])

    def test_common_title_attention_download_are_normal_inputs(self):
        frames = [{"boot_title_logo": (200, 350)}, {"boot_attention": (300, 450)},
                  {"startdownload": (230, 910)}, {"Inn": (400, 700)}]
        r = self.execute("common-title", frames,
                         [dict(kind=0, x=450, y=1450), dict(kind=0, x=450, y=1450), dict(kind=0, x=250, y=922)], workflow="common")
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 3)
        self.assertEqual(r["snapshot"]["generation"], 1)
        self.assertEqual(r["lifecycle_calls"], [])

    def test_common_retry_uses_old_blank_and_low_confidence_rules(self):
        r = self.execute("common-retry-blank", [{"retry_blank": (400, 700)}, {"Inn": (400, 700)}],
                         [dict(kind=0, x=420, y=815)], workflow="common")
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        r = self.execute("common-retry-low", [{"retry": (400, 700)}, {"retry": (400, 700)}, {"Inn": (400, 700)}],
                         [dict(kind=0, x=450, y=900)] * 2, workflow="common", degraded_retry_frames=[0, 1])
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 2)

    def test_common_download_permission_stop_and_reject(self):
        r = self.execute("common-download-blocked", [{"startdownload": (230, 910)}], [],
                         workflow="common", allow_download=False)
        self.assertEqual(r["snapshot"]["state"], "Interrupted", r)
        self.assertEqual(r["backend_calls"], 0)
        for stop in (False, True):
            r = self.execute("common-stop" if stop else "common-reject", [{"resume": (400, 800)}, {"Inn": (400, 700)}],
                             [dict(kind=0, x=420, y=812, reject=not stop)], workflow="common", stop_after_first=stop)
            self.assertEqual(r["snapshot"]["state"], "UserStopped" if stop else "Failed", r)
            self.assertEqual(r["backend_calls"], 1)

    def test_common_stuck_resume_has_finite_input_budget(self):
        r = self.execute("common-stuck", [{"resume": (400, 800), "dungFlag": (50, 150)}],
                         [dict(kind=0, x=420, y=812, stay=True)], workflow="common")
        self.assertEqual(r["snapshot"]["state"], "Interrupted", r)
        self.assertEqual(r["backend_calls"], 6)
        self.assertEqual(r["lifecycle_calls"], [])

    def test_common_route_insert_returns_to_same_point(self):
        base = {"mapFlag": (100, 100), "cursor_0": (480, 588)}
        r = self.execute("common-route", [{**base, "resume": (400, 800)}, base],
                         [dict(kind=0, x=420, y=812)], workflow="dungeon-route", profile=self.turn_profile(defend=True),
                         route_targets=[["position", [None], [500, 600]]])
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 1)
        self.assertEqual(r["snapshot"]["generation"], 1)
        self.assertEqual(r["snapshot"]["business"]["task_step"], 1)
        self.assertEqual(r["snapshot"]["business"]["crashes"], 0)

    def test_common_iteration_boot_does_not_request_restart(self):
        r = self.execute("common-iteration", [{"boot_title_logo": (200, 350)}, {"Inn": (400, 700), "Dist": (300, 500)},
            {"GotoDung": (400, 700)}, {"mapFlag": (100, 100), "cursor_0": (480, 588)}],
            [dict(kind=0, x=450, y=1450), dict(kind=0, x=320, y=512), dict(kind=0, x=420, y=712)],
            workflow="iteration", profile=self.turn_profile(defend=True), route_targets=[["position", [None], [500, 600]]])
        self.assertEqual(r["snapshot"]["state"], "Completed", r)
        self.assertEqual(r["backend_calls"], 3)
        self.assertEqual(r["snapshot"]["business"]["crashes"], 0)
        self.assertEqual(r["lifecycle_calls"], [])

    def test_common_unknown_and_missing_asset_never_grant_input(self):
        r = self.execute("common-unknown", [{}], [], workflow="common")
        self.assertEqual(r["snapshot"]["state"], "Interrupted", r)
        self.assertEqual(r["backend_calls"], 0)
        r = self.execute("common-missing", [{"Inn": (400, 700)}], [], workflow="common", omit_image="boot_title_logo.png")
        self.assertIn("COMPILE_IMAGE_NOT_IN_MANIFEST", r["publish_error"])
        self.assertEqual(r["connections"], 0)
        self.assertEqual(r["backend_calls"], 0)


if __name__ == "__main__":
    unittest.main()
