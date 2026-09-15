"""Fixed legacy dungeon traces executed by the real Maa workflow fixture.

--prepare only materializes independent expectations, never runtime evidence.
Unittest discovery executes four scenarios per TaskID plus bounded stair cases.
No WorkflowTests inheritance, production node inspection, device or user data.
"""
import argparse
import ast
import copy
import hashlib
import importlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
BASELINE = "6585f4075f5714ab522aa582993860c09af912c1"
BLOBS = {
    "resources/quest/quest.json": "c5398eff233aa6105c95cd7e4c7ca51fd4024945",
    "src/script.py": "0f80961a63d06f26ce7d76e33677dc9cfb9bdf72",
}
TASK_IDS = (
    "Dist", "lounge", "fortress-B1F", "fortress-B8F_entrance", "fortress-10F",
    "DH-4f", "DH-6f", "DH-7f-right", "DH-7f-auto", "DH-10f-test", "DH-10f-JR",
    "DH-Church-upper", "DH-Church-auto", "White-G-v2", "FFXI-2F", "FFXI-2F-elite",
    "FFXI-5F-4Elite", "FFXI-5F-2Elite-mid", "FFXI-5F-2Elite-bottom",
    "FFXI-5F-2Elite-left", "FFXI-5F-Elite", "HSR-1F", "IWO", "IWO_2",
    "malice-B3F", "fordraig-B3F", "SSC_chest", "LBC", "AWD", "AWD-noGorgon",
    "GHB", "COS_chest", "COS_chest_short", "DOE", "DOF", "DOF_Plus", "DOW",
    "DOW_Plus", "DOL", "DOL_PLUS", "DOS", "LMG-GT", "LMG-FT",
)
ALIASES = {
    "returnText.png": "ReturnText.png",
    "returntoTown.png": "returntotown.png",
    "Mark_auto.png": "mark_auto.png",
    "malice_B3F.png": "malice_b3f.png",
}
# TargetInfo.swipeDir, fixed script lines 151-172. No new compiler output.
SWIPES = {
    None: [None, [100, 100, 700, 1200], [400, 1200, 400, 100],
           [700, 800, 100, 800], [400, 100, 400, 1200], [100, 800, 700, 800]],
    "\u5de6\u4e0a": [[100, 250, 700, 1200]],
    "\u53f3\u4e0a": [[700, 250, 100, 1200]],
    "\u53f3\u4e0b": [[700, 1200, 100, 250]],
    "\u5de6\u4e0b": [[100, 1200, 700, 250]],
}
EXCLUSIONS = [[0, 0, 900, 208], [0, 1265, 900, 335], [0, 636, 137, 222],
              [763, 636, 137, 222], [336, 208, 228, 77], [336, 1168, 228, 97]]
# Explicit legal top-lefts inside OLD include rectangles and outside exclusions.
# Never search for an ROI or choose a point using the production recognizer.
CHEST_POINTS = {
    ("DH-6f", 2): (220, 900), ("DH-7f-right", 3): (600, 700),
    ("DH-10f-test", 6): (620, 600), ("DH-10f-test", 7): (200, 600),
    ("DH-10f-test", 9): (600, 1000), ("DH-10f-test", 10): (600, 1000),
    ("DOE", 1): (600, 900), ("DOF", 2): (200, 1000),
    ("DOW", 0): (500, 1000), ("DOL", 0): (600, 500), ("DOS", 2): (220, 1000),
}
MAP = {"mapFlag": (100, 100)}
DUNGEON = {"dungFlag": (50, 150)}
CITY = {"Inn": (100, 400)}
OUTSIDE = {"returntoTown": (400, 800)}
AUTO = {**DUNGEON, "chest_auto": (730, 270), "chest_auto_minus": (811, 340)}
WRONG_STAIR_TASKS = ("DH-4f", "DH-Church-upper", "DH-Church-auto")
STAIR_CASES = ("harken_first", "harken_last", "bharken_last", "missing",
               "failure", "stop", "combat", "correct")


