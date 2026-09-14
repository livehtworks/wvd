"""生成当前迁移台账，不生成业务代码，也不把目录解析结果提升为执行通过。"""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return json.loads(path.read_text(encoding="utf-8"))


def write(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def generate(data_evidence, state_evidence=None, plan_evidence=None, workflow_evidence=None):
    folder = ROOT / "docs/migration"
    inventory = read(folder / "feature_inventory.json")["items"]
    previous = {x["inventory_id"]: x for x in read(folder / "m3-implementation-map.json")["entries"]}
    tasks_result = read(data_evidence / "tasks/result.json")
    source_tasks = read(ROOT / "packs/wvd/parameters/legacy-quests.json")
    if tasks_result.get("outcome") != "PASS" or tasks_result.get("quests") != source_tasks:
        raise ValueError("M4_DATA_EVIDENCE_MISMATCH")
    if tasks_result.get("execution_available") is not False:
        raise ValueError("M4_DATA_RESULT_IS_NOT_EXECUTION")
    plans = {}
    if plan_evidence is not None:
        parsed = read(plan_evidence / "all/result.json")
        plans = {p["task_id"]: p for p in parsed.get("plans", [])}
        if parsed.get("outcome") != "PASS" or set(plans) != set(source_tasks):
            raise ValueError("M4_PLAN_EVIDENCE_MISMATCH")
        for task_id, plan in plans.items():
            if plan["source"] != source_tasks[task_id] or plan["pipeline_available"]:
                raise ValueError("M4_PLAN_NOT_FULL_EXECUTION")
    data_functions = {
        "LoadConfig": ("native/storage/legacy_import.cpp", "LegacyConfigImporter::parse"),
        "LoadRawConfigFromFile": ("native/storage/legacy_import.cpp", "parse_legacy_json / import_copy"),
        "SaveConfigToFile": ("native/storage/profile_store.cpp", "ProfileStore::create / compare_exchange"),
        "_build_quest_data": ("native/storage/legacy_import.cpp", "merge_legacy_quests"),
        "FarmConfig.__init__": ("native/storage/legacy_import.cpp", "LegacyConfigImporter::parse"),
    }
    state_functions = {}
    workflow_functions = {}
    if workflow_evidence is not None:
        executable = ROOT / "build/m4/Release/test_m4_workflow.exe"
        expected_hash = hashlib.sha256(executable.read_bytes()).hexdigest()
        expected_cases = {
            "city-0": ("Completed", 1), "city-4": ("Completed", 5), "arrived": ("Completed", 0),
            "no-world": ("Interrupted", 0), "clipped": ("Completed", 2),
            "inn-False-False": ("Completed", 5), "inn-True-True": ("Completed", 5),
            "inn-True-False": ("Completed", 5), "reject": ("Failed", 1),
            "city-budget": ("Interrupted", 25), "auto-enabled": ("Completed", 0),
            "auto-close": ("Completed", 2), "auto-cancel": ("Completed", 2),
            "auto-back": ("Completed", 2), "auto-unknown-True": ("Completed", 1),
            "auto-unknown-False": ("Interrupted", 1), "auto-popup-stuck": ("Interrupted", 3),
        }
        for case, (state, calls) in expected_cases.items():
            result = read(workflow_evidence / case / "output.json")
            execution = read(workflow_evidence / case / "execution.json")
            if (execution["exit"] != 0 or execution["exe_sha256"] != expected_hash or
                    result["snapshot"]["state"] != state or result["backend_calls"] != calls or
                    result["mismatch"] or not result["snapshot"]["quiescent"]):
                raise ValueError("M4_WORKFLOW_EVIDENCE_MISMATCH:" + case)
        workflow_functions = {
            "Factory.PressWorldMapTargetArea": ("native/games/wvd/navigation/world_map.cpp", "navigation::enter_city"),
            "Factory.WaitWorldMapTargetEntered": ("native/games/wvd/navigation/world_map.cpp", "navigation::enter_city"),
            "Factory.StateInn": ("native/games/wvd/supply/inn.cpp", "supply::rest_at_inn"),
            "Factory.StateCombat.DismissSkillPopupBeforeAuto": ("native/games/wvd/combat/auto_combat.cpp", "combat::enable_auto"),
            "Factory.StateCombat.ActiveAutoCombat": ("native/games/wvd/combat/auto_combat.cpp", "combat::enable_auto"),
        }
    if state_evidence is not None:
        normal = read(state_evidence / "normal/result.json")
        for name in ("snapshot", "second"):
            if normal[name]["state"] != "Completed" or normal[name]["completed_business_units"] != 2:
                raise ValueError("M4_STATE_EVIDENCE_MISMATCH")
        if normal["backend_inputs"] or normal["real_connections"] or normal["real_inputs"]:
            raise ValueError("M4_STATE_EXPECTED_OFFLINE_ONLY")
        state_functions = {
            "Factory.ReloadStrategy": ("native/games/wvd/combat/strategy.cpp", "CombatStrategy::reload"),
            "Factory.StateCombat": ("native/games/wvd/combat/strategy.cpp", "CombatStrategy::select / consume"),
            "Factory.StateDungeon": ("native/games/wvd/state.cpp", "WvdRunState::resume_dungeon / enter_dungeon"),
            "Factory.StateDungeon.TargetPointComplete": ("native/games/wvd/state.cpp", "WvdRunState::target_point_completed"),
            "Factory.DungeonCompletionCounter": ("native/games/wvd/state.cpp", "WvdRunState::dungeon_completed"),
            "Factory.restartGame": ("native/games/wvd/state.cpp", "WvdRunState::restart_game"),
        }
    entries = []
    task_entries = []
    for old in inventory:
        if old["kind"] not in ("function", "config", "task"):
            continue
        item = {"inventory_id": old["id"], "kind": old["kind"], "legacy_symbol": old["legacy_symbol"],
                "source_ref": old["source_ref"], "source_body_sha256": old.get("body_sha256"),
                "original_semantics": old["original_semantics"], "acceptance_ids": old["acceptance_ids"],
                "planned_owner_from_baseline": old["new_owner"], "implementation": None, "entry": None,
                "implementation_status": "NOT_STARTED", "offline_status": "NOT_RUN",
                "real_status": "UNVERIFIED", "release_allowed": False,
                "remaining": "本轮未承接此完整业务；固定旧入口仍为唯一生产实现。"}
        if old["id"] in previous:
            visual = previous[old["id"]]
            item.update(implementation=visual["implementation"], entry=visual["entry"],
                        implementation_status="PARTIAL", implementation_extent="M3_VISUAL_ONLY", remaining=visual["remaining"],
                        quality_status=visual["quality_status"], evidence_report="../m3-fix-validation.md")
        if old["legacy_symbol"] in data_functions:
            path, entry = data_functions[old["legacy_symbol"]]
            item.update(implementation=path, entry=entry, implementation_status="PARTIAL", implementation_extent="DATA_ONLY",
                        offline_status="PASS", verification_scope="离线数据解析/保存，不是完整业务",
                        remaining="离线导入/保存已接入检查入口；旧 GUI、任务状态和业务写回尚未承接。")
        if old["legacy_symbol"] in state_functions:
            path, entry = state_functions[old["legacy_symbol"]]
            item.update(implementation=path, entry=entry, implementation_status="PARTIAL", implementation_extent="STATE_ONLY",
                        offline_status="PASS", verification_scope="状态 API 与真实 Maa 有限段，不是游戏动作",
                        evidence_report="../m4-state-validation.md",
                        remaining="状态消费/重置已迁移，实际导航/战斗/恢复输入及完整业务出口尚未承接。")
        if plans and old["legacy_symbol"] in ("LoadQuest", "TargetInfo.__init__", "TargetInfo.swipeDir", "TargetInfo.roi"):
            item.update(implementation="native/games/wvd/tasks/task_plan.cpp", entry="WvdTaskPlan::parse",
                        implementation_status="PARTIAL", implementation_extent="TYPED_PLAN_DATA",
                        offline_status="PASS", verification_scope="纯任务数据解析，未编译执行图",
                        evidence_report="../m4-plan-validation.md", remaining="业务 Pipeline、资源绑定和专项 case 未完成。")
        if old["legacy_symbol"] in workflow_functions:
            path, entry = workflow_functions[old["legacy_symbol"]]
            item.update(implementation=path, entry=entry, implementation_status="PARTIAL",
                        implementation_extent="FINITE_WORKFLOW_SUBSET", offline_status="PASS",
                        verification_scope="真实 Maa 因果离线子流程，不是完整任务或所有旧分支",
                        evidence_report="../m4-workflow-validation.md",
                        remaining="子流程通过；外层任务调度、全部原分支和恢复生命周期尚未全部接入。")
        if old["kind"] == "config":
            item.update(implementation="native/storage/legacy_import.cpp", entry="LegacyConfigImporter::parse",
                        implementation_status="PARTIAL", implementation_extent="DATA_BOUND", offline_status="PASS",
                        verification_scope="33 字段数据，不代表所有字段已有业务消费者",
                        remaining="状态工厂参数可随 Run 冻结；完整业务消费与写回未实现。" if state_functions else "业务消费与写回未实现。",
                        data_evidence="../m4-business-validation.md#配置与目录")
        if old["kind"] == "task":
            task_id = old["task_id"]
            source = source_tasks[task_id]
            if source != old["data"]:
                raise ValueError("TASK_BASELINE_CHANGED: " + task_id)
            item.update(implementation="native/games/wvd/tasks/quest_catalog.cpp", entry="WvdQuestCatalog",
                        implementation_status="PARTIAL", implementation_extent="DATA_BOUND_ONLY",
                        remaining="未实现任务计划、编译入口及完整业务执行。")
            task_entries.append({"task_id": task_id, "legacy_type": source["_TYPE"],
                "legacy_title": old["task_title"], "source_ref": old["source_ref"],
                "source_data_sha256_canonical_json": hashlib.sha256(json.dumps(source, ensure_ascii=False,
                    sort_keys=True, separators=(",", ":")).encode()).hexdigest(),
                "original_acceptance_ids": old["acceptance_ids"], "implementation_status": "PARTIAL", "implementation_extent": "DATA_BOUND_ONLY",
                "data_binding_status": "PASS", "plan_status": "NOT_IMPLEMENTED", "offline_status": "NOT_RUN",
                "real_status": "UNVERIFIED", "release_allowed": False, "new_entry": None,
                "new_files": ["native/games/wvd/tasks/quest_catalog.cpp", "packs/wvd/parameters/legacy-quests.json"],
                "source_fields": list(source), "success_evidence": [], "failure_stop_recovery_evidence": [],
                "data_evidence": "../m4-business-validation.md#配置与目录", "semantic_differences": [],
                "blockers": ([] if state_functions else ["WVD_RUN_STATE_NOT_IMPLEMENTED"]) +
                            ["TASK_PLAN_NOT_IMPLEMENTED", "BUSINESS_ACTIONS_NOT_IMPLEMENTED"]})
            if plans:
                item.update(implementation="native/games/wvd/tasks/task_plan.cpp", entry="WvdTaskPlan::parse",
                            implementation_extent="TYPED_PLAN_DATA", remaining="任务数据已类型化，执行图和业务动作未实现。")
                task_entries[-1].update(implementation_extent="TYPED_PLAN_DATA", plan_status="PARTIAL",
                    plan_data_status="PASS", plan_evidence="../m4-plan-validation.md",
                    blockers=["PIPELINE_COMPILER_NOT_IMPLEMENTED", "BUSINESS_ACTIONS_NOT_IMPLEMENTED"] +
                             (["SPECIAL_CASE_NOT_IMPLEMENTED"] if source["_TYPE"] == "quest" else []))
                task_entries[-1]["new_files"].append("native/games/wvd/tasks/task_plan.cpp")
                if workflow_functions:
                    task_entries[-1]["blockers"][0] = "FULL_TASK_PIPELINE_NOT_IMPLEMENTED"
        entries.append(item)
    counts = {kind: sum(x["kind"] == kind for x in entries) for kind in ("function", "config", "task")}
    if counts != {"function": 250, "config": 33, "task": 58}:
        raise ValueError("M4_INVENTORY_DENOMINATOR_CHANGED")
    common = {"schema": 1, "implementation_base": "4d7c4fabd29bfadf55963bfdaecf79e3b6582bdd",
              "legacy_base": "6585f4075f5714ab522aa582993860c09af912c1", "release_allowed": False}
    write(folder / "m4-implementation-map.json", {**common, "counts": counts,
          "semantic_review_complete": False, "status": "M4_PARTIAL_IMPLEMENTATION", "entries": entries})
    write(folder / "m4-task-status.json", {**common, "expected_counts": {"total": 58, "dungeon": 43, "quest": 15},
          "execution_passed": 0, "items": task_entries})
    print("341 entries retained; 58 task data bindings verified; 0 task executions claimed.")


def verify_workflows(folder, expected):
    executable_hash = hashlib.sha256((ROOT / "build/m4/Release/test_m4_workflow.exe").read_bytes()).hexdigest()
    for name, (state, calls) in expected.items():
        result = read(folder / name / "output.json")
        execution = read(folder / name / "execution.json")
        if (execution != {"exe_sha256": executable_hash, "exit": 0} or result["snapshot"]["state"] != state
                or result["backend_calls"] != calls or result["mismatch"] or not result["snapshot"]["quiescent"]):
            raise ValueError("M4_WORKFLOW_EVIDENCE_MISMATCH:" + name)


def update_navigation(workflow_evidence, plan_evidence):
    """入本图与完整任务分别登记；不覆盖已有战斗等阶段证据。"""
    verify_workflows(workflow_evidence, {
        "auto-route-success": ("Completed", 2), "auto-route-disabled": ("Interrupted", 1),
        "auto-route-battle": ("Interrupted", 1), "auto-route-stay": ("Interrupted", 0),
        "auto-route-retreat": ("Completed", 1), "auto-route-mark": ("Completed", 1),
        "auto-route-stopped": ("Interrupted", 1), "entry-sequence": ("Completed", 5),
        "entry-not-entered": ("Interrupted", 1), "entry-event": ("Completed", 4),
        "entry-event-fail": ("Failed", 1), "entry-world": ("Completed", 5),
    })
    compiled = read(plan_evidence / "entries/result.json")
    execution = read(plan_evidence / "entries/execution.json")
    exe_hash = hashlib.sha256((ROOT / "build/m4/Release/wvd_m4_check.exe").read_bytes()).hexdigest()
    source = read(ROOT / "packs/wvd/parameters/legacy-quests.json")
    entries = {v["task_id"]: v for v in compiled["compiled_entries"]}
    if (execution != {"exe_sha256": exe_hash, "exit": 0} or compiled["outcome"] != "PASS"
            or set(entries) != {k for k, v in source.items() if v["_TYPE"] == "dungeon"}
            or any(v["executed"] or v["missing_images"] or v["scope"] != "ENTRY_ONLY_NOT_FULL_TASK" for v in entries.values())):
        raise ValueError("M4_ENTRY_COMPILATION_EVIDENCE_MISMATCH")
    folder = ROOT / "docs/migration"
    document = read(folder / "m4-implementation-map.json")
    targets = {
        "Factory.StateEoT": ("navigation/dungeon_entry.cpp", "navigation::enter_dungeon"),
        "Factory.StateEoT.EoTStep": ("navigation/dungeon_entry.cpp", "navigation::enter_dungeon"),
        "Factory.StateDungeon.startAuto": ("navigation/auto_route.cpp", "navigation::auto_route"),
    }
    found = set()
    for row in document["entries"]:
        if row["legacy_symbol"] in targets:
            found.add(row["legacy_symbol"])
            file, entry = targets[row["legacy_symbol"]]
            row.update(implementation="native/games/wvd/" + file, entry=entry,
                       implementation_status="PARTIAL", implementation_extent="FINITE_ENTRY_OR_AUTO_ROUTE",
                       offline_status="PASS", verification_scope="有限入本/自动寻路子流程，不是全任务执行",
                       evidence_report="../m4-entry-validation.md",
                       remaining="外层任务推进、普通插入回归、补给和恢复尚未全部连接；真实质量未验。")
    if found != set(targets):
        raise ValueError("M4_ENTRY_INVENTORY_MISMATCH:" + str(found))
    task_document = read(folder / "m4-task-status.json")
    if {r["task_id"] for r in task_document["items"]} != set(source):
        raise ValueError("M4_TASK_DENOMINATOR_MISMATCH")
    for row in task_document["items"]:
        if row["task_id"] in entries:
            row["entry_compilation"] = {"status": "PASS", "scope": "ENTRY_ONLY_NOT_FULL_TASK",
                "entry": "navigation::enter_dungeon", "executed": False,
                "evidence_report": "../m4-entry-validation.md"}
    write(folder / "m4-implementation-map.json", document)
    write(folder / "m4-task-status.json", task_document)
    print("3 navigation entries and 43 entry-only compilation records updated; full-task statuses unchanged.")


def update_combat(combat_evidence):
    """只覆盖已验证的战斗子流程行，保留其他阶段证据及全部任务分母。"""
    expected = {
        "turn-enemy": ("Completed", 3), "turn-aoe": ("Completed", 3), "turn-support": ("Completed", 3),
        "turn-auto": ("Completed", 4), "turn-unmatched": ("Completed", 2), "turn-low-edge": ("Completed", 3),
        "turn-fail-False": ("Failed", 1), "turn-fail-True": ("UserStopped", 1), "turn-stale-actor": ("Failed", 2),
        "turn-defend": ("Completed", 2), "turn-resource-False": ("Completed", 7),
        "turn-resource-True": ("Interrupted", 7), "turn-three-open": ("Completed", 5),
    }
    verify_workflows(combat_evidence, expected)
    path = ROOT / "docs/migration/m4-implementation-map.json"
    document = read(path)
    targets = {"Factory.StateCombat", "Factory.StateCombat.AutoThisChar", "Factory.StateCombat.AutoThisCharAfterTargetFailure",
               "Factory.StateCombat.PressCombatTargetArea", "Factory.StateCombat.SkillLvlSelectAndDoubleCheck"}
    found = set()
    for row in document["entries"]:
        if row["legacy_symbol"] in targets:
            found.add(row["legacy_symbol"])
            row.update(implementation="native/games/wvd/combat/turn.cpp", entry="combat::take_turn",
                       implementation_status="PARTIAL", implementation_extent="FINITE_COMBAT_TURN",
                       offline_status="PASS", verification_scope="真实 Maa 的单角色有限动作及策略消费，不是整场战斗或完整任务",
                       evidence_report="../m4-combat-turn-validation.md",
                       remaining="外层战斗循环、Pause/复活/死亡恢复及完整任务仍待承接；真实目标质量未验。")
    if found != targets or document["counts"] != {"function": 250, "config": 33, "task": 58}:
        raise ValueError("M4_COMBAT_INVENTORY_MISMATCH")
    write(path, document)
    print("5 combat entries updated; 58 complete task statuses unchanged.")


def update_encounter(evidence):
    """记录有界遭遇的实际调用/计数证据，不将组合子流程升级为完整任务。"""
    verify_workflows(evidence, {
        "encounter-two-actors": ("Completed", 4), "encounter-three-turns": ("Completed", 3),
        "encounter-budget": ("Interrupted", 1), "encounter-reject": ("Failed", 1),
        "encounter-to-chest": ("Completed", 1), "encounter-ended-before-auto": ("Completed", 2),
        "encounter-auto-ended": ("Completed", 1), "state-two-encounters": ("Completed", 6),
    })
    repeated = read(evidence / "encounter-three-turns/output.json")["snapshot"]["business"]
    if repeated["combats"] != 1 or repeated["strategy"]["current"]["skill_settings"]:
        raise ValueError("M4_ENCOUNTER_STATE_MISMATCH")
    path = ROOT / "docs/migration/m4-implementation-map.json"
    document = read(path)
    rows = [row for row in document["entries"] if row["legacy_symbol"] == "Factory.StateCombat"]
    if len(rows) != 1 or document["counts"] != {"function": 250, "config": 33, "task": 58}:
        raise ValueError("M4_ENCOUNTER_INVENTORY_MISMATCH")
    rows[0].update(implementation="native/games/wvd/combat/turn.cpp", entry="combat::take_turn",
                  supporting_implementations=["native/games/wvd/combat/encounter.cpp"],
                  implementation_status="PARTIAL", implementation_extent="FINITE_COMBAT_TURN_AND_ENCOUNTER",
                  offline_status="PASS", verification_scope="真实 Maa 单角色动作、有界遭遇组合和连续计数；不是完整副本",
                  evidence_report="../m4-native-child-validation.md",
                  remaining="Pause/复活/死亡恢复、完整副本与专项仍未接齐；真实目标质量未验。")
    write(path, document)
    print("Finite encounter evidence updated; 58 complete task statuses unchanged.")


def update_healing(evidence):
    """仅为 StateDungeon 的角色恢复子链登记证据，不改变完整任务分母。"""
    expected = {
        "heal-not-needed": ("Completed", 0), "heal-initial-False": ("Completed", 4),
        "heal-initial-True": ("Completed", 4), "heal-rotate-seek": ("Completed", 7),
        "heal-stop-False": ("Failed", 1), "heal-stop-True": ("UserStopped", 1),
        "heal-back-bounded": ("Interrupted", 6), "heal-unknown-panel": ("Failed", 1),
        **{"heal-interrupted-" + name: ("Interrupted", 1) for name in ("combatActive", "chestFlag", "RiseAgain")},
    }
    verify_workflows(evidence, expected)
    for name in expected:
        state = read(evidence / name / "output.json")["snapshot"]["business"]
        completed = name in ("heal-not-needed", "heal-initial-False", "heal-initial-True", "heal-rotate-seek")
        if state["healing_required"] == completed or state["healing_sequence"] != (0 if name == "heal-not-needed" else 1):
            raise ValueError("M4_HEALING_STATE_MISMATCH:" + name)
    path = ROOT / "docs/migration/m4-implementation-map.json"
    document = read(path)
    rows = [row for row in document["entries"] if row["legacy_symbol"] == "Factory.StateDungeon"]
    if len(rows) != 1 or document["counts"] != {"function": 250, "config": 33, "task": 58}:
        raise ValueError("M4_HEALING_INVENTORY_MISMATCH")
    rows[0].update(implementation="native/games/wvd/state.cpp", entry="WvdRunState::resume_dungeon / enter_dungeon",
        supporting_implementations=["native/games/wvd/supply/dungeon_recover.cpp"],
        implementation_status="PARTIAL", implementation_extent="STATE_AND_HEALING_SUBFLOW",
        offline_status="PASS", verification_scope="角色面板恢复的真实 Maa 子链及状态，不是完整地下城任务",
        evidence_report="../m4-healing-validation.md", remaining="完整路线、死亡/Pause、恢复调度及全部专项仍待接齐。")
    write(path, document)
    print("Healing subflow evidence updated; 58 complete task statuses unchanged.")


def update_dungeon_route(evidence, plan_evidence):
    """区分通用路线因果验证、43 条路线静态绑定与尚未完成的完整任务。"""
    verify_workflows(evidence, {
        "route-combat": ("Completed", 3), "route-chest-combat": ("Completed", 4),
        "route-combat-heal": ("Completed", 6), "route-input-rejected": ("Failed", 1),
        "route-map-initial-heal": ("Completed", 0), "route-Inn": ("Completed", 0),
        "route-RiseAgain": ("Interrupted", 0), "route-exit-prompt": ("Completed", 2),
    })
    state = read(evidence / "route-combat-heal/output.json")["snapshot"]["business"]
    both = read(evidence / "route-chest-combat/output.json")["snapshot"]["business"]
    if state["task_step"] != 1 or state["healing_required"] or state["combats"] != 1 or state["healing_sequence"] != 1:
        raise ValueError("M4_DUNGEON_HEALING_EVIDENCE_MISMATCH")
    if both["task_step"] != 1 or both["combats"] != 1 or both["chests"] != 1:
        raise ValueError("M4_DUNGEON_ENCOUNTER_EVIDENCE_MISMATCH")
    compiled = read(plan_evidence / "routes/result.json")
    execution = read(plan_evidence / "routes/execution.json")
    exe_hash = hashlib.sha256((ROOT / "build/m4/Release/wvd_m4_check.exe").read_bytes()).hexdigest()
    source = read(ROOT / "packs/wvd/parameters/legacy-quests.json")
    routes = {row["task_id"]: row for row in compiled["compiled_routes"]}
    if (execution != {"exe_sha256": exe_hash, "exit": 0} or compiled["outcome"] != "PASS"
            or set(routes) != {key for key, value in source.items() if value["_TYPE"] == "dungeon"}
            or any(row["executed"] or row["missing_images"] or row["scope"] != "DUNGEON_ROUTE_ONLY_NOT_FULL_TASK" for row in routes.values())):
        raise ValueError("M4_DUNGEON_ROUTE_COMPILATION_MISMATCH")
    folder = ROOT / "docs/migration"
    document = read(folder / "m4-implementation-map.json")
    rows = [row for row in document["entries"] if row["legacy_symbol"] == "Factory.StateDungeon"]
    if len(rows) != 1 or document["counts"] != {"function": 250, "config": 33, "task": 58}:
        raise ValueError("M4_DUNGEON_ROUTE_INVENTORY_MISMATCH")
    rows[0].update(implementation="native/games/wvd/tasks/dungeon_route.cpp", entry="tasks::traverse_dungeon",
        supporting_implementations=["native/games/wvd/state.cpp", "native/games/wvd/supply/dungeon_recover.cpp",
            "native/games/wvd/combat/encounter.cpp", "native/games/wvd/chest/chest.cpp"],
        implementation_status="PARTIAL", implementation_extent="DUNGEON_ROUTE_COMPOSITION",
        offline_status="PASS", verification_scope="路线与遭遇/角色恢复组合的因果验证；不是43条完整任务执行",
        evidence_report="../m4-dungeon-route-validation.md",
        remaining="完整入本/回城/住宿循环、死亡/Pause、恢复调度和其余特殊对话尚未齐。")
    task_document = read(folder / "m4-task-status.json")
    if {row["task_id"] for row in task_document["items"]} != set(source):
        raise ValueError("M4_TASK_DENOMINATOR_MISMATCH")
    for row in task_document["items"]:
        if row["task_id"] not in routes:
            continue
        row["route_compilation"] = {"status": "PASS", "scope": "DUNGEON_ROUTE_ONLY_NOT_FULL_TASK",
            "entry": "tasks::traverse_dungeon", "executed": False,
            "evidence_report": "../m4-dungeon-route-validation.md"}
        row["implementation_extent"] = "TYPED_DATA_AND_DUNGEON_ROUTE"
        if "native/games/wvd/tasks/dungeon_route.cpp" not in row["new_files"]:
            row["new_files"].append("native/games/wvd/tasks/dungeon_route.cpp")
        row["blockers"] = ["FULL_TASK_PIPELINE_NOT_IMPLEMENTED", "FULL_LIFECYCLE_AND_DIALOGS_INCOMPLETE", "FULL_TASK_EXECUTION_NOT_VERIFIED"]
    write(folder / "m4-implementation-map.json", document)
    write(folder / "m4-task-status.json", task_document)
    print("43 dungeon route graphs recorded; complete task execution statuses unchanged.")


def update_common(evidence):
    verify_workflows(evidence, {"common-download-blocked": ("Interrupted", 0), "common-iteration": ("Completed", 3),
        "common-reject": ("Failed", 1), "common-resume": ("Completed", 1), "common-retry-blank": ("Completed", 1),
        "common-retry-low": ("Completed", 2), "common-route": ("Completed", 1), "common-stop": ("UserStopped", 1),
        "common-stuck": ("Interrupted", 6), "common-title": ("Completed", 3), "common-unknown": ("Interrupted", 0)})
    missing = read(evidence / "common-missing/output.json")
    if not missing["publish_error"].startswith("COMPILE_IMAGE_NOT_IN_MANIFEST") or missing["connections"] or missing["backend_calls"]:
        raise ValueError("M4_COMMON_ASSET_REJECTION_MISMATCH")
    for name in ("common-iteration", "common-route"):
        output = read(evidence / name / "output.json")
        if output["lifecycle_calls"] or output["snapshot"]["generation"] != 1 or output["snapshot"]["business"]["crashes"]:
            raise ValueError("M4_COMMON_NOT_ORDINARY_INSERT:" + name)
    path = ROOT / "docs/migration/m4-implementation-map.json"
    document = read(path)
    symbols = {"Factory.TryHandleCommonBlockingScreen", "Factory.TryPressRetry"}
    found = set()
    for row in document["entries"]:
        if row["legacy_symbol"] in symbols:
            found.add(row["legacy_symbol"])
            row.update(implementation="native/games/wvd/recovery/boot.cpp", entry="recovery::clear_common_screens",
                supporting_implementations=["native/games/wvd/vision/boot_probes.hpp"],
                implementation_status="PARTIAL", implementation_extent="COMMON_SCREEN_DISPATCH",
                offline_status="PASS", evidence_report="../m4-common-screen-validation.md",
                verification_scope="正常迭代/路线分派点的同代次阻塞处理，不是所有子动作中途插入",
                remaining="子动作中途弹窗打断、死亡/Pause/对话及全任务恢复仍待接齐。")
    if found != symbols or document["counts"] != {"function": 250, "config": 33, "task": 58}:
        raise ValueError("M4_COMMON_INVENTORY_MISMATCH")
    write(path, document)
    print("Common blocking screens recorded; complete task execution statuses unchanged.")


def update_interruption(evidence):
    verify_workflows(evidence, {"interrupt-map": ("Completed", 2), "interrupt-skill": ("Interrupted", 1),
        "interrupt-stop": ("UserStopped", 1), "interrupt-reject": ("Failed", 1),
        "interrupt-chest": ("Completed", 5), "interrupt-heal": ("Completed", 6)})
    for name in ("interrupt-map", "interrupt-chest", "interrupt-heal"):
        result = read(evidence / name / "output.json")
        if result["lifecycle_calls"] or result["snapshot"]["generation"] != 1 or result["snapshot"]["business"]["task_step"] != 1:
            raise ValueError("M4_INTERRUPTION_NOT_ORDINARY_RETURN:" + name)
    if not read(evidence / "interrupt-skill/output.json")["snapshot"]["business"]["has_prepared_skill"]:
        raise ValueError("M4_INTERRUPTION_CONSUMED_SKILL")
    path = ROOT / "docs/migration/m4-implementation-map.json"
    document = read(path)
    for row in document["entries"]:
        if row["legacy_symbol"] in {"Factory.TryHandleCommonBlockingScreen", "Factory.TryPressRetry"}:
            row.update(implementation_extent="COMMON_SCREEN_DISPATCH_AND_SUBFLOW_INTERRUPTION",
                supporting_implementations=["native/games/wvd/vision/boot_probes.hpp", "native/games/wvd/tasks/pipeline_compiler.cpp"],
                evidence_report="../m4-interruption-validation.md",
                verification_scope="路线及地图/战斗/开箱/角色恢复子作用域中途插入，6 个离线场景；不是完整任务迁移",
                remaining="其余入城/住宿/专项的中途插入、死亡/Pause/对话及副作用未确认窗口对账仍待接齐。")
    write(path, document)
    print("Ordinary subflow interruption recorded; complete task statuses unchanged.")


def update_iteration(evidence, plan_evidence):
    verify_workflows(evidence, {"auto-return-prompt": ("Completed", 1), "iteration-entry": ("Completed", 4),
        "iteration-fail": ("Failed", 1), "iteration-stop": ("UserStopped", 1), "iteration-two": ("Completed", 10),
        **{"position-exit-" + name: ("Completed", 2) for name in ("openworldmap", "worldmapflag", "returnText")}})
    snapshot = read(evidence / "iteration-two/output.json")["snapshot"]
    state = snapshot["business"]
    if (snapshot["generation"] != 2 or snapshot["completed_business_units"] != 2 or
            state["combats"] != 2 or state["dungeons"] != 1 or state["task_step"] != 1 or state["supply_cycle"] != 2):
        raise ValueError("M4_ITERATION_STATE_MISMATCH")
    compiled = read(plan_evidence / "iterations/result.json")
    execution = read(plan_evidence / "iterations/execution.json")
    exe_hash = hashlib.sha256((ROOT / "build/m4/Release/wvd_m4_check.exe").read_bytes()).hexdigest()
    source = read(ROOT / "packs/wvd/parameters/legacy-quests.json")
    rows = {row["task_id"]: row for row in compiled["compiled_iterations"]}
    if (execution != {"exe_sha256": exe_hash, "exit": 0} or compiled["outcome"] != "PASS"
            or set(rows) != {key for key, value in source.items() if value["_TYPE"] == "dungeon"}
            or any(row["executed"] or row["missing_images"] or row["scope"] != "NORMAL_FARM_ITERATION_NOT_FULL_TASK" for row in rows.values())):
        raise ValueError("M4_ITERATION_COMPILATION_MISMATCH")
    folder = ROOT / "docs/migration"
    document = read(folder / "m4-implementation-map.json")
    for row in document["entries"]:
        if row["legacy_symbol"] == "Factory.DungeonFarm":
            row.update(implementation="native/games/wvd/tasks/dungeon_iteration.cpp", entry="tasks::dungeon_iteration",
                supporting_implementations=["native/games/wvd/tasks/departure.cpp", "native/games/wvd/tasks/dungeon_route.cpp"],
                implementation_extent="NORMAL_FARM_ITERATION", implementation_status="PARTIAL", offline_status="PASS",
                verification_scope="正常入口/路线/出本及两段续接，不是全部43任务执行或全局事件完整迁移",
                evidence_report="../m4-iteration-validation.md",
                remaining="全局阻塞/死亡/复活/对话、完整任务恢复与逐任务离线证据未齐。")
    tasks = read(folder / "m4-task-status.json")
    if document["counts"] != {"function": 250, "config": 33, "task": 58} or {row["task_id"] for row in tasks["items"]} != set(source):
        raise ValueError("M4_ITERATION_DENOMINATOR_MISMATCH")
    for row in tasks["items"]:
        if row["task_id"] in rows:
            row["iteration_compilation"] = {"status": "PASS", "entry": "tasks::dungeon_iteration", "executed": False,
                "scope": "NORMAL_FARM_ITERATION_NOT_FULL_TASK", "evidence_report": "../m4-iteration-validation.md"}
            if "native/games/wvd/tasks/dungeon_iteration.cpp" not in row["new_files"]:
                row["new_files"].append("native/games/wvd/tasks/dungeon_iteration.cpp")
            row["implementation_extent"] = "NORMAL_FARM_ITERATION_PARTIAL"
    write(folder / "m4-implementation-map.json", document)
    write(folder / "m4-task-status.json", tasks)
    print("43 finite Farm iterations compiled; complete task execution statuses unchanged.")


def update_departure(evidence):
    expected = {"inn-receipt": ("Completed", 5), "inn-paid-exit-failed": ("Failed", 5),
        "departure-forced": ("Completed", 5), "departure-return-town": ("Completed", 6),
        "departure-world": ("Completed", 1), "departure-prompt": ("Completed", 1),
        "departure-stop": ("UserStopped", 1), "departure-reject": ("Failed", 1),
        **{"departure-skip-" + name: ("Completed", 0) for name in ("Inn", "returntoTown", "openworldmap", "EdgeOfTown")},
        **{"departure-uncertain-" + name: ("Interrupted", 0) for name in ("Stay", "worldmapflag", "mapFlag")}}
    verify_workflows(evidence, expected)
    for name in ("inn-receipt", "inn-paid-exit-failed", "departure-forced", "departure-return-town"):
        state = read(evidence / name / "output.json")["snapshot"]["business"]
        if state["inn_rests"] != 1 or not state["inn_rest_completed"]:
            raise ValueError("M4_INN_RECEIPT_MISMATCH:" + name)
    path = ROOT / "docs/migration/m4-implementation-map.json"
    document = read(path)
    for row in document["entries"]:
        if row["legacy_symbol"] == "Factory.StateInn":
            row.update(implementation="native/games/wvd/supply/inn.cpp", entry="supply::rest_at_inn",
                implementation_extent="FINITE_INN_WITH_RUN_RECEIPT", offline_status="PASS",
                evidence_report="../m4-departure-validation.md",
                remaining="已确认住宿后不重复付费；输入后确认前的中断窗口和完整任务恢复仍须承接。")
        if row["legacy_symbol"] == "Factory.DungeonFarm":
            row.update(implementation="native/games/wvd/tasks/departure.cpp", entry="tasks::prepare_departure",
                implementation_status="PARTIAL", implementation_extent="DEPARTURE_SUPPLY_SUBFLOW",
                offline_status="PASS", evidence_report="../m4-departure-validation.md",
                verification_scope="回城补给子流程，不是 DungeonFarm 完整运行",
                remaining="完整入本/路线循环、其他全局事件、逐任务成功/失败/停止/恢复证据未齐。")
    if document["counts"] != {"function": 250, "config": 33, "task": 58}:
        raise ValueError("M4_DEPARTURE_INVENTORY_MISMATCH")
    write(path, document)
    print("Departure and inn receipt subflow recorded; complete task execution statuses unchanged.")


def update_boundaries(evidence):
    """只登记本轮实际链路，保留已存在的任务/子流程范围，不重建整张历史叠加表。"""
    expected = {
        "heal-rotate-seek": ("Completed", 7), "image-base-first": ("Completed", 1),
        "image-alias-first": ("Completed", 1), "image-mod-snapshot": ("Completed", 1),
        "image-base-corrupt": ("Failed", 0), "uncertain-reject": ("Failed", 2),
        "uncertain-defend": ("Interrupted", 1), "uncertain-enemy": ("Interrupted", 2),
        "uncertain-disarm": ("Interrupted", 2), "uncertain-heal-False": ("Interrupted", 3),
        "uncertain-heal-True": ("Interrupted", 4),
    }
    verify_workflows(evidence, expected)
    for name in expected:
        result = read(evidence / name / "output.json")
        if result["lifecycle_calls"] or result["snapshot"]["generation"] != 1:
            raise ValueError("M4_BOUNDARY_UNEXPECTED_RECOVERY:" + name)
    changed = read(evidence / "image-mod-changed/output.json")
    if changed["connections"] or changed["backend_calls"] or "HASH" not in changed["publish_error"]:
        raise ValueError("M4_MOD_CHANGED_NOT_REJECTED")
    for name, owner in (("image-base-first", "baseline"), ("image-alias-first", "baseline"), ("image-mod-snapshot", "mod")):
        selected = read(evidence / name / "output.json")["image_sources"]["images"]["City_RoyalCityLuknalia.png"]
        if selected["source"] != owner:
            raise ValueError("M4_IMAGE_SOURCE_MISMATCH:" + name)
    path = ROOT / "docs/migration/m4-implementation-map.json"
    document = read(path)
    if document["counts"] != {"function": 250, "config": 33, "task": 58}:
        raise ValueError("M4_BOUNDARY_INVENTORY_MISMATCH")
    for row in document["entries"]:
        if row["legacy_symbol"] == "LoadTemplateImage":
            row.update(implementation="native/games/wvd/vision/asset_resolver.cpp",
                entry="vision::resolve_image_source / AssetResolver::load",
                supporting_implementations=["native/games/wvd/tasks/workflow_session.cpp"],
                implementation_status="PARTIAL", implementation_extent="FROZEN_IMAGE_IMPORT_AND_RESOLUTION",
                offline_status="PASS", evidence_report="../m4-image-import-validation.md",
                verification_scope="基线/别名/mod 来源与封存后真实 Maa 输入；不是用户 mod 或真实图片质量验收",
                remaining="真实用户图片 mod 未导入验证；完整任务验收未齐。坏图按本包 Error 契约处理，不隐式后备。")
        if row["legacy_symbol"] in ("Factory.StateCombat", "Factory.StateChest", "Factory.StateDungeon"):
            row["boundary_validation"] = {"status": "PASS", "evidence_report": "../m4-effect-interruption-validation.md",
                "scope": "明确副作用后覆盖层停止，不重放、不消费，不是全部恢复窗口已闭合"}
    write(path, document)
    print("Effect and image-source boundaries recorded; complete task statuses unchanged.")


def update_revival(evidence, pause_evidence, pause_retry_evidence):
    verify_workflows(evidence, {
        "route-Inn": ("Completed", 0), "route-RiseAgain": ("Completed", 1),
        "revival-confirmed-False": ("Completed", 1), "revival-confirmed-True": ("Completed", 2),
        "route-revival-heal": ("Completed", 7), "revival-stop-False": ("Failed", 1),
        "revival-stop-True": ("UserStopped", 1), "revival-unchanged": ("Interrupted", 2),
        "revival-blocked": ("Interrupted", 1),
    })
    verify_workflows(pause_evidence, {
        "pause-clears-1": ("Completed", 1), "pause-clears-6": ("Completed", 6),
        **{"pause-negative-" + name: ("Completed", 0) for name in ("trait", "recover", "spellskill-skillDetail", "close")},
    })
    verify_workflows(pause_retry_evidence, {
        "pause-frozen": ("Interrupted", 6), "pause-stop-False": ("Failed", 1), "pause-stop-True": ("UserStopped", 1),
    })
    for name in ("revival-unchanged", "revival-blocked"):
        result = read(evidence / name / "output.json")
        if result["lifecycle_calls"] or result["snapshot"]["sessions"][-1]["reason"] != "revival.outcome_unconfirmed":
            raise ValueError("REVIVAL_UNCERTAIN_RECOVERY_INVALID")
    frozen = read(pause_retry_evidence / "pause-frozen/output.json")
    if frozen["snapshot"]["sessions"][-1]["reason"] != "pause.physics_frozen":
        raise ValueError("PAUSE_FREEZE_REASON_INVALID")
    path = ROOT / "docs/migration/m4-implementation-map.json"
    document = read(path)
    owners = {
        "Factory.RiseAgainReset": ("recovery/revival.cpp", "recovery::revive_after_defeat", "m4-revival-validation.md"),
        "Factory.TryResumePauseOverlay": ("recovery/boot.cpp", "recovery::clear_common_screens", "m4-pause-validation.md"),
        "Factory.IdentifyState": ("tasks/dungeon_route.cpp", "tasks::traverse_dungeon / recovery::clear_common_screens", "m4-revival-validation.md"),
    }
    for row in document["entries"]:
        if row["legacy_symbol"] in owners:
            source, entry, report = owners[row["legacy_symbol"]]
            row.update(implementation="native/games/wvd/" + source, entry=entry,
                implementation_status="PARTIAL", implementation_extent="GLOBAL_PAUSE_REVIVAL_SUBSET",
                offline_status="PASS", evidence_report="../" + report,
                verification_scope="真实 Maa 合成场景的 Pause/复活与正常路线连接，不是全部全局事件或完整任务",
                remaining="多人死亡、其它全局对话、完整任务与真实质量未齐；失败遭遇使用独立序号，不复用成功次数。")
    write(path, document)
    print("Pause and revival evidence recorded; complete task counts unchanged.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--data-evidence", type=Path)
    parser.add_argument("--state-evidence", type=Path)
    parser.add_argument("--plan-evidence", type=Path)
    parser.add_argument("--workflow-evidence", type=Path)
    parser.add_argument("--combat-evidence", type=Path)
    parser.add_argument("--navigation-evidence", type=Path)
    parser.add_argument("--encounter-evidence", type=Path)
    parser.add_argument("--healing-evidence", type=Path)
    parser.add_argument("--dungeon-route-evidence", type=Path)
    parser.add_argument("--departure-evidence", type=Path)
    parser.add_argument("--iteration-evidence", type=Path)
    parser.add_argument("--common-evidence", type=Path)
    parser.add_argument("--interruption-evidence", type=Path)
    parser.add_argument("--boundaries-evidence", type=Path)
    parser.add_argument("--revival-evidence", type=Path)
    parser.add_argument("--pause-evidence", type=Path)
    parser.add_argument("--pause-retry-evidence", type=Path)
    args = parser.parse_args()
    if not any((args.data_evidence, args.combat_evidence, args.navigation_evidence, args.encounter_evidence, args.healing_evidence, args.dungeon_route_evidence, args.departure_evidence, args.iteration_evidence, args.common_evidence, args.interruption_evidence, args.boundaries_evidence, args.revival_evidence)):
        parser.error("an evidence group is required")
    if args.data_evidence:
        generate(args.data_evidence, args.state_evidence, args.plan_evidence, args.workflow_evidence)
    if args.combat_evidence:
        update_combat(args.combat_evidence)
    if args.navigation_evidence:
        if not args.plan_evidence:
            parser.error("--navigation-evidence requires --plan-evidence")
        update_navigation(args.navigation_evidence, args.plan_evidence)
    if args.encounter_evidence:
        update_encounter(args.encounter_evidence)
    if args.healing_evidence:
        update_healing(args.healing_evidence)
    if args.departure_evidence:
        update_departure(args.departure_evidence)
    if args.iteration_evidence:
        if not args.plan_evidence:
            parser.error("--iteration-evidence requires --plan-evidence")
        update_iteration(args.iteration_evidence, args.plan_evidence)
    if args.common_evidence:
        update_common(args.common_evidence)
    if args.interruption_evidence:
        update_interruption(args.interruption_evidence)
    if args.boundaries_evidence:
        update_boundaries(args.boundaries_evidence)
    if args.revival_evidence:
        if not args.pause_evidence or not args.pause_retry_evidence:
            parser.error("--revival-evidence requires both Pause evidence directories")
        update_revival(args.revival_evidence, args.pause_evidence, args.pause_retry_evidence)
    if args.dungeon_route_evidence:
        if not args.plan_evidence:
            parser.error("--dungeon-route-evidence requires --plan-evidence")
        update_dungeon_route(args.dungeon_route_evidence, args.plan_evidence)
