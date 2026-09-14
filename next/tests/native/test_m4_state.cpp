#include "runtime_fixture.hpp"
#include "games/wvd/state.hpp"
#include "games/wvd/vision/recognizers.hpp"
#include "storage/legacy_import.hpp"
#include <iostream>

using namespace fixture;
class TestClock final : public contracts::MonotonicClock {
  public:
    std::atomic<std::int64_t> milliseconds{};
    TimePoint now() const noexcept override {
        return TimePoint{} + std::chrono::milliseconds(milliseconds.load());
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
int main(int argc, char **argv) {
    try {
        require(argc == 2, "PRIVATE_CONFIG_REQUIRED");
        J config;
        std::ifstream(maafw::path_from_utf8(argv[1])) >> config;
        J descriptor;
        std::ifstream(maafw::path_from_utf8(config.at("descriptor"))) >> descriptor;
        storage::LegacyConfigImporter importer(descriptor);
        auto profile = importer.parse(config.at("source")).values;
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
            while (!context.cancelled())
                std::this_thread::sleep_for(5ms);
            return false;
        });
        registry->add_recovery({"test.recover_unit", "1"}, recover_unit);
        registry->seal();
        output["registry"] = registry->manifest();
        auto device = std::make_shared<OfflineDevice>();
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
        try {
            coordinator.start(definition, device);
            if (config.value("mutate_definition", false))
                definition.state_factory->parameters["profile"]["STRATEGY"] = J::array();
            if (config.value("stop", false)) {
                until([&] { return coordinator.snapshot().state == contracts::RunState::Running; });
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