class FixtureBlocked(AssertionError):
    """A visible failure, not unittest.SkipTest or expectedFailure."""


def git_bytes(path):
    data = subprocess.check_output(["git", "show", BASELINE + ":" + path], cwd=ROOT.parent)
    oid = hashlib.sha1(b"blob " + str(len(data)).encode("ascii") + b"\0" + data).hexdigest()
    if oid != BLOBS[path]:
        raise AssertionError("fixed legacy blob changed: " + path)
    return data


def baseline():
    quest_bytes, script_bytes = git_bytes("resources/quest/quest.json"), git_bytes("src/script.py")
    catalog = json.loads(quest_bytes)
    ids = tuple(k for k, v in catalog.items() if v.get("_TYPE") == "dungeon")
    if ids != TASK_IDS or len(ids) != 43:
        raise AssertionError("43-task legacy denominator changed")
    tree = ast.parse(script_bytes.decode("utf-8"))
    names = {n.name for n in ast.walk(tree) if isinstance(n, (ast.FunctionDef, ast.ClassDef))}
    required = {"TargetInfo", "StateMapSearch", "StateMap_FindSwipeClick", "StateChest",
                "CheckIf_ReachPosition", "CheckIf_throughStair", "CheckIf_harkenStair", "StateEoT"}
    if not required <= names:
        raise AssertionError("independent source contracts absent")
    files = subprocess.check_output(["git", "ls-tree", "-r", "--name-only", BASELINE,
                                     "resources/images"], cwd=ROOT.parent).decode("utf-8").splitlines()
    images = {p.removeprefix("resources/images/") for p in files if p.endswith(".png")}
    return catalog, quest_bytes, script_bytes, images


def directions(target):
    hint = target[1] if len(target) > 1 else None
    return copy.deepcopy(hint if isinstance(hint, list) else SWIPES[hint])


def resources(source):
    names = set()

    def patterns(value):
        if isinstance(value, str) and value not in ("press", "default", "intoWorldMap"):
            if not value.startswith("input "):
                names.add(value)
        elif isinstance(value, list):
            for item in value:
                patterns(item)

    patterns(source.get("_EOT"))
    patterns(source.get("_RTT"))
    patterns(source.get("_preEOTcheck"))
    patterns(source.get("_FloorCheck"))
    for target in source["_TARGETINFOLIST"]:
        if target[0] != "position":
            names.add(target[0])
        if len(target) > 2 and isinstance(target[2], str) and target[2].startswith("stair_"):
            names.add(target[2])
            if target[0] in ("harken", "Bharken"):
                names.update(("harken", "Bharken"))
    return sorted(names)


def click(x, y):
    return dict(kind=0, x=x, y=y)


def swipe(coords):
    return dict(kind=1, x=coords[0], y=coords[1], x2=coords[2], y2=coords[3], duration=400)


class Trace:
    def __init__(self, initial):
        self.frames = [copy.deepcopy(initial)]
        self.commands = []
        self.causes = []
        self.focus = {}
        self.combats = 0
        self.chests = 0
        self.route_start = 0
        self.steps = 0
        self.stair_fault_after = None

    def advance(self, command, frame, cause):
        self.commands.append(command)
        self.causes.append(cause)
        self.frames.append(copy.deepcopy(frame))

    def show(self, **templates):
        # Describe the post-state of the preceding real input, not a new frame.
        self.frames[-1].update(templates)

    def tap(self, point, frame, cause):
        self.advance(click(*point), frame, cause)

    def drag(self, coords, frame, cause):
        if coords is not None:
            self.advance(swipe(coords), frame, cause)

    def open_map(self, frame):
        if "mapFlag" in self.frames[-1]:
            self.show(**frame)
        else:
            self.tap((777, 150), frame, "StateDungeon/open-map")

    def battle(self, after):
        # Existing Manual defend contract: actual skill input, then dungeon receipt.
        self.tap((513, 1200), after, "StateCombat/defend -> observed dungeon")
        self.combats += 1

    def chest(self, after):
        self.tap((320, 412), {"whowillopenit": (200, 500)}, "StateChest/open chest")
        self.tap((258, 1161), {"chestOpening": (300, 500)}, "StateChest/select character 1")
        self.tap((515, 934), after, "StateChest/disarm -> observed dungeon")
        self.chests += 1


