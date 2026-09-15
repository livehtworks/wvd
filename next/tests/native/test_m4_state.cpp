#include "runtime_fixture.hpp"
#include "games/wvd/state.hpp"
#include "games/wvd/vision/recognizers.hpp"
#include "storage/legacy_import.hpp"
#include "storage/profile_store.hpp"
#include "storage/karma_writer.hpp"
#include <algorithm>
#include <array>
#include <barrier>
#include <iostream>
#include <thread>
#include <windows.h>

using namespace fixture;
class TestClock final : public contracts::MonotonicClock {
  public:
    std::atomic<std::int64_t> milliseconds{};
    TimePoint now() const noexcept override {
        return TimePoint{} + std::chrono::milliseconds(milliseconds.load());
    }
};
class StateDevice final : public OfflineDevice {
  public:
    std::atomic<bool> block_capture{}, capture_waiting{}, release_capture{};
    devices::RawFrame capture() override {
        capture_waiting = true;
        while (block_capture && !release_capture)
            std::this_thread::sleep_for(5ms);
        return OfflineDevice::capture();
    }
};

// 测试绑定只驱动真实状态 API；不代替技能输入、识别或完整任务完成的验收。
bool state_probe(maafw::Context &context, const J &node, const J &) {
    if (node.value("cache_check", false)) {
        const auto frame = context.capture();
        const auto request = maafw::parse_recognition_request(
            {{"id", "state-cache"}, {"revision", "1"}, {"type", "custom"}, {"binding", "WvdVision"},
             {"roi", {0, 0, 900, 1600}}, {"parameters", {{"mode", "business"}, {"field", "/task_step"}, {"value", 0}}}});
        const auto before = context.recognize(frame, request);
        context.with_business_state([](contracts::BusinessRunState &base) {
            dynamic_cast<games::WvdRunState &>(base).target_point_completed();
            return true;
        });
        // 故意复用同一帧和参数；不能因图片未变而命中旧业务条件缓存。
        const auto after = context.recognize(frame, request);
        require(before.outcome == contracts::RecognitionOutcome::Hit &&
                    after.outcome == contracts::RecognitionOutcome::NoHit, "BUSINESS_CONDITION_CACHE_STALE");
        require(!before.action_eligible && !before.center, "BUSINESS_CONDITION_GRANTED_INPUT");
    }
    return context.with_business_state([&](contracts::BusinessRunState &base) {
        auto &state = dynamic_cast<games::WvdRunState &>(base);
        auto before = state.summary();
        if (node.contains("expected_remaining"))
            require(before["strategy"]["current"]["skill_settings"].size() ==
                        node.at("expected_remaining").get<std::size_t>(),
                    "STATE_NOT_CONTINUED");
        if (node.value("restart", false))
            state.restart_game();
        if (node.value("consume", false)) {
            auto skill = state.select_skill({{node.at("role"), .95}});
            require(skill.has_value(), "EXPECTED_SKILL_MISSING");
            state.confirm_skill(*skill, games::SkillOutcome::Succeeded);
        }
        if (node.value("target_completed", false))
            state.target_point_completed();
        return true;
    });
}
std::optional<runtime::SessionDefinition> recover_unit(const contracts::SessionResult &,
                                                       const runtime::SessionDefinition &previous,
                                                       const J &) {
    auto next = previous;
    next.entry = "Recovered";
    return next;
}
J direct_contract(const J &profile) {
    auto clock = std::make_shared<TestClock>();
    games::WvdRunState state(profile, {"direct", 1, clock});
    state.enter_segment(contracts::SegmentBoundary::Initial, 1, 0);
    state.dungeon_completed();
    J result;
    result["initial"] = state.summary();
    {
        auto configured = profile;
        configured.update({{"ACTIVE_REST", false}, {"REST_INTERVEL", 1}});
        games::WvdRunState giant(configured, {"giant", 1, clock});
        giant.enter_segment(contracts::SegmentBoundary::Initial, 1, 0);
        const auto start = giant.confirmation_id("giant.start", "giant_cycle_started");
        giant.confirm_event(start, "giant_cycle_started", 1, 1);
        giant.enter_dungeon();
        bool early = false;
        try { giant.confirm_event("early", "giant_route_completed", 1, 2); }
        catch (const std::exception &e) { early = std::string(e.what()) == "GIANT_ROUTE_NOT_COMPLETED"; }
        require(early, "GIANT_EARLY_ROUTE_COMPLETED");
        for (int i = 0; i < 2; ++i) giant.target_point_completed();
        giant.confirm_event(giant.confirmation_id("giant.route", "giant_route_completed"), "giant_route_completed", 1, 3);
        giant.enter_segment(contracts::SegmentBoundary::LifecycleRecovery, 2, 0);
        require(!giant.confirm_event(start, "giant_cycle_started", 2, 4), "GIANT_RESTART_DOUBLE_COUNTED");
        require(giant.summary().at("giant_route_completed").get<bool>(), "GIANT_RESTART_LOST_ROUTE");
        bool unpaid = false;
        try { giant.confirm_event("unpaid", "giant_cycle_completed", 2, 5); }
        catch (const std::exception &e) { unpaid = std::string(e.what()) == "GIANT_CYCLE_NOT_COMPLETED"; }
        require(unpaid, "GIANT_SKIPPED_DUE_REST");
        giant.confirm_event(giant.confirmation_id("inn.prepare", "inn_payment_prepared"), "inn_payment_prepared", 2, 6);
        giant.confirm_event(giant.confirmation_id("inn.paid", "inn_rest_completed"), "inn_rest_completed", 2, 7);
        const auto done = giant.confirmation_id("giant.complete", "giant_cycle_completed");
        giant.confirm_event(done, "giant_cycle_completed", 2, 8);
        require(!giant.confirm_event(done, "giant_cycle_completed", 2, 9), "GIANT_DUPLICATE_COMPLETION");
        giant.enter_segment(contracts::SegmentBoundary::Continuation, 3, 1);
        giant.confirm_event(giant.confirmation_id("giant.start", "giant_cycle_started"), "giant_cycle_started", 3, 10);
        giant.enter_dungeon();
        require(!giant.summary().at("giant_rest_due").get<bool>(), "GIANT_INTERVAL_OFF_BY_ONE");
        for (int i = 0; i < 2; ++i) giant.target_point_completed();
        giant.confirm_event(giant.confirmation_id("giant.route", "giant_route_completed"), "giant_route_completed", 3, 11);
        giant.confirm_event(giant.confirmation_id("giant.complete", "giant_cycle_completed"), "giant_cycle_completed", 3, 12);
        result["giant_contract"] = giant.summary();
    }
    {
        games::WvdRunState trap(profile, {"trap", 1, clock});
        trap.enter_segment(contracts::SegmentBoundary::Initial, 1, 0);
        const auto started = trap.confirmation_id("trap.start", "trap_cycle_started");
        trap.confirm_event(started, "trap_cycle_started", 1, 1);
        trap.enter_segment(contracts::SegmentBoundary::Recovery, 2, 0);
        require(!trap.confirm_event(started, "trap_cycle_started", 2, 2), "TRAP_RESTART_DOUBLE_COUNTED");
        bool premature = false;
        try { trap.confirm_event("early", "trap_cycle_completed", 2, 3); }
        catch (const std::runtime_error &e) { premature = std::string(e.what()) == "TRAP_ROUTE_NOT_COMPLETED"; }
        require(premature, "TRAP_PREMATURE_COMPLETION");
        trap.enter_dungeon();
        for (int i = 0; i < 7; ++i)
            trap.target_point_completed();
        const auto completed = trap.confirmation_id("trap.complete", "trap_cycle_completed");
        trap.confirm_event(completed, "trap_cycle_completed", 2, 4);
        require(!trap.confirm_event(completed, "trap_cycle_completed", 2, 5), "TRAP_COMPLETION_REPLAYED");
        trap.enter_segment(contracts::SegmentBoundary::Continuation, 3, 1);
        trap.confirm_event(trap.confirmation_id("trap.start", "trap_cycle_started"), "trap_cycle_started", 3, 6);
        result["trap_contract"] = trap.summary();
        require(result["trap_contract"].at("dungeons") == 2 && result["trap_contract"].at("trap_cycles_completed") == 1,
                "TRAP_ATTEMPT_SUCCESS_CONFLATED");
    }
    {
        games::WvdRunState route(profile, {"route-restart", 1, clock});
        route.enter_segment(contracts::SegmentBoundary::Initial, 1, 0);
        auto entry = route.confirmation_id("entry", "dungeon_entered");
        route.confirm_event(entry, "dungeon_entered", 1, 1);
        const auto point = route.confirmation_id("point.0", "target_completed");
        route.confirm_event(point, "target_completed", 1, 2, 0);
        route.enter_segment(contracts::SegmentBoundary::LifecycleRecovery, 2, 0);
        const auto restarted_entry = route.confirmation_id("entry", "dungeon_entered");
        require(entry != restarted_entry, "ROUTE_RECOVERY_REUSED_ENTRY_RECEIPT");
        route.confirm_event(restarted_entry, "dungeon_entered", 2, 3);
        require(route.summary().at("task_step") == 0, "ROUTE_RECOVERY_KEPT_OLD_STEP");
        require(!route.confirm_event(point, "target_completed", 2, 4, 0), "ROUTE_OLD_RECEIPT_REPLAYED");
        route.confirm_event(route.confirmation_id("point.0", "target_completed"), "target_completed", 2, 5, 0);
        require(route.summary().at("task_step") == 1, "ROUTE_NEW_PASS_NOT_ADVANCED");
        result["route_restart_contract"] = route.summary();
    }
    {
        games::WvdRunState wall(profile, {"wall", 1, clock});
        wall.enter_segment(contracts::SegmentBoundary::Initial, 1, 0);
        require(wall.summary().at("bypass_after_restart").get<bool>(), "WALL_ENABLED_BEFORE_RESTART");
        wall.restart_game();
        bool wrong_order = false;
        try { wall.confirm_event("wall-wrong", "wall_right_completed", 1, 1); }
        catch (const std::runtime_error &e) { wrong_order = std::string(e.what()) == "WALL_BYPASS_STEP_MISMATCH"; }
        require(wrong_order, "WALL_ACCEPTED_WRONG_ORDER");
        const auto turn = wall.confirmation_id("wall.Turn", "wall_turn_completed");
        wall.confirm_event(turn, "wall_turn_completed", 1, 2);
        wall.enter_segment(contracts::SegmentBoundary::Continuation, 2, 1);
        require(!wall.confirm_event(turn, "wall_turn_completed", 2, 3), "WALL_REPLAYED_TURN");
        require(wall.summary().at("wall_bypass_step") == 1, "WALL_LOST_CONFIRMED_PHASE");
        wall.confirm_event(wall.confirmation_id("wall.Left", "wall_left_completed"), "wall_left_completed", 2, 4);
        const auto right = wall.confirmation_id("wall.Right", "wall_right_completed");
        wall.confirm_event(right, "wall_right_completed", 2, 5);
        require(wall.summary().at("bypass_after_restart").get<bool>(), "WALL_NOT_COMPLETED");
        wall.restart_game();
        require(!wall.confirm_event(right, "wall_right_completed", 2, 6), "OLD_WALL_COMPLETED_NEW_RESTART");
        require(wall.confirmation_id("wall.Turn", "wall_turn_completed") != turn, "WALL_REUSED_RESTART_ID");
        result["wall_bypass_contract"] = wall.summary();
    }
    auto supply_profile = profile;
    supply_profile.update({{"ACTIVE_REST", true}, {"REST_INTERVEL", 1}, {"RE_ASSEMBLE_PARTY", true}});
    games::WvdRunState supply_state(supply_profile, {"supply", 1, clock});
    supply_state.enter_segment(contracts::SegmentBoundary::Initial, 1, 0);
    supply_state.dungeon_completed();
    supply_state.observe_combat();
    supply_state.resume_dungeon();
    require(supply_state.summary().at("ordinary_rest_due").get<bool>(), "ORDINARY_REST_MISSING");
    const auto receipt = supply_state.confirmation_id("inn", "inn_rest_completed");
    bool unprepared_rejected = false;
    try { supply_state.confirm_event(receipt, "inn_rest_completed", 1, 1); }
    catch (const std::exception &e) { unprepared_rejected = std::string(e.what()) == "INN_PAYMENT_NOT_PREPARED"; }
    require(unprepared_rejected, "UNPREPARED_INN_COMPLETED");
    const auto payment = supply_state.confirmation_id("inn.prepare", "inn_payment_prepared");
    supply_state.confirm_event(payment, "inn_payment_prepared", 1, 1);
    require(supply_state.summary().at("inn_payment_pending").get<bool>(), "PAYMENT_INTENT_MISSING");
    require(!supply_state.confirm_event(payment, "inn_payment_prepared", 1, 1), "PAYMENT_INTENT_REPLAYED");
    supply_state.confirm_event(receipt, "inn_rest_completed", 1, 1);
    require(!supply_state.summary().at("inn_payment_pending").get<bool>(), "CONFIRMED_PAYMENT_STILL_PENDING");
    require(!supply_state.summary().at("ordinary_rest_due").get<bool>(), "PAID_REST_REPEATED");
    supply_state.enter_segment(contracts::SegmentBoundary::Continuation, 2, 1);
    require(supply_state.summary().at("inn_rest_completed").get<bool>(), "RECEIPT_LOST_ON_CONTINUATION");
    require(!supply_state.confirm_event(receipt, "inn_rest_completed", 2, 2), "RECEIPT_REPLAYED");
    supply_state.enter_dungeon();
    require(!supply_state.summary().at("inn_rest_completed").get<bool>(), "NEW_LAP_HAS_OLD_RECEIPT");
    require(!supply_state.confirm_event(receipt, "inn_rest_completed", 2, 3), "OLD_RECEIPT_PAID_NEW_LAP");
    clock->milliseconds = 21600000;
    supply_state.dungeon_completed();
    require(supply_state.summary().at("party_refresh_due").get<bool>(), "PARTY_PERIOD_MISSING");
    const auto party_receipt = supply_state.confirmation_id("party", "party_reassembled");
    supply_state.confirm_event(party_receipt, "party_reassembled", 2, 4);
    require(!supply_state.summary().at("party_refresh_due").get<bool>(), "PARTY_PERIOD_NOT_COMMITTED");
    result["supply_receipts"] = supply_state.summary();
    games::WvdRunState interrupted_payment(supply_profile, {"unconfirmed-payment", 1, clock});
    interrupted_payment.enter_segment(contracts::SegmentBoundary::Initial, 1, 0);
    const auto unpaid = interrupted_payment.confirmation_id("inn.prepare", "inn_payment_prepared");
    interrupted_payment.confirm_event(unpaid, "inn_payment_prepared", 1, 1);
    interrupted_payment.enter_segment(contracts::SegmentBoundary::LifecycleRecovery, 2, 0);
    require(interrupted_payment.summary().at("inn_payment_pending").get<bool>(), "RESTART_LOST_PAYMENT_INTENT");
    bool new_lap_rejected = false;
    try { interrupted_payment.enter_dungeon(); }
    catch (const std::exception &e) { new_lap_rejected = std::string(e.what()) == "INN_PAYMENT_UNCONFIRMED"; }
    require(new_lap_rejected && interrupted_payment.summary().at("inn_rests") == 0, "PENDING_PAYMENT_COUNTED_OR_DISCARDED");
    require(!interrupted_payment.confirm_event(unpaid, "inn_payment_prepared", 2, 2), "RESTART_REPLAYED_PAYMENT_INTENT");
    result["unconfirmed_payment"] = interrupted_payment.summary();
    clock->milliseconds = 0;
    games::WvdRunState recurring(profile, {"recurring", 1, clock});
    recurring.enter_segment(contracts::SegmentBoundary::Initial, 1, 0);
    for (std::uint64_t encounter = 1; encounter <= 2; ++encounter) {
        const auto combat_id = recurring.confirmation_id("observe", "combat_observed");
        require(recurring.confirm_event(combat_id, "combat_observed", 1, encounter * 10), "ENCOUNTER_BEGIN_MISSING");
        require(recurring.confirmation_id("observe", "combat_observed") == combat_id, "ENCOUNTER_ID_UNSTABLE");
        require(!recurring.confirm_event(combat_id, "combat_observed", 1, encounter * 10 + 1), "ENCOUNTER_REPEAT_APPLIED");
        const auto chest_id = recurring.confirmation_id("chest", "chest_observed");
        require(recurring.confirm_event(chest_id, "chest_observed", 1, encounter * 10 + 2), "CHEST_BEGIN_MISSING");
        const auto resume_id = recurring.confirmation_id("resume", "dungeon_resumed");
        require(recurring.confirm_event(resume_id, "dungeon_resumed", 1, encounter * 10 + 3), "ENCOUNTER_RESUME_MISSING");
        require(recurring.confirmation_id("resume", "dungeon_resumed") == resume_id, "RESUME_ID_UNSTABLE");
        require(!recurring.confirm_event(resume_id, "dungeon_resumed", 1, encounter * 10 + 4), "RESUME_REPEAT_COUNTED");
        // 保存的旧 ID 即使迟到到下一次循环也不能重放已执行效果。
        require(!recurring.confirm_event(combat_id, "combat_observed", 1, encounter * 10 + 5), "OLD_RECEIPT_REPLAYED");
    }
    result["recurring_encounters"] = recurring.summary();
    auto healing_profile = profile;
    healing_profile["RECOVER_WHEN_BEGINNING"] = true;
    healing_profile["SKIP_COMBAT_RECOVER"] = false;
    healing_profile["SKIP_CHEST_RECOVER"] = true;
    games::WvdRunState healing(healing_profile, {"healing", 1, clock});
    healing.enter_segment(contracts::SegmentBoundary::Initial, 1, 0);
    require(healing.healing_required(), "INITIAL_HEALING_MISSING");
    const auto request = healing.confirmation_id("heal", "healing_requested");
    require(healing.confirm_event(request, "healing_requested", 1, 1), "HEALING_REQUEST_MISSING");
    require(healing.confirmation_id("heal", "healing_requested") == request, "HEALING_ID_CHANGED_IN_PROGRESS");
    require(!healing.confirm_event(request, "healing_requested", 1, 2), "HEALING_REQUEST_REPLAYED");
    require(healing.healing_required() && !healing.summary().at("need_initial_recover").get<bool>(), "HEALING_PENDING_LOST");
    const auto completed = healing.confirmation_id("done", "healing_completed");
    healing.confirm_event(completed, "healing_completed", 1, 3);
    require(!healing.healing_required(), "HEALING_NOT_CLEARED");
    healing.observe_chest();
    healing.resume_dungeon();
    require(!healing.healing_required(), "SKIPPED_CHEST_HEALING_REQUESTED");
    healing.observe_combat();
    healing.resume_dungeon();
    require(healing.healing_required(), "COMBAT_HEALING_MISSING");
    const auto again = healing.confirmation_id("heal", "healing_requested");
    require(again != request, "DISTINCT_HEALING_ID_REUSED");
    healing.confirm_event(again, "healing_requested", 1, 4);
    require(!healing.confirm_event(completed, "healing_completed", 1, 5) && healing.healing_required(),
            "OLD_HEALING_COMPLETION_REPLAYED");
    healing.observe_combat();
    healing.resume_dungeon();
    require(!healing.summary().at("healing_active").get<bool>() && healing.healing_required(), "INTERRUPTED_HEALING_RETAINED_INTENT");
    const auto interrupted = healing.confirmation_id("heal", "healing_requested");
    require(interrupted != again, "INTERRUPTED_HEALING_REUSED_ID");
    healing.confirm_event(interrupted, "healing_requested", 1, 6);
    healing.enter_segment(contracts::SegmentBoundary::Continuation, 2, 1);
    require(healing.healing_required() && !healing.summary().at("healing_active").get<bool>(), "HEALING_INTENT_CROSSED_GENERATION");
    result["healing_contract"] = healing.summary();
    result["below_threshold"] = state.select_skill({{"A", .79}}).has_value();
    result["negative_is_nohit"] = !state.select_skill({{"A", -1}, {"B", -.5}}).has_value();
    auto selection = state.select_skill({{"A_sp", .8}});
    require(selection.has_value(), "ALIAS_NOT_SELECTED");
    for (auto outcome : {games::SkillOutcome::TargetFailed, games::SkillOutcome::Cancelled,
                         games::SkillOutcome::AutoFallback})
        require(!state.confirm_skill(*selection, outcome), "FAILED_ACTION_CONSUMED");
    result["after_failures"] = state.summary();
    state.confirm_skill(*selection, games::SkillOutcome::Succeeded);
    result["after_success"] = state.summary();
    games::WvdRunState fallback(profile, {"fallback", 1, clock});
    fallback.enter_segment(contracts::SegmentBoundary::Initial, 1, 0);
    fallback.prepare_skill({{"A", .91}}, profile.at("STRATEGY")[0].at("skill_settings"));
    require(!fallback.finish_prepared_skill(0, games::SkillOutcome::AutoFallback), "UNCONFIRMED_AUTO_CONSUMED");
    result["fallback_before_confirmation"] = fallback.summary();
    fallback.finish_prepared_skill(0, games::SkillOutcome::AutoFallbackConfirmed);
    result["fallback_confirmed"] = fallback.summary();
    fallback.prepare_skill({{"B", .91}}, profile.at("STRATEGY")[0].at("skill_settings"));
    fallback.enter_segment(contracts::SegmentBoundary::Continuation, 2, 1);
    result["prepared_cleared_at_boundary"] = !fallback.summary().at("has_prepared_skill").get<bool>();
    try {
        state.confirm_skill(*selection, games::SkillOutcome::Succeeded);
        result["duplicate_rejected"] = false;
    } catch (const std::runtime_error &e) {
        result["duplicate_rejected"] = std::string(e.what()) == "STALE_STRATEGY_SELECTION";
    }
    state.enter_segment(contracts::SegmentBoundary::Continuation, 2, 1);
    result["continued"] = state.summary();
    try {
        state.confirm_skill(*selection, games::SkillOutcome::Succeeded);
        result["old_generation_rejected"] = false;
    } catch (const std::runtime_error &e) {
        result["old_generation_rejected"] = std::string(e.what()) == "STALE_BUSINESS_SELECTION";
    }
    games::WvdRunState other(profile, {"direct", 2, clock});
    other.enter_segment(contracts::SegmentBoundary::Initial, 1, 0);
    try {
        other.confirm_skill(*selection, games::SkillOutcome::Succeeded);
        result["other_run_rejected"] = false;
    } catch (const std::runtime_error &e) {
        result["other_run_rejected"] = std::string(e.what()) == "STALE_BUSINESS_SELECTION";
    }
    state.observe_combat();
    clock->milliseconds = 5000;
    state.observe_combat();
    state.observe_chest();
    clock->milliseconds = 10000;
    state.resume_dungeon();
    state.resume_dungeon();
    result["timers"] = state.summary();
    state.observe_combat();
    state.restart_game();
    result["restarted"] = state.summary();
    clock->milliseconds = 13000;
    state.dungeon_completed();
    state.dungeon_completed();
    result["counting"] = state.summary();
    auto points = profile;
    points["TASK_SPECIFIC_CONFIG"] = true;
    points["TASK_POINT_STRATEGY"] = {{"overall_strategy", "自定义任务点策略"},
                                     {"task_point", {{"0", "Manual"}, {"1", "Other"}}}};
    points["STRATEGY"].push_back(
        {{"group_name", "Other"},
         {"skill_settings", J::array({profile["STRATEGY"][0]["skill_settings"][1]})}});
    games::WvdRunState point_state(points, {"direct", 3, clock});
    point_state.enter_segment(contracts::SegmentBoundary::Initial, 1, 0);
    point_state.target_point_completed();
    result["task_point"] = point_state.summary();
    points["LANGUAGE"] = "en_US";
    points["TASK_POINT_STRATEGY"]["overall_strategy"] = "Custom Task Point Strategy";
    games::WvdRunState english(points, {"direct", 4, clock});
    english.enter_segment(contracts::SegmentBoundary::Initial, 1, 0);
    english.target_point_completed();
    result["english_task_point"] = english.summary();
    auto each_combat = profile;
    each_combat["RELOAD_STRATEGY_WHEN"] = "每场战斗前";
    games::WvdRunState reloaded(each_combat, {"direct", 5, clock});
    reloaded.enter_segment(contracts::SegmentBoundary::Initial, 1, 0);
    auto consumed = reloaded.select_skill({{"A", .9}});
    reloaded.confirm_skill(*consumed, games::SkillOutcome::Succeeded);
    reloaded.observe_combat();
    result["observing_combat"] = reloaded.summary();
    reloaded.resume_dungeon();
    result["after_combat_reset"] = reloaded.summary();
    reloaded.resurrected();
    result["after_rez"] = reloaded.summary();
    J revival_results = J::object();
    for (const bool failed_combat : {false, true}) {
        games::WvdRunState defeated(profile, {"revival", failed_combat ? 8u : 9u, clock});
        defeated.enter_segment(contracts::SegmentBoundary::Initial, 1, 0);
        auto confirm = [&](const std::string &operation, const std::string &event, unsigned frame) {
            const auto id = defeated.confirmation_id(operation, event);
            require(defeated.confirm_event(id, event, 1, frame), "REVIVAL_EVENT_NOT_APPLIED");
            require(defeated.confirmation_id(operation, event) == id, "REVIVAL_EVENT_ID_UNSTABLE");
            require(!defeated.confirm_event(id, event, 1, frame + 1), "REVIVAL_EVENT_REPLAYED");
            return id;
        };
        std::string old_combat, old_chest;
        for (unsigned repeat = 0; repeat < 2; ++repeat) {
            const auto combat_id = defeated.confirmation_id("combat", "combat_observed");
            const auto chest_id = defeated.confirmation_id("chest", "chest_observed");
            require(combat_id != old_combat && chest_id != old_chest, "DEFEAT_REUSED_ENCOUNTER_ID");
            old_combat = combat_id;
            old_chest = chest_id;
            if (failed_combat) {
                confirm("chest", "chest_observed", repeat * 20 + 1);
                confirm("combat", "combat_observed", repeat * 20 + 3);
            } else {
                confirm("combat", "combat_observed", repeat * 20 + 1);
                confirm("chest", "chest_observed", repeat * 20 + 3);
            }
            confirm("revival", "revival_observed", repeat * 20 + 5);
            confirm("revived", "resurrected", repeat * 20 + 7);
            require(!defeated.confirm_event(old_combat, "combat_observed", 1, repeat * 20 + 9), "OLD_COMBAT_REOPENED");
            confirm("resume", "dungeon_resumed", repeat * 20 + 11);
        }
        const auto key = failed_combat ? "combat_defeats" : "chest_defeats";
        revival_results[key] = defeated.summary();
        confirm("combat", "combat_observed", 50);
        confirm("chest", "chest_observed", 52);
        confirm("resume", "dungeon_resumed", 54);
        revival_results[std::string(key) + "_then_success"] = defeated.summary();
        bool unobserved_rejected = false;
        try {
            defeated.confirm_event("unobserved-revival", "resurrected", 1, 56);
        } catch (const std::runtime_error &e) {
            unobserved_rejected = std::string(e.what()) == "REVIVAL_NOT_OBSERVED";
        }
        require(unobserved_rejected, "UNOBSERVED_REVIVAL_ACCEPTED");
    }
    result["revival_contract"] = revival_results;
    games::WvdRunState party_death(profile, {"party-death", 10, clock});
    party_death.enter_segment(contracts::SegmentBoundary::Initial, 1, 0);
    party_death.observe_combat();
    auto death_skill = party_death.select_skill({{"A", .99}});
    require(death_skill.has_value(), "DEATH_TEST_SKILL_MISSING");
    party_death.confirm_skill(*death_skill, games::SkillOutcome::Succeeded);
    const auto death_id = party_death.confirmation_id("party.death", "party_death_observed");
    party_death.confirm_event(death_id, "party_death_observed", 1, 1);
    require(party_death.confirmation_id("party.death", "party_death_observed") == death_id, "DEATH_ID_CHANGED");
    require(party_death.select_skill({{"A", .99}}).has_value(), "DEATH_STRATEGY_NOT_RESET");
    death_skill = party_death.select_skill({{"A", .99}});
    party_death.confirm_skill(*death_skill, games::SkillOutcome::Succeeded);
    require(!party_death.confirm_event(death_id, "party_death_observed", 1, 2), "DEATH_REPLAYED");
    require(!party_death.select_skill({{"A", .99}}), "DEATH_REPLAY_RESET_STRATEGY");
    party_death.enter_segment(contracts::SegmentBoundary::Continuation, 2, 1);
    const auto clear_id = party_death.confirmation_id("party.death.clear", "party_death_cleared");
    party_death.confirm_event(clear_id, "party_death_cleared", 2, 3);
    require(!party_death.confirm_event(clear_id, "party_death_cleared", 2, 4), "DEATH_CLEAR_REPLAYED");
    result["party_death_contract"] = party_death.summary();
    const auto defeat_id = party_death.confirmation_id("party.defeat", "party_defeat_observed");
    party_death.confirm_event(defeat_id, "party_defeat_observed", 2, 5);
    require(party_death.confirmation_id("party.defeat", "party_defeat_observed") == defeat_id, "DEFEAT_ID_CHANGED");
    require(!party_death.confirm_event(defeat_id, "party_defeat_observed", 2, 6), "DEFEAT_OBSERVATION_REPLAYED");
    party_death.restart_game();
    require(party_death.summary().at("suicide_requested").get<bool>(), "RESTART_INVENTED_SUICIDE_RESET");
    party_death.confirm_event(party_death.confirmation_id("revival.observe", "revival_observed"), "revival_observed", 2, 7);
    party_death.confirm_event(party_death.confirmation_id("revival.complete", "resurrected"), "resurrected", 2, 8);
    require(!party_death.confirm_event(defeat_id, "party_defeat_observed", 2, 9), "OLD_DEFEAT_REOPENED");
    require(party_death.confirmation_id("party.defeat", "party_defeat_observed") != defeat_id, "NEXT_DEFEAT_REUSED_ID");
    result["party_defeat_after_revival"] = party_death.summary();
    {
        games::chest::Selection selection;
        selection.prepare({true, false, true, true, true, true}, 1, 42);
        require(selection.selected() == 1, "FEAR_SELECTED");
        selection.attempted();
        selection.prepare({}, 1, 42);
        require(selection.selected() == 1 && selection.available_mask() == 2, "FEAR_POOL_RESET_IN_SAME_CHEST");
        selection.reset();
        selection.prepare({}, 6, 42);
        require(selection.selected() == 5 && selection.available_mask() == 63, "PREFERRED_FIRST_MISSING");
        bool other_role = false;
        for (unsigned i = 0; i < 16; ++i) {
            selection.attempted();
            selection.prepare({}, 6, 42);
            require(selection.selected().has_value(), "AVAILABLE_ROLE_MISSING");
            other_role = other_role || selection.selected() != 5;
        }
        require(other_role, "PREFERRED_FORCED_AFTER_FIRST_TRY");
        selection.prepare({true, true, true, true, true, true}, 6, 42);
        require(!selection.selected() && !selection.available_mask(), "EMPTY_POOL_SELECTED");
    }
    games::WvdRunState chest_state(profile, {"chest-selection", 10, clock});
    chest_state.enter_segment(contracts::SegmentBoundary::Initial, 1, 0);
    chest_state.observe_chest();
    chest_state.prepare_chest_character({true, false, true, true, true, true}, 1, 42);
    const auto attempt_id = chest_state.confirmation_id("role", "chest_character_attempted");
    chest_state.confirm_event(attempt_id, "chest_character_attempted", 1, 1);
    require(chest_state.confirmation_id("role", "chest_character_attempted") == attempt_id, "CHEST_ATTEMPT_ID_UNSTABLE");
    require(!chest_state.confirm_event(attempt_id, "chest_character_attempted", 1, 2), "CHEST_ATTEMPT_REPLAYED");
    chest_state.enter_segment(contracts::SegmentBoundary::Recovery, 2, 0);
    chest_state.prepare_chest_character({}, 1, 42);
    result["chest_selection_contract"] = chest_state.summary();
    chest_state.resume_dungeon();
    chest_state.observe_chest();
    result["new_chest_selection"] = chest_state.summary();
    games::CombatStrategy missing(points);
    missing.reload(0);
    auto old = missing.select({{"A", .9}});
    missing.reload(2);
    auto empty_before = missing.summary();
    try {
        missing.consume(*old, games::SkillOutcome::Succeeded);
    } catch (const std::runtime_error &) {
    }
    result["missing_group_unchanged"] = missing.summary() == empty_before;
    return result;
}
J profile_storage_contract(const J &descriptor, const J &source, const std::filesystem::path &directory) {
    // 路径来自本用例私有根；不存在的目录是隔离前提，绝不落到旧 config.json。
    require(!std::filesystem::exists(directory), "PROFILE_TEST_DIRECTORY_EXISTS");
    std::filesystem::create_directory(directory);
    const auto path = directory / "profile.json";
    storage::LegacyConfigImporter importer(descriptor);
    storage::ProfileStore store(path, descriptor);
    const auto original = store.create(importer.parse(source));
    const auto original_bytes = bytes(path);
    auto draft = original;
    draft["values"]["KARMA_ADJUST"] = "+1";
    auto attempt = [&](const J &value) {
        try {
            store.compare_exchange(original.at("revision"), value);
            return std::string("SAVED");
        } catch (const std::exception &e) {
            return std::string(e.what());
        }
    };
    struct HeldFile {
        HANDLE handle;
        HeldFile(const std::filesystem::path &p, DWORD access, DWORD sharing, DWORD disposition)
            : handle(CreateFileW(p.c_str(), access, sharing, nullptr, disposition, FILE_ATTRIBUTE_NORMAL, nullptr)) {
            require(handle != INVALID_HANDLE_VALUE, "PROFILE_TEST_LOCK_FAILED");
        }
        ~HeldFile() { CloseHandle(handle); }
    };
    J result;
    {
        HeldFile lock(path.wstring() + L".lock", GENERIC_READ | GENERIC_WRITE, 0, OPEN_EXISTING);
        result["busy_error"] = attempt(draft);
        require(result["busy_error"] == "PROFILE_BUSY" && bytes(path) == original_bytes, "PROFILE_BUSY_LOST_DATA");
    }
    {
        // 允许读取旧 profile，但不给删除共享，实际 Windows 原子替换必须失败。
        HeldFile prevent_replace(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING);
        result["replace_error"] = attempt(draft);
        require(result["replace_error"] == "STORAGE_COMMIT_FAILED" && bytes(path) == original_bytes,
                "PROFILE_FAILED_REPLACE_LOST_DATA");
    }
    result["failure_preserved"] = store.load() == original;
    std::array<std::string, 2> outcomes;
    std::array<J, 2> saved;
    std::barrier ready(3);
    std::array<std::jthread, 2> writers;
    for (std::size_t i = 0; i < writers.size(); ++i)
        writers[i] = std::jthread([&, i] {
            ready.arrive_and_wait();
            try {
                storage::ProfileStore competing(path, descriptor);
                auto proposed = original;
                proposed["values"]["KARMA_ADJUST"] = i ? "+2" : "+1";
                saved[i] = competing.compare_exchange(original.at("revision"), proposed);
                outcomes[i] = "SAVED";
            } catch (const std::exception &e) {
                outcomes[i] = e.what();
            }
        });
    ready.arrive_and_wait();
    for (auto &writer : writers)
        writer.join();
    const auto winners = std::count(outcomes.begin(), outcomes.end(), "SAVED");
    require(winners == 1, "PROFILE_CONCURRENT_WRITERS_BOTH_COMMITTED");
    for (const auto &outcome : outcomes)
        require(outcome == "SAVED" || outcome == "PROFILE_BUSY" || outcome == "PROFILE_CONFLICT",
                "PROFILE_CONCURRENT_UNEXPECTED_ERROR");
    const auto winner = outcomes[0] == "SAVED" ? 0 : 1;
    require(store.load() == saved[winner], "PROFILE_CONCURRENT_PARTIAL_FILE");
    result["writers"] = outcomes;
    result["winner"] = saved[winner];
    result["stale_error"] = attempt(original);
    require(result["stale_error"] == "PROFILE_CONFLICT" && store.load() == saved[winner], "PROFILE_STALE_OVERWROTE_WINNER");
    result["after_stale"] = store.load();
    return result;
}
int main(int argc, char **argv) {
    try {
        require(argc == 2, "PRIVATE_CONFIG_REQUIRED");
        J config;
        std::ifstream(maafw::path_from_utf8(argv[1])) >> config;
        J descriptor;
        std::ifstream(maafw::path_from_utf8(config.at("descriptor"))) >> descriptor;
        storage::LegacyConfigImporter importer(descriptor);
        if (config.value("profile_storage", false)) {
            const auto root = maafw::path_from_utf8(config.at("output")).parent_path() / "profile-storage";
            const J output{{"profile_storage", profile_storage_contract(descriptor, config.at("source"), root)},
                           {"backend_inputs", 0}, {"offline_connections", 0}, {"real_connections", 0}, {"real_inputs", 0}};
            std::ofstream(maafw::path_from_utf8(config.at("output"))) << output.dump(2);
            return 0;
        }
        auto profile = importer.parse(config.at("source")).values;
        if (config.contains("karma_cases")) {
            J output{{"karma_cases", J::array()}, {"backend_inputs", 0}};
            for (const auto &value : config.at("karma_cases")) {
                try {
                    const auto choice = games::choose_karma(value);
                    output["karma_cases"].push_back({{"ambush", choice.ambush}, {"after", choice.after}});
                } catch (const std::exception &e) {
                    output["karma_cases"].push_back({{"error", e.what()}});
                }
            }
            const auto path = maafw::path_from_utf8(config.at("output")).parent_path() / "karma-profile.json";
            storage::ProfileStore store(path, descriptor);
            auto original = store.create(importer.parse(config.at("source")));
            const auto encoded = path.u8string();
            J binding{{"path", std::string(encoded.begin(), encoded.end())}, {"descriptor", descriptor},
                      {"revision", original.at("revision")}};
            auto clock = std::make_shared<TestClock>();
            games::WvdRunState state(profile, {"karma-state", 1, clock}, storage::make_karma_writer(binding, profile));
            state.enter_segment(contracts::SegmentBoundary::Initial, 1, 0);
            bool premature = false;
            try { state.confirm_event("early", "karma_completed", 1, 1); }
            catch (const std::runtime_error &e) { premature = std::string(e.what()) == "KARMA_NOT_OBSERVED"; }
            require(premature && store.load() == original, "KARMA_SAVED_WITHOUT_OBSERVATION");
            state.confirm_event(state.confirmation_id("observe", "karma_observed"), "karma_observed", 1, 2);
            require(store.load() == original, "KARMA_OBSERVATION_WROTE_PROFILE");
            const auto operation = state.confirmation_id("complete", "karma_completed");
            state.confirm_event(operation, "karma_completed", 1, 3);
            const auto saved = store.load();
            require(saved.at("values").at("KARMA_ADJUST") == "+2", "KARMA_WRONG_UPDATE");
            require(!state.confirm_event(operation, "karma_completed", 1, 4) && store.load() == saved,
                    "KARMA_DUPLICATE_WRITE");
            state.enter_segment(contracts::SegmentBoundary::Continuation, 2, 1);
            require(!state.confirm_event(operation, "karma_completed", 2, 5) && store.load() == saved,
                    "KARMA_GENERATION_DUPLICATE_WRITE");
            output["karma_receipt"] = state.summary().at("karma_effect");
            state.confirm_event(state.confirmation_id("observe", "karma_observed"), "karma_observed", 2, 6);
            state.confirm_event(state.confirmation_id("complete", "karma_completed"), "karma_completed", 2, 7);
            require(store.load().at("values").at("KARMA_ADJUST") == "+1", "KARMA_REUSED_FROZEN_VALUE");
            output["karma_second"] = state.summary().at("karma_effect");
            std::ofstream(maafw::path_from_utf8(config.at("output"))) << output.dump(2);
            return 0;
        }
        J output{{"direct", direct_contract(profile)}};
        if (config.contains("supply_cases")) {
            output["supply_cases"] = J::array();
            for (const auto &item : config.at("supply_cases")) {
                auto configured = profile;
                configured.update(item.at("profile"));
                const auto &facts = item.at("facts");
                const auto decision = games::supply::decide_rest(
                    configured,
                    {facts.at("dungeons"), facts.at("met_encounter"), facts.at("total_seconds"),
                     facts.at("last_bag_clear")},
                    item.value("pickaxes_exhausted", false));
                output["supply_cases"].push_back({{"reason", int(decision.reason)},
                                                  {"required", decision.required()},
                                                  {"reassemble", decision.reassemble},
                                                  {"royal_suite", decision.royal_suite}});
            }
            auto clock = std::make_shared<TestClock>();
            games::WvdRunState supply_state(profile, {"supply", 1, clock});
            supply_state.enter_segment(contracts::SegmentBoundary::Initial, 1, 0);
            output["forced_state_rest"] = int(supply_state.rest_decision(true).reason);
        }
        auto registry = std::make_shared<runtime::BehaviorRegistry>("m4-state-tests-1");
        games::register_wvd_state(*registry);
        games::vision::register_wvd(*registry);
        registry->add_action({"test.state", "1"}, state_probe);
        registry->add_action({"test.wait", "1"}, [](maafw::Context &context, const J &, const J &) {
            context.business_event("fixture.wait_entered", J::object());
            while (!context.cancelled())
                std::this_thread::sleep_for(5ms);
            return false;
        });
        registry->add_recovery({"test.recover_unit", "1"}, recover_unit);
        registry->seal();
        output["registry"] = registry->manifest();
        auto device = std::make_shared<StateDevice>();
        device->block_capture = config.value("stop_during_capture", false);
        device->before = bytes(maafw::path_from_utf8(config.at("frame")));
        device->after = device->before;
        maafw::Bundle bundle{maafw::path_from_utf8(config.at("bundle")), "m4-state-fixture", {}};
        for (const auto &file : config.at("files"))
            bundle.files.push_back({file.at("path"), file.at("sha256")});
        runtime::RunDefinition definition;
        definition.request_id = "first";
        definition.policy = {
            "m2-offline", "wvd", "fixture.app", bundle.revision, "portrait", {900, 1600}, {},
            {},           {},    2000ms};
        auto session = [&](int index) {
            runtime::SessionDefinition d{bundle,
                                         "Unit" + std::to_string(index),
                                         "Terminal" + std::to_string(index),
                                         {{"StateProbe", {"test.state", "1"}, J::object()},
                                          {"Wait", {"test.wait", "1"}, J::object()}},
                                         5000ms,
                                         200ms};
            d.checkpoint_node = "Checkpoint" + std::to_string(index);
            d.recognitions = {games::vision::binding(J::object())};
            return d;
        };
        definition.initial = session(0);
        definition.state_factory = games::wvd_state_binding(profile);
        definition.max_business_units = config.value("max_units", 2);
        if (config.value("continue", true))
            definition.continuation_units.push_back(session(1));
        if (config.value("recovery", false)) {
            definition.recover =
                contracts::BehaviorBinding{"RecoverUnit", {"test.recover_unit", "1"}, J::object()};
            definition.recovery_limit = 1;
        }
        if (config.value("unknown_factory", false))
            definition.state_factory->implementation.revision = "unknown";
        runtime::RunCoordinator coordinator(maafw::path_from_utf8(config.at("run_root")), registry);
        struct ReleaseCapture {
            std::shared_ptr<StateDevice> device;
            ~ReleaseCapture() { device->release_capture = true; }
        } release_capture{device};
        try {
            coordinator.start(definition, device);
            if (config.value("mutate_definition", false))
                definition.state_factory->parameters["profile"]["STRATEGY"] = J::array();
            if (config.value("stop_during_capture", false)) {
                until([&] { return device->capture_waiting.load(); });
                coordinator.request_stop();
                until([&] { return coordinator.snapshot().reason == "STOP_TIMEOUT"; });
                const auto pending = coordinator.snapshot();
                require(!pending.quiescent && pending.generation == 1, "BLOCKED_CAPTURE_RELEASED_EARLY");
                output["stop_pending"] = storage::snapshot_json(pending);
                device->release_capture = true;
            } else if (config.value("stop", false)) {
                // Running 早于首个 Custom 进入；不能把原生截图等待误当成协作取消回调。
                until([&] {
                    const auto events = coordinator.events();
                    for (const auto &event : events.at("events"))
                        if (event.at("type") == "business.fixture.wait_entered")
                            return true;
                    return false;
                });
                coordinator.request_stop();
            }
            require(coordinator.wait_for(10000ms), "RUN_NOT_FINISHED");
            output["snapshot"] = storage::snapshot_json(coordinator.snapshot());
            output["events"] = coordinator.events();
            J stored;
            std::ifstream(coordinator.run_directory() / "run.json") >> stored;
            output["frozen_definition"] = stored.at("definition");
            if (config.value("new_run", false)) {
                definition.request_id = "second";
                definition.state_factory = games::wvd_state_binding(profile);
                coordinator.start(definition, device);
                require(coordinator.wait_for(10000ms), "SECOND_RUN_NOT_FINISHED");
                output["second"] = storage::snapshot_json(coordinator.snapshot());
            }
        } catch (const std::exception &e) {
            output["start_error"] = e.what();
        }
        output["offline_connections"] = device->connections.load();
        output["backend_inputs"] = device->calls.load();
        output["real_connections"] = 0;
        output["real_inputs"] = 0;
        std::ofstream(maafw::path_from_utf8(config.at("output"))) << output.dump(2);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what();
        return 1;
    }
}
