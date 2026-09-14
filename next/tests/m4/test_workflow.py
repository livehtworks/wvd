"""运行正式 C++ 编译器的导航/住宿 Pipeline；图像合成，SDK 和门禁不替换。"""
import hashlib
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
        print("M4 workflow evidence: " + str(cls.root), flush=True)

    def execute(self, name, screens, transitions, **options):
        folder = self.root / name
        bundle = folder / "bundle"
        (bundle / "image").mkdir(parents=True)
        names = ["worldmapflag", "City_RoyalCityLuknalia", "Inn", "Stay", "Economy", "royalsuite", "OK"]
        if options.get("workflow") in ("departure", "iteration"):
            names += ["openworldmap", "intoWorldMap", "returntoTown", "returnText", "EdgeOfTown", "dungFlag", "mapFlag", "chestFlag",
                      "combatActive", "combatActive_2", "combatActive_3", "combatActive_4",
                      "guild", "Edit", "PartyManagement", "PartyManagementTitle", "AssembleParty"]
        if options.get("workflow") in ("auto", "turn", "encounter", "dungeon-route", "iteration"):
            names += ["combatActive", "combatActive_2", "combatActive_3", "combatActive_4", "close",
                      "dungFlag", "chestFlag", "RiseAgain",
                      "spellskill/skillDetail", "spellskill/CombatAutoEnable", "spellskill/CombatAutoDisable"]
        if options.get("workflow") in ("turn", "encounter", "dungeon-route", "iteration"):
            names += ["spellskill/char/A", "spellskill/char/A_sp", "spellskill/char/B", "flee", "dungFlag", "chestFlag",
                      "RiseAgain", "supportSkillCheck", "notenoughsp", "notenoughmp", "next", "combatTarget", "combatSpd", "combatSpd_DHI"]
            names += [f"spellskill/skillLvl/{prefix}{level}" for prefix in ("lv", "s_lv") for level in range(1, 10)]
        if options.get("workflow") in ("map", "map-confirm", "state-route", "dungeon-route", "iteration"):
            names += ["mapFlag", "dungFlag", "chest", "chestFlag", "chestOpening", "whowillopenit",
                      "AutoMove", "EdgeOfTown", "combatActive", "combatActive_2", "combatActive_3", "combatActive_4",
                      "cursor_0", "cursor_1", "cursor_2", "cursor_3", "stair_up", "stair_floor", "harken",
                      "returnText", "returntoTown", "openworldmap"]
        if options.get("workflow") in ("chest", "dungeon-route", "iteration"):
            names += ["chestFlag", "whowillopenit", "chestOpening", "chestfear", "RiseAgain", "ambush", "dungFlag",
                      "combatActive", "combatActive_2", "combatActive_3", "combatActive_4"]
        if options.get("workflow") in ("heal", "dungeon-route", "iteration"):
            names += ["mapFlag", "dungFlag", "trait", "recover", "story", "chestFlag", "whowillopenit", "chestOpening", "RiseAgain",
                      "combatActive", "combatActive_2", "combatActive_3", "combatActive_4"]
        if options.get("workflow") == "travel":
            names += ["openworldmap", "intoWorldMap", "dungFlag"]
        if options.get("workflow") in ("party", "party-rest"):
            names += ["guild", "Edit", "PartyManagement", "PartyManagementTitle", "AssembleParty", "partyBlue"]
        if options.get("workflow") in ("auto-route", "entry"):
            names += ["mapFlag", "dungFlag", "chestFlag", "combatActive", "combatActive_2", "combatActive_3", "combatActive_4", "EdgeOfTown"]
        if options.get("workflow") in ("auto-route", "dungeon-route", "iteration"):
            names += ["chestOpening", "whowillopenit", "RiseAgain", "NoChestCanBeFound", "theRouteToTheDestinationCannotBeFound",
                      "chest_auto", "mark_auto", "chest_auto_minus", "resume", "returnText", "returntoTown", "openworldmap"]
        if options.get("workflow") in ("entry", "iteration"):
            names += ["GotoDung", "openworldmap", "returntoTown", "intoWorldMap", "TradeWaterway", "Dist", "EVENT", "FFXI/EVENT_GCN", "FFXI/zone5", "preGate"]
        if options.get("workflow") in ("recover", "common", "iteration", "dungeon-route", "map", "map-confirm", "state-route",
                                      "auto-route", "auto", "turn", "encounter", "chest", "heal", "revival"):
            names += ["dungFlag", "openworldmap", "returnText", "returntoTown", "mapFlag", "chestFlag", "whowillopenit",
                      "fishing/cast", "fishing/striking", "fishing/CloseFishInfo", "combatActive", "combatActive_2",
                      "combatActive_3", "combatActive_4", "boot_title_logo", "boot_attention", "startdownload",
                      "retry", "retry_blank", "totitle", "resume", "trait", "recover", "spellskill/skillDetail", "close", "someonedead", "RiseAgain",
                      "multipeopledead", "skull", "sandman_recover", "blessing", "combatClose", "ambush", "ignore"]
        names += options.get("extra_images", [])
        if options.get("workflow") == "revival":
            names.append("RiseAgain")
        rng = np.random.default_rng(90614)
        patterns = {name: rng.integers(30, 255, (24, 40, 3), dtype=np.uint8) for name in names}
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
            for key, (x, y) in screen.items():
                pattern = patterns[key.split("@", 1)[0]]
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
                pixels[y:y+24, x:x+40] = pattern
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
        if options.get("workflow") in ("chest", "map-confirm", "state-route", "turn", "encounter", "recover", "common", "heal", "dungeon-route", "departure", "inn-tracked", "iteration", "revival"):
            config.update(with_state=True, descriptor=str(ROOT / "packs/wvd/parameters/legacy-config-fields.json"))
        if "omit_image" in options:
            config["files"] = [f for f in config["files"] if f["path"] != "image/" + options["omit_image"]]
        source = folder / "input.json"
        source.write_text(json.dumps(config), encoding="utf-8")
        exe = ROOT / "build/m4/Release/test_m4_workflow.exe"
        before_hash = digest(exe)
        # 测试看护预算跟随正式有限定义；不把较短测试超时伪装成产品停止。
        route_budget = 1300 if options.get("profile", {}).get("QUICK_DISARM_CHEST", False) else 1000
        with (folder / "native.log").open("wb") as log:
            result = subprocess.run([str(exe), str(source)], cwd=folder, env=self.env,
                                    stdout=log, stderr=log, timeout={"dungeon-route": route_budget + 20, "recover": 750, "departure": 200, "heal": 260,
                                        "chest": 920 if options.get("quick") else 620,
                                        "common": 140, "iteration": (route_budget + 380) * options.get("normal_units", 1)}.get(options.get("workflow"), 90))
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

    def test_departure_paid_but_exit_failed_preserves_receipt(self):
        frames, commands = self.inn_sequence()
        commands[-1] = dict(kind=5, key=4, reject=True)
        r = self.execute("inn-paid-exit-failed", frames, commands, workflow="inn-tracked")
        self.assertEqual(r["snapshot"]["state"], "Failed", r)
        self.assertEqual(r["backend_calls"], 5)
        self.assertEqual(r["snapshot"]["business"]["inn_rests"], 1)

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
            self.assertEqual(r["snapshot"]["state"], "Interrupted", r)
            self.assertEqual(r["snapshot"]["reason"], "RECOVERY_REQUIRED")
            self.assertEqual(r["snapshot"]["sessions"][-1]["reason"], "karma.choice_outcome_unconfirmed")
            self.assertEqual(r["backend_calls"], 1)
            self.assertEqual(r["lifecycle_calls"], [])
            self.assertEqual(r["profile_before"], r["profile_after"])

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