def entry_trace(source):
    trace = Trace(CITY)
    steps = source["_EOT"]
    first = steps[0][1]
    if first == "intoWorldMap":
        trace.show(intoWorldMap=(100, 700))
    elif first == "EVENT":
        trace.show(EVENT=(200, 500))
    else:
        trace.show(EdgeOfTown=(300, 600))
    pre = source.get("_preEOTcheck")
    if pre:
        after = copy.deepcopy(trace.frames[-1])
        trace.show(**{pre: (300, 500)})
        trace.tap((320, 512), after, "StateEoT/_preEOTcheck " + pre)
    for i, (_, target, fallback, _) in enumerate(steps):
        last = i == len(steps) - 1
        next_name = None if last else steps[i + 1][1]
        if target == "intoWorldMap":
            destination = fallback[0]
            trace.tap((120, 712), {"worldmapflag": (80, 100), destination: (500, 700)},
                      "StateEoT/world map open")
            after = DUNGEON if last else {"openworldmap": (300, 100), next_name: (400, 700)}
            trace.tap((520, 712), after, "StateEoT/world destination " + destination)
        elif target == "EVENT":
            trace.tap((1, 1), {"EVENT": (200, 500)}, "StateEoT/event fallback[0]")
            trace.tap((220, 512), {fallback: (300, 700)}, "StateEoT/event fallback[1]")
            trace.tap((320, 712), {"openworldmap": (300, 100), next_name: (400, 700)},
                      "StateEoT/event destination " + fallback)
        else:
            if i == 0:
                # Old nested fallback runs to completion, including [1,1].
                trace.tap((320, 612), {target: (400, 700)}, "StateEoT/fallback EdgeOfTown")
                tail = fallback[1]
                command = swipe(list(map(int, tail.split()[2:]))) if isinstance(tail, str) else click(*tail)
                trace.advance(command, {target: (400, 700)}, "StateEoT/fallback final action")
            after = {"GotoDung": (500, 900)} if last else {next_name: (400, 700)}
            trace.tap((420, 712), after, "StateEoT/press " + target)
            if last:
                trace.tap((520, 912), DUNGEON, "StateEoT/GotoDung -> observed dungeon")
    trace.route_start = len(trace.commands)
    return trace


def legal_chest_point(task_id, index, target):
    x, y = CHEST_POINTS.get((task_id, index), (450, 500))
    regions = copy.deepcopy(target[2] if len(target) > 2 else None)
    regions = [[0, 0, 900, 1600]] if regions is None else regions
    if regions == "default":
        regions = [[0, 0, 900, 1600]] + EXCLUSIONS
    rx, ry, rw, rh = regions[0]
    if not (rx <= x and ry <= y and x + 40 <= rx + rw and y + 24 <= ry + rh):
        raise FixtureBlocked(f"BLOCKED_CHEST_ROI:{task_id}:{index}: explicit point outside include")
    for rx, ry, rw, rh in regions[1:] + EXCLUSIONS:
        if x < rx + rw and x + 40 > rx and y < ry + rh and y + 24 > ry:
            raise FixtureBlocked(f"BLOCKED_CHEST_ROI:{task_id}:{index}: explicit point overlaps exclusion")
    return x, y


