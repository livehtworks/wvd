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
        if options.get("workflow") == "auto":
            names += ["combatActive", "combatActive_2", "combatActive_3", "combatActive_4", "close",
                      "spellskill/skillDetail", "spellskill/CombatAutoEnable", "spellskill/CombatAutoDisable"]
        if options.get("workflow") in ("map", "map-confirm"):
            names += ["mapFlag", "dungFlag", "chest", "chestFlag", "chestOpening", "whowillopenit",
                      "AutoMove", "EdgeOfTown", "combatActive", "combatActive_2", "combatActive_3", "combatActive_4",
                      "cursor_0", "cursor_1", "cursor_2", "cursor_3", "stair_up", "stair_floor"]
        if options.get("workflow") == "chest":
            names += ["chestFlag", "whowillopenit", "chestOpening", "chestfear", "RiseAgain", "ambush", "dungFlag",
                      "combatActive", "combatActive_2", "combatActive_3", "combatActive_4"]
        if options.get("workflow") == "travel":
            names += ["openworldmap", "intoWorldMap", "dungFlag"]
        if options.get("workflow") in ("party", "party-rest"):
            names += ["guild", "Edit", "PartyManagement", "PartyManagementTitle", "AssembleParty", "partyBlue"]
        rng = np.random.default_rng(90614)
        patterns = {name: rng.integers(30, 255, (24, 40, 3), dtype=np.uint8) for name in names}
        def write(path, pixels):
            path.write_bytes(cv2.imencode(".png", pixels)[1].tobytes())
        for key, pixels in patterns.items():
            path = bundle / "image" / (key + ".png")
            path.parent.mkdir(parents=True, exist_ok=True)
            write(path, pixels)
        frames = []
        for i, screen in enumerate(screens):
            pixels = np.zeros((1600, 900, 3), dtype=np.uint8)
            for key, (x, y) in screen.items():
                pixels[y:y+24, x:x+40] = patterns[key.split("@", 1)[0]]
            frame = folder / f"frame-{i}.png"
            write(frame, pixels)
            frames.append(str(frame))
        config = dict(workflow="city", city="City_RoyalCityLuknalia", bundle=str(bundle),
                      frames=frames, transitions=transitions, run_root=str(folder / "run"),
                      output=str(folder / "output.json"), files=[
                          {"path": p.relative_to(bundle).as_posix(), "sha256": digest(p)}
                          for p in sorted(bundle.rglob("*.png"))])
        config.update(options)
        if options.get("workflow") in ("chest", "map-confirm"):
            config.update(with_state=True, descriptor=str(ROOT / "packs/wvd/parameters/legacy-config-fields.json"))
        if "omit_image" in options:
            config["files"] = [f for f in config["files"] if f["path"] != "image/" + options["omit_image"]]
        source = folder / "input.json"
        source.write_text(json.dumps(config), encoding="utf-8")
        exe = ROOT / "build/m4/Release/test_m4_workflow.exe"
        before_hash = digest(exe)
        with (folder / "native.log").open("wb") as log:
            result = subprocess.run([str(exe), str(source)], cwd=folder, env=self.env,
                                    stdout=log, stderr=log, timeout=90)
        self.assertEqual(digest(exe), before_hash)
        (folder / "execution.json").write_text(json.dumps({"exe_sha256": before_hash, "exit": result.returncode}), encoding="utf-8")
        self.assertEqual(result.returncode, 0, (folder / "native.log").read_text(encoding="utf-8", errors="replace")[-3000:])
        output = json.loads((folder / "output.json").read_text(encoding="utf-8"))
        for module in output["loaded_modules"]:
            self.assertEqual(Path(module["path"]).resolve(), (self.sdk / "bin" / module["name"]).resolve())
            self.assertEqual(module["sha256"], digest(self.sdk / "bin" / module["name"]))
        return output

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
            self.assertEqual(result["snapshot"]["business"]["confirmed_operations"], 2)
            self.assertEqual(result["backend_calls"], 3)
            self.assertFalse(result["mismatch"])

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


if __name__ == "__main__":
    unittest.main()
