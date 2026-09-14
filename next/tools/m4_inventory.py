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


def update_combat(combat_evidence):
    """只覆盖已验证的战斗子流程行，保留其他阶段证据及全部任务分母。"""
    expected = {
        "turn-enemy": ("Completed", 3), "turn-aoe": ("Completed", 3), "turn-support": ("Completed", 3),
        "turn-auto": ("Completed", 4), "turn-unmatched": ("Completed", 2), "turn-low-edge": ("Completed", 3),
        "turn-fail-False": ("Failed", 1), "turn-fail-True": ("UserStopped", 1), "turn-stale-actor": ("Failed", 2),
        "turn-defend": ("Completed", 2), "turn-resource-False": ("Completed", 7),
        "turn-resource-True": ("Interrupted", 7), "turn-three-open": ("Completed", 5),
    }
    executable_hash = hashlib.sha256((ROOT / "build/m4/Release/test_m4_workflow.exe").read_bytes()).hexdigest()
    for name, (state, calls) in expected.items():
        result = read(combat_evidence / name / "output.json")
        execution = read(combat_evidence / name / "execution.json")
        if (execution != {"exe_sha256": executable_hash, "exit": 0} or result["snapshot"]["state"] != state
                or result["backend_calls"] != calls or result["mismatch"] or not result["snapshot"]["quiescent"]):
            raise ValueError("M4_COMBAT_EVIDENCE_MISMATCH:" + name)
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


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--data-evidence", type=Path)
    parser.add_argument("--state-evidence", type=Path)
    parser.add_argument("--plan-evidence", type=Path)
    parser.add_argument("--workflow-evidence", type=Path)
    parser.add_argument("--combat-evidence", type=Path)
    args = parser.parse_args()
    if not args.data_evidence and not args.combat_evidence:
        parser.error("--data-evidence or --combat-evidence is required")
    if args.data_evidence:
        generate(args.data_evidence, args.state_evidence, args.plan_evidence, args.workflow_evidence)
    if args.combat_evidence:
        update_combat(args.combat_evidence)