def append_stair_trace(trace, target, case):
    """Fixed CheckIf_harkenStair: original swipe, H six views, then BH six.

    No new route is passed to native: this is the last point of the full catalog
    iteration. The two decoys deliberately test priority, not merely a hit.
    """
    if case not in STAIR_CASES:
        raise ValueError(case)
    name, _, stair = target
    assert name in ("harken", "Bharken") and stair.startswith("stair_")
    outer = directions(target)
    assert len(outer) == 1 and outer[0] is not None
    decoy = {**MAP, "Bharken": (600, 900)}
    if case == "correct":
        other = "Bharken" if name == "harken" else "harken"
        initial = {**MAP, stair: (200, 400), name: (400, 700), other: (600, 900)}
    elif case == "harken_first":
        initial = {**decoy, "harken": (400, 700)}
    elif case in ("harken_last", "failure", "stop", "combat"):
        initial = decoy
    else:
        initial = MAP
    trace.open_map(MAP)
    trace.drag(outer[0], initial, "stair/original target direction BEFORE checking stair")
    if case in ("correct", "harken_first"):
        trace.tap((420, 712), initial, "stair/original target" if case == "correct" else "stair/harken before Bharken")
        trace.tap((136, 1431), OUTSIDE, "stair/real outside; no target-step receipt")
        return
    if case == "combat":
        # An actual interrupted search returns to the same catalog point. The
        # next map is blank, so no pre-combat location can authorize a click.
        trace.drag(SWIPES[None][1], {"combatActive": (10, 5), "spellskill/char/A": (24, 55),
                                   "flee": (750, 1150)}, "stair/first harken swipe interrupted by combat")
        trace.battle(DUNGEON)
        trace.open_map(MAP)
        trace.drag(outer[0], decoy, "stair/re-observe same original point after combat")
    for view, direction in enumerate(SWIPES[None][1:], 1):
        reached = {**decoy, "harken": (400, 700)} if view == 5 else decoy
        screen = MAP if case in ("bharken_last", "missing") else reached
        trace.drag(direction, screen, f"stair/harken view {view}/5 after initial view")
        if view == 1:
            trace.stair_fault_after = len(trace.commands)
    if case in ("bharken_last", "missing"):
        for view, direction in enumerate(SWIPES[None][1:], 1):
            # Harken becomes visible AFTER its full search has failed. The old
            # inner Bharken search must not jump back to it or use its position.
            screen = {**MAP, "harken": (600, 900)} if case == "bharken_last" else MAP
            if case == "bharken_last" and view == 5:
                screen["Bharken"] = (400, 700)
            trace.drag(direction, screen, f"stair/Bharken view {view}/5 after initial view")
    if case != "missing":
        trace.tap((420, 712), trace.frames[-1], "stair/select independently observed fallback target")
        trace.tap((136, 1431), OUTSIDE, "stair/fallback movement -> observed outside")


def make_trace(task_id, source, stair_case=None):
    trace = entry_trace(source)
    targets = source["_TARGETINFOLIST"]
    for index, target in enumerate(targets):
        name = target[0]
        dirs = directions(target)
        final = index == len(targets) - 1
        if stair_case is not None and final:
            assert task_id in WRONG_STAIR_TASKS
            append_stair_trace(trace, target, stair_case)
            return trace
        if name == "chest_auto":
            if "mapFlag" in trace.frames[-1]:
                trace.advance(dict(kind=5, key=4), AUTO, "startAuto/close map")
            else:
                trace.show(**AUTO)
            trace.tap((750, 282), {"chestFlag": (300, 400)}, "startAuto/available chest -> real chest")
            trace.chest(AUTO)
            trace.tap((750, 282), {**DUNGEON, "NoChestCanBeFound": (300, 700)},
                      "startAuto/second search exhausted AFTER chest receipt")
            trace.steps += 1
            continue
        if name == "chest":
            x, y = legal_chest_point(task_id, index, target)
            found = {**MAP, "chest": (x, y)}
            trace.open_map(found if dirs[0] is None else MAP)
            trace.drag(dirs[0], found, f"StateMapSearch/{index}/first search direction")
            trace.tap((x + 20, y + 12), found, "StateMapSearch/select actual chest")
            trace.tap((136, 1431), {"chestFlag": (300, 400)}, "StateMoving/arrive at chest")
            trace.chest(DUNGEON)
            trace.open_map(MAP)
            for direction in dirs:
                trace.drag(direction, MAP, f"StateMapSearch/{index}/exhaust each view after opening")
            trace.steps += 1
            continue
        positional = name == "position" or name.startswith("stair")
        exit_target = name in ("harken", "Bharken", "leaveDung") or name.endswith("_quit")
        view = dict(MAP)
        if not positional:
            view[name] = (400, 700)
        if len(target) > 2 and isinstance(target[2], str) and target[2].startswith("stair_"):
            view[target[2]] = (200, 400)
        if name == "Mark_auto":
            # Case-sensitive old target is an ordinary map pattern, not the
            # lowercase automatic button. First five views miss, sixth hits.
            trace.open_map(MAP)
            for view_index, direction in enumerate(dirs[1:], 1):
                trace.drag(direction, view if view_index == len(dirs) - 1 else MAP,
                           f"StateMapSearch/Mark_auto view {view_index}/5 after initial view")
        else:
            trace.open_map(view if dirs[0] is None else MAP)
            trace.drag(dirs[0], view, f"StateMapSearch/{index}/legacy swipe")
        if positional:
            point = tuple(target[2])
            point = (max(33, min(point[0], 866)), max(33, min(point[1], 1566)))
        else:
            point = (440, 740) if name in ("harken2", "Bharken2", "Mark_auto") else (420, 712)
        trace.tap(point, view, f"StateMapSearch/{index}/select {name}")
        if exit_target or (final and name == "position"):
            trace.tap((136, 1431), OUTSIDE, f"StateMoving/{index}/observed outside, NOT target completion")
            if not final:
                raise FixtureBlocked(f"BLOCKED_EARLY_EXIT:{task_id}:{index}: remaining route cannot be discarded")
            return trace
        combat = name == "position" or not positional
        arrival = {"combatActive": (10, 5), "spellskill/char/A": (24, 55), "flee": (750, 1150)} if combat else DUNGEON
        trace.tap((136, 1431), arrival, f"StateMoving/{index}/{'encounter' if combat else 'stairs'}")
        if combat:
            trace.battle(DUNGEON)
        reached = dict(MAP)
        if name == "position":
            reached["cursor_0"] = (point[0] - 20, point[1] - 12)
        elif positional:
            reached[name] = (200, 400)
        else:
            reached[name] = (400, 700)
        trace.open_map(reached)
        if not positional:
            trace.focus[str(len(trace.frames) - 1)] = [name]
        trace.drag(dirs[0], reached, f"StateMapSearch/{index}/re-observe arrival after movement")
        if not positional:
            trace.focus[str(len(trace.frames) - 1)] = [name]
        trace.steps += 1
    return trace


def scenario(task_id, source, mode):
    stair_case = mode.removeprefix("wrong_stair_") if mode.startswith("wrong_stair_") else None
    if mode in ("failure", "stop"):
        # These are independently executable even if the positive route is blocked.
        trace = entry_trace(source)
        if source["_TARGETINFOLIST"][0][0] == "chest_auto":
            trace.show(**AUTO)
            command = click(750, 282)
        else:
            command = click(777, 150)
        trace.advance(command, {}, "first real dungeon input; failure/stop boundary")
    else:
        trace = make_trace(task_id, source, stair_case)
    options = {}
    expected = dict(state="Completed", completed_business_units=1, task_step=trace.steps,
                    combats=trace.combats, chests=trace.chests, generation=1, crashes=0,
                    dungeons=0, lifecycle_calls=[])
    if mode in ("failure", "stop"):
        count = trace.route_start + 1
        trace.commands = trace.commands[:count]
        trace.causes = trace.causes[:count]
        trace.frames = trace.frames[:count] + [{}]
        trace.focus = {}
        if mode == "failure":
            trace.commands[-1]["reject"] = True
        else:
            options["stop_after_calls"] = count
        expected.update(state="Failed" if mode == "failure" else "UserStopped",
                        completed_business_units=0, task_step=0, combats=0, chests=0)
    elif stair_case in ("failure", "stop"):
        count = trace.stair_fault_after
        assert count is not None
        trace.commands = trace.commands[:count]
        trace.causes = trace.causes[:count]
        trace.frames = trace.frames[:count] + [{}]
        trace.focus = {i: v for i, v in trace.focus.items() if int(i) < count}
        if stair_case == "failure":
            trace.commands[-1]["reject"] = True
        else:
            options["stop_after_calls"] = count
        expected.update(state="Failed" if stair_case == "failure" else "UserStopped", completed_business_units=0)
    elif stair_case == "missing":
        expected.update(state="Interrupted", completed_business_units=0, reason="RECOVERY_REQUIRED",
                        session_reason="navigation.target_missing")
    elif mode == "recovery":
        trace.frames = [{} for _ in range(7)] + trace.frames
        trace.commands = [click(450, 760) for _ in range(6)] + trace.commands
        trace.causes = ["Pause/ineffective resume" for _ in range(6)] + trace.causes
        trace.focus = {str(int(i) + 7): v for i, v in trace.focus.items()}
        options.update(attach_recovery=True, pause_frames=list(range(7)), restart_frame=7, restart_action=6)
        expected.update(generation=2, crashes=1,
                        lifecycle_calls=["EnsureVpn", "StopApplication", "StartApplication"])
    elif mode != "success" and stair_case is None:
        raise ValueError(mode)
    if mode == "failure" or stair_case == "failure":
        expected["reason"] = "CUSTOM_ACTION_FAILED"
    expected["backend_calls"] = len(trace.commands)
    expected["cursor"] = len(trace.frames) - (2 if expected["state"] == "Failed" else 1)
    options["focused_map_templates"] = trace.focus
    return trace, options, expected


def task_requirements(task_id, source, images):
    extra = resources(source)
    missing = [n for n in extra if ALIASES.get(n + ".png", n + ".png") not in images]
    reason = None
    if missing:
        reason = "BLOCKED_BASELINE_IMAGES: " + ", ".join(missing)
    return dict(task_id=task_id, source=source, extra_images=extra, aliases=ALIASES,
                floor=source.get("_FloorCheck"), floor_semantics=[t for t in source["_TARGETINFOLIST"]
                    if t[0].startswith("stair") or (len(t) > 2 and isinstance(t[2], str) and t[2].startswith("stair_"))],
                route_semantics=source["_TARGETINFOLIST"], return_semantics=source.get("_RTT"),
                blocked=reason,
                wrong_stair_validation="NOT_RUN" if task_id in WRONG_STAIR_TASKS else None,
                runtime_status="NOT_RUN", real_status="BLOCKED_NO_DEVICE_AUTHORIZATION")


def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=True, indent=2), encoding="utf-8")


def profile():
    # Explicit supported production settings; no new knobs or callback behavior.
    return dict(DEFAULT_OVERALL_STRATEGY="Manual", TASK_SPECIFIC_CONFIG=False,
                STRATEGY=[dict(group_name="Manual", skill_settings=[dict(role_var=r,
                    skill_var="\u9632\u5fa1", skill_lvl=1, target_var="next", freq_var="\u4fdd\u7559\u539f\u503c")
                    for r in ("A", "B")])],
                WHO_WILL_OPEN_IT=1, QUICK_DISARM_CHEST=False, ACTIVE_REST=False,
                RE_ASSEMBLE_PARTY=False, RECOVER_WHEN_BEGINNING=False,
                SKIP_CHEST_RECOVER=True, SKIP_COMBAT_RECOVER=True, BYPASS_THE_WALL=False)


class DungeonTaskMatrixTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.catalog, qb, sb, cls.images = baseline()
        cls.root = Path(tempfile.mkdtemp(prefix="m4-task-matrix-", dir=ROOT / ".local"))
        cls.quest_path = cls.root / "legacy-quests.json"
        cls.quest_path.write_bytes(qb)
        (cls.root / "legacy-script.py").write_bytes(sb)
        # Import the module, not its TestCase symbol into this module's globals.
        module = importlib.import_module("test_workflow")
        cls.helper = module.WorkflowTests("runTest")
        cls.helper.root = cls.root
        cls.helper.sdk = Path(json.loads((ROOT / ".local/maafw.json").read_text(encoding="utf-8"))["sdk"])
        cls.helper.env = dict(os.environ, PATH=str(cls.helper.sdk / "bin") + os.pathsep + os.environ.get("PATH", ""))
        cls.helper.default_dialogues = sorted(Path(p).stem for p in cls.images if p.startswith("dialogueChoices/"))
        if len(cls.helper.default_dialogues) != 14:
            raise AssertionError("fixed dialogue inventory changed")
        cls.exe = ROOT / "build/m4/Release/test_m4_workflow.exe"
        cls.exe_hash = hashlib.sha256(cls.exe.read_bytes()).hexdigest()
        write_json(cls.root / "baseline.json", dict(commit=BASELINE, blobs=BLOBS, exe_sha256=cls.exe_hash))
        print("M4 task matrix evidence: " + str(cls.root), flush=True)

    def run_task(self, task_id, mode):
        source = self.catalog[task_id]
        name = f"{task_id}--{mode}"
        requirement = task_requirements(task_id, source, self.images)
        record_path = self.root / (name + "-expectation.json")
        write_json(record_path, requirement)
        try:
            if requirement["blocked"]:
                raise FixtureBlocked(requirement["blocked"])
            trace, options, expected = scenario(task_id, source, mode)
            requirement.update(frames=trace.frames, transitions=trace.commands, causes=trace.causes,
                               options=options, expected=expected)
            write_json(record_path, requirement)
            self.assertEqual(hashlib.sha256(self.exe.read_bytes()).hexdigest(), self.exe_hash,
                             "MATRIX_EXE_CHANGED: stop the serial batch")
            result = self.helper.execute(name, trace.frames, trace.commands, workflow="iteration",
                catalog_task_id=task_id, quest_catalog=str(self.quest_path), normal_units=1,
                profile=profile(), aliases=ALIASES, extra_images=requirement["extra_images"],
                large_templates=["harken2", "Bharken2"] + (["mark_auto"] if task_id == "White-G-v2" else []), **options)
            self.assertEqual(hashlib.sha256(self.exe.read_bytes()).hexdigest(), self.exe_hash)
            self.assertEqual(result["task_plan"]["task_id"], task_id)
            self.assertEqual(result["task_plan"]["source"], source)
            self.assertEqual(result["task_plan"]["floor"], source.get("_FloorCheck"))
            self.assertEqual(result["kind"], "tasks.dungeon_iteration." + task_id)
            self.assertFalse(result["mismatch"], result.get("mismatch_detail"))
            self.assertEqual(result["backend_calls"], expected["backend_calls"])
            self.assertEqual(result["cursor"], expected["cursor"])
            self.assertEqual(result["lifecycle_calls"], expected["lifecycle_calls"])
            self.assertEqual(result["time_event_count"], 0)
            snapshot = result["snapshot"]
            self.assertTrue(snapshot["quiescent"])
            self.assertTrue(snapshot["result_saved"])
            self.assertFalse(snapshot["storage_error"])
            for field in ("state", "completed_business_units", "generation"):
                self.assertEqual(snapshot[field], expected[field], field)
            for field in ("task_step", "chests", "combats", "crashes", "dungeons"):
                self.assertEqual(snapshot["business"][field], expected[field], field)
            self.assertEqual(snapshot["inputs"]["backend_called"], len(trace.commands))
            self.assertEqual(sum(s["inputs"]["backend_called"] for s in snapshot["sessions"]), len(trace.commands))
            if "reason" in expected:
                self.assertEqual(snapshot["reason"], expected["reason"])
            if "session_reason" in expected:
                self.assertEqual(snapshot["sessions"][-1]["reason"], expected["session_reason"])
            if expected["state"] == "Failed":
                # accepted counts gate admission, not backend success.
                self.assertEqual(snapshot["inputs"]["accepted"], len(trace.commands))
            if mode == "recovery":
                self.assertEqual(snapshot["sessions"][0]["reason"], "pause.physics_frozen")
                self.assertEqual(snapshot["sessions"][0]["business"]["chests"], 0)
                self.assertEqual(snapshot["sessions"][0]["business"]["combats"], 0)
                self.assertEqual(snapshot["sessions"][0]["inputs"]["backend_called"], 6)
                self.assertFalse(snapshot["business"]["lifecycle_recovery_active"])
            saved = list((self.root / name / "run").rglob("result.json"))
            self.assertEqual(len(saved), 1)
            persisted = json.loads(saved[0].read_text(encoding="utf-8"))
            for field in ("state", "reason", "business", "inputs", "generation", "completed_business_units"):
                self.assertEqual(persisted[field], snapshot[field], "persisted " + field)
            if expected["state"] == "Completed":
                terminal = persisted["root_terminal"]
                self.assertGreater(persisted["root_task_id"], 0)
                self.assertEqual(terminal["task_id"], persisted["root_task_id"])
                self.assertEqual(terminal["generation"], snapshot["generation"])
                self.assertEqual(terminal["depth"], 0)
                self.assertTrue(terminal["node"])
            requirement["runtime_status"] = "SCENARIO_PASS_NOT_FULL_TASK_ACCEPTANCE"
        except FixtureBlocked as exc:
            requirement.update(runtime_status="BLOCKED", blocked=str(exc))
            raise
        except Exception as exc:
            requirement.update(runtime_status="FAIL_OR_ERROR", error=str(exc))
            raise
        finally:
            write_json(record_path, requirement)


def modes_for(task_id):
    modes = ["success", "failure", "stop", "recovery"]
    if task_id in WRONG_STAIR_TASKS:
        modes += ["wrong_stair_" + case for case in STAIR_CASES]
    return modes


def install_tests():
    for task_id in TASK_IDS:
        for mode in modes_for(task_id):
            def test(self, task_id=task_id, mode=mode):
                self.run_task(task_id, mode)
            test.__doc__ = f"{task_id}: {mode}; full fixed catalog definition, causal native execution."
            setattr(DungeonTaskMatrixTests, "test_" + task_id.replace("-", "_") + "__" + mode, test)


install_tests()


def prepare():
    catalog, qb, sb, images = baseline()
    root = Path(tempfile.mkdtemp(prefix="m4-task-matrix-prepare-", dir=ROOT / ".local"))
    (root / "legacy-quests.json").write_bytes(qb)
    (root / "legacy-script.py").write_bytes(sb)
    rows = []
    for task_id in TASK_IDS:
        row = task_requirements(task_id, catalog[task_id], images)
        row["scenarios"] = {}
        for mode in modes_for(task_id):
            try:
                if row["blocked"]:
                    raise FixtureBlocked(row["blocked"])
                trace, options, expected = scenario(task_id, catalog[task_id], mode)
                data = dict(frames=trace.frames, transitions=trace.commands, causes=trace.causes,
                            options=options, expected=expected, runtime_status="NOT_RUN")
                assert len(trace.causes) == len(trace.commands)
                assert all("stay" not in c for c in trace.commands)
                # Recovery's extra frame has one explicit StartApplication edge.
                assert len(trace.frames) == len(trace.commands) + (2 if mode == "recovery" else 1)
                row["scenarios"][mode] = data
            except FixtureBlocked as exc:
                row["scenarios"][mode] = dict(runtime_status="BLOCKED", reason=str(exc))
        write_json(root / (task_id + ".json"), row)
        rows.append(dict(task_id=task_id, scenarios={m: v["runtime_status"] for m, v in row["scenarios"].items()}))
    write_json(root / "index.json", dict(baseline=BASELINE, blobs=BLOBS, task_count=43,
               native_executed=False, accepted_tasks=0, scenarios=rows))
    print("PREPARED_ONLY, native_executed=false, accepted_tasks=0: " + str(root))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prepare", action="store_true", help="write traces only, never invoke native")
    args, remaining = parser.parse_known_args()
    if args.prepare:
        if remaining:
            parser.error("unexpected arguments with --prepare")
        prepare()
    else:
        unittest.main(argv=[__file__] + remaining)
