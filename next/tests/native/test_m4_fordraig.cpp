#include "games/wvd/state.hpp"
#include "games/wvd/quests/fordraig.hpp"
#include "games/wvd/tasks/fordraig.hpp"
#include "games/wvd/tasks/featured_request.hpp"
#include "games/wvd/vision/asset_resolver.hpp"
#include "maafw/buffers.hpp"
#include "storage/legacy_import.hpp"
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <utility>

using namespace wvd;
using J = nlohmann::json;
using Phase = games::quests::FordraigCycle::Phase;
using Boundary = contracts::SegmentBoundary;

namespace {
void require(bool value, const std::string &message) {
    if (!value) throw std::runtime_error(message);
}
template <class F> void rejected(F &&action, const std::string &message) {
    bool caught = false;
    try { action(); } catch (const std::runtime_error &) { caught = true; }
    require(caught, message);
}
J read(const std::filesystem::path &path) {
    std::ifstream input(path);
    require(input.good(), "FORDRAIG_TEST_INPUT_MISSING");
    return J::parse(input);
}
class TestClock final : public contracts::MonotonicClock {
  public:
    TimePoint now() const noexcept override { return TimePoint{} + std::chrono::milliseconds{milliseconds}; }
    std::int64_t milliseconds{};
};

J domain_contract() {
    games::quests::FordraigCycle cycle;
    require(!cycle.force_automatic() && !cycle.continuation_ready(0), "FORDRAIG_DOMAIN_NOT_FRESH");
    rejected([&] { cycle.start(247, 0); }, "FORDRAIG_DOMAIN_UNIT_OVERFLOW_ACCEPTED");
    cycle.start(0, 4);
    rejected([&] { cycle.start(0, 4); }, "FORDRAIG_DOMAIN_DUPLICATE_START");
    rejected([&] { cycle.advance(Phase::Leap, 0, 4, 0); }, "FORDRAIG_DOMAIN_UNPREPARED_LEAP");
    cycle.prepare(Phase::Leap, 0);
    rejected([&] { cycle.prepare(Phase::Leap, 0); }, "FORDRAIG_DOMAIN_DUPLICATE_PREPARE");
    require(!cycle.continuation_ready(0), "FORDRAIG_DOMAIN_PENDING_CONTINUED");
    cycle.advance(Phase::Leap, 0, 4, 0);
    require(cycle.continuation_ready(0), "FORDRAIG_DOMAIN_LEAP_NOT_CONTINUABLE");
    rejected([&] { cycle.advance(Phase::Request, 0, 5, 0); }, "FORDRAIG_DOMAIN_WRONG_UNIT");
    rejected([&] { cycle.advance(Phase::Enter, 1, 5, 0); }, "FORDRAIG_DOMAIN_WRONG_PHASE");
    for (const auto visits : {4u, 6u})
        rejected([&] { cycle.advance(Phase::Request, 1, visits, 0); }, "FORDRAIG_DOMAIN_VISIT_DELTA_INVALID");
    cycle.advance(Phase::Request, 1, 5, 0);
    cycle.advance(Phase::Enter, 2, 5, 0);
    const std::array<Phase, 6> phases{Phase::Trap1Route, Phase::Trap2Route, Phase::Trap3, Phase::PreBoss, Phase::Boss, Phase::Exit};
    // 预期来自固定旧源的六次 StateDungeon 调用，不从被测 route_points 反推。
    const std::array<std::size_t, 6> counts{2, 2, 4, 1, 1, 1};
    for (std::size_t index = 0; index < phases.size(); ++index) {
        const auto unit = index + 3;
        require(cycle.force_automatic() == (phases[index] != Phase::Boss), "FORDRAIG_DOMAIN_AUTO_SCOPE");
        require(games::quests::FordraigCycle::route_points(phases[index]) == counts[index], "FORDRAIG_DOMAIN_POINT_COUNT");
        for (const auto count : {counts[index] - 1, counts[index] + 1})
            rejected([&] { cycle.advance(phases[index], unit, 5, count); }, "FORDRAIG_DOMAIN_INCOMPLETE_ROUTE");
        cycle.advance(phases[index], unit, 5, counts[index]);
        if (index < 2) {
            const auto push = index ? Phase::Trap2Push : Phase::Trap1Push;
            require(!cycle.continuation_ready(unit), "FORDRAIG_DOMAIN_SKIPPED_PUSH");
            rejected([&] { cycle.advance(push, unit, 5, counts[index]); }, "FORDRAIG_DOMAIN_UNPREPARED_PUSH");
            cycle.prepare(push, unit);
            cycle.advance(push, unit, 5, counts[index]);
        }
        require(cycle.continuation_ready(unit), "FORDRAIG_DOMAIN_STAGE_NOT_CONTINUABLE");
    }
    cycle.advance(Phase::Return, 9, 5, 1);
    require(!cycle.force_automatic() && cycle.continuation_ready(9), "FORDRAIG_DOMAIN_RETURN_INCOMPLETE");
    const auto completed = cycle.summary(9);
    rejected([&] { cycle.start(11, 5); }, "FORDRAIG_DOMAIN_SKIPPED_CYCLE");
    cycle.start(10, 5);
    return {{"completed", completed}, {"next_cycle", cycle.summary(10)}};
}

// 只调用真实状态所有者；这里的帧号是状态契约输入，不冒充视觉或战斗执行证据。
class StateDriver {
  public:
    explicit StateDriver(const J &profile) : clock(std::make_shared<TestClock>()), state(profile, {"fordraig-test", 1, clock}) {
        enter(Boundary::Initial, 0);
    }
    void enter(Boundary boundary, std::size_t unit) {
        state.enter_segment(boundary, generation + 1, unit);
        ++generation;
    }
    void apply(const std::string &event, const std::string &operation = "", std::optional<std::size_t> point = {}) {
        const auto id = state.confirmation_id(operation.empty() ? event : operation, event);
        require(state.confirm_event(id, event, generation, ++frame, point), "FORDRAIG_STATE_EVENT_NOT_APPLIED:" + event);
        const auto after = state.summary();
        require(!state.confirm_event(id, event, generation, ++frame, point), "FORDRAIG_STATE_EVENT_REPLAYED:" + event);
        require(state.summary() == after, "FORDRAIG_STATE_REPLAY_CHANGED_FACTS");
    }
    template <class F> void unchanged_rejection(F &&action, const std::string &message) {
        const auto before = state.summary();
        rejected(std::forward<F>(action), message);
        require(state.summary() == before, "FORDRAIG_STATE_REJECTION_CHANGED_FACTS:" + message);
    }
    void missing(const std::string &event) {
        unchanged_rejection([&] { state.confirm_event(state.confirmation_id(event, event), event, generation, ++frame); },
            "FORDRAIG_STATE_INVALID_EVENT_ACCEPTED:" + event);
    }
    void points(std::size_t count) {
        for (std::size_t point = 0; point < count; ++point)
            apply("target_completed", "fordraig.test.point." + std::to_string(point), point);
    }
    std::shared_ptr<TestClock> clock;
    games::WvdRunState state;
    std::uint64_t generation{}, frame{};
};

J state_cycles(const J &profile, bool recovery) {
    StateDriver driver(profile);
    auto &state = driver.state;
    const bool original_auto = state.summary().at("strategy").at("automatic");
    J boss_states = J::array();
    for (std::size_t cycle = 0; cycle < 2; ++cycle) {
        const auto base = cycle * 10;
        if (cycle) driver.enter(Boundary::Continuation, base);
        driver.apply("fordraig_started");
        require(state.summary().at("strategy").at("automatic") == true, "FORDRAIG_STATE_NONBOSS_NOT_AUTO");
        driver.missing("fordraig_leaped");
        driver.apply("fordraig_leap_prepared");
        driver.unchanged_rejection([&] { state.enter_segment(Boundary::Continuation, driver.generation + 1, base + 1); },
            "FORDRAIG_STATE_PENDING_CONTINUATION_ACCEPTED");
        if (recovery) {
            driver.enter(Boundary::Recovery, base);
            require(state.summary().at("fordraig").at("leap_pending") == true, "FORDRAIG_STATE_LEAP_INTENT_LOST");
        }
        driver.clock->milliseconds += 15000;
        driver.apply("fordraig_leaped");
        driver.enter(Boundary::Continuation, base + 1);
        driver.missing("fordraig_requested");
        driver.apply("featured_visit_started");
        driver.apply("inn_payment_prepared");
        driver.apply("inn_rest_completed");
        // 第二轮覆盖旧任务已领取：访问完成，但不再产生选择回执。
        if (!cycle) { driver.apply("featured_request_prepared"); driver.apply("featured_request_completed"); }
        driver.apply("featured_visit_completed");
        driver.apply("fordraig_requested");
        driver.enter(Boundary::Continuation, base + 2);
        driver.apply("fordraig_entered");
        const std::array<std::size_t, 6> counts{2, 2, 4, 1, 1, 1};
        const std::array<std::string, 6> events{"fordraig_trap1_routed", "fordraig_trap2_routed", "fordraig_trap3_completed",
            "fordraig_preboss_completed", "fordraig_boss_completed", "fordraig_exited"};
        for (std::size_t index = 0; index < counts.size(); ++index) {
            driver.enter(Boundary::Continuation, base + 3 + index);
            driver.apply("dungeon_entered");
            require(state.summary().at("task_step") == 0, "FORDRAIG_STATE_ROUTE_NOT_RESET");
            const auto snapshot = state.summary();
            require(snapshot.at("strategy").at("automatic") == (index == 4 ? original_auto : true), "FORDRAIG_STATE_BOSS_POLICY_CHANGED");
            driver.missing(events[index]);
            if (index == 4) {
                boss_states.push_back(snapshot.at("strategy"));
                driver.apply("combat_observed");
                driver.apply("dungeon_resumed");
                if (!original_auto && !cycle) {
                    const auto selected = state.select_skill({{"A", .95}});
                    require(selected.has_value(), "FORDRAIG_STATE_BOSS_SKILL_MISSING");
                    require(state.confirm_skill(*selected, games::SkillOutcome::Succeeded), "FORDRAIG_STATE_BOSS_SKILL_NOT_CONSUMED");
                }
            }
            driver.points(counts[index]);
            driver.apply(events[index]);
            if (index < 2) {
                const std::string prefix = index ? "fordraig_trap2" : "fordraig_trap1";
                driver.missing(prefix + "_completed");
                if (recovery) {
                    const auto before = state.summary().at("fordraig");
                    driver.enter(Boundary::Recovery, base + 3 + index);
                    require(state.summary().at("fordraig") == before, "FORDRAIG_STATE_PUSH_PHASE_LOST");
                }
                driver.apply(prefix + "_prepared");
                driver.apply(prefix + "_completed");
            }
            require(state.summary().at("fordraig").at("continuation_ready") == true, "FORDRAIG_STATE_STAGE_NOT_CONTINUABLE");
        }
        driver.enter(Boundary::Continuation, base + 9);
        driver.apply("fordraig_completed");
    }
    games::WvdRunState fresh(profile, {"fordraig-test-fresh", 2, driver.clock});
    fresh.enter_segment(Boundary::Initial, 1, 0);
    require(fresh.summary().at("fordraig").at("completed_cycles") == 0, "FORDRAIG_STATE_CROSS_RUN_LEAK");
    require(fresh.summary().at("strategy").at("automatic") == original_auto, "FORDRAIG_STATE_FROZEN_PROFILE_CHANGED");
    if (!original_auto) {
        require(boss_states.at(0).at("current").at("skill_settings").size() == 2, "FORDRAIG_STATE_INITIAL_ROWS_CHANGED");
        require(boss_states.at(1).at("current").at("skill_settings").size() == 1, "FORDRAIG_STATE_CONSUMPTION_RESET");
    }
    return {{"state", state.summary()}, {"boss_strategies", boss_states}, {"fresh_state", fresh.summary()}};
}

J pending_contract(const J &profile) {
    J snapshots = J::array();
    for (const auto boundary : {Boundary::Recovery, Boundary::LifecycleRecovery}) {
        StateDriver driver(profile);
        driver.apply("fordraig_started");
        driver.apply("fordraig_leap_prepared");
        const auto before = driver.state.summary().at("fordraig");
        driver.enter(boundary, 0);
        require(driver.state.summary().at("fordraig") == before, "FORDRAIG_PENDING_LOST_ON_RECOVERY");
        driver.unchanged_rejection([&] { driver.state.confirm_event("stale", "fordraig_leaped", driver.generation - 1, 1); }, "FORDRAIG_STALE_GENERATION_ACCEPTED");
        driver.unchanged_rejection([&] { driver.state.confirm_event("empty-frame", "fordraig_leaped", driver.generation, 0); }, "FORDRAIG_ZERO_FRAME_ACCEPTED");
        driver.missing("fordraig_completed");
        driver.missing("fordraig_entered");
        snapshots.push_back(driver.state.summary());
    }
    return snapshots;
}

J plan_contract(const J &config, const J &profile) {
    const auto baseline = read(maafw::path_from_utf8(config.at("quests")));
    require(baseline.size() == 58 && !baseline.contains("fordraig") && baseline.contains("fordraig-B3F"), "FORDRAIG_BASELINE_CHANGED");
    const nlohmann::ordered_json mod{{"fordraig", {{"_TYPE", "quest"}, {"questName", "鸟剑"}}}};
    J diagnostics = J::array();
    const auto merged = storage::merge_legacy_quests(baseline, mod, diagnostics);
    require(merged.size() == 59 && diagnostics.empty(), "FORDRAIG_MOD_NOT_IMPORTED");
    for (const auto &[id, value] : baseline.items()) require(merged.at(id) == value, "FORDRAIG_MOD_CHANGED_BASELINE");
    games::WvdQuestCatalog catalog(merged);
    const auto &definition = catalog.at("fordraig");
    const auto plan = games::tasks::fordraig_plan(definition);
    rejected([&] { games::tasks::fordraig_plan(catalog.at("fordraig-B3F")); }, "FORDRAIG_BASELINE_ALIASED_TO_EXTENSION");
    auto invalid = definition; invalid.type = "dungeon";
    rejected([&] { games::tasks::fordraig_plan(invalid); }, "FORDRAIG_WRONG_TYPE_ACCEPTED");
    const auto manifest = read(maafw::path_from_utf8(config.at("manifest")));
    maafw::Bundle bundle;
    std::set<std::string> images, files;
    for (const auto &file : manifest.at("files")) {
        const auto path = file.at("path").get<std::string>();
        bundle.files.push_back({path, file.at("sha256")}); files.insert(path);
        if (path.starts_with("image/")) images.insert(path.substr(6));
    }
    J stages = J::array();
    std::vector<runtime::SessionDefinition> sessions;
    for (const auto &workflow : games::tasks::fordraig_cycle(definition, profile, images, true)) {
        workflow.validate();
        J resolved = J::object();
        for (const auto &image : workflow.images) {
            const auto source = games::vision::resolve_image_source(bundle, manifest.at("aliases"), image);
            require(files.contains(source.relative_path), "FORDRAIG_RESOURCE_MISSING:" + image);
            resolved[image] = source.relative_path;
        }
        stages.push_back({{"kind", workflow.kind}, {"time_limit_ms", workflow.time_limit.count()},
            {"dialogue", games::recovery::dialogue_policy_name(workflow.dialogue_policy)},
            {"nodes", workflow.nodes}, {"resolved_images", resolved}, {"required_actions", workflow.required_actions}});
        // 只为配置函数构造内存定义；不发布包、不创建 RunCoordinator 或 DeviceBackend。
        runtime::SessionDefinition session;
        session.entry = workflow.kind;
        session.checkpoint_node = workflow.checkpoint;
        session.time_limit = workflow.time_limit;
        sessions.push_back(std::move(session));
    }
    runtime::RunDefinition definition_units;
    games::tasks::configure_fordraig_units(definition_units, sessions, 2);
    J order{definition_units.initial.entry};
    for (const auto &session : definition_units.continuation_units) order.push_back(session.entry);
    require(order.size() == 20, "FORDRAIG_NORMAL_UNIT_COUNT");
    rejected([&] { games::tasks::configure_fordraig_units(definition_units, sessions); }, "FORDRAIG_DOUBLE_CONFIGURATION_ACCEPTED");
    for (const auto cycles : {0u, 26u}) {
        runtime::RunDefinition invalid_units;
        rejected([&] { games::tasks::configure_fordraig_units(invalid_units, sessions, cycles); }, "FORDRAIG_INVALID_CYCLE_COUNT_ACCEPTED");
    }
    auto invalid_sessions = sessions;
    invalid_sessions.pop_back();
    runtime::RunDefinition invalid_units;
    rejected([&] { games::tasks::configure_fordraig_units(invalid_units, invalid_sessions); }, "FORDRAIG_INVALID_STAGE_COUNT_ACCEPTED");
    invalid_sessions = sessions; invalid_sessions.front().time_limit = std::chrono::seconds{1801};
    rejected([&] { games::tasks::configure_fordraig_units(invalid_units, invalid_sessions); }, "FORDRAIG_OVERSIZED_SESSION_ACCEPTED");
    return {{"plan", plan.inspect()}, {"baseline_size", baseline.size()}, {"merged_size", merged.size()},
        {"stages", stages}, {"unit_order", order}, {"normal_units", definition_units.max_business_units}};
}
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    try {
        const auto config = read(maafw::path_from_utf8(argv[1]));
        require(!config.contains("device") && !config.contains("binding") && !config.value("execute", false), "FORDRAIG_TEST_EXECUTION_FORBIDDEN");
        storage::LegacyConfigImporter importer(read(maafw::path_from_utf8(config.at("descriptor"))));
        const auto profile = importer.parse(config.at("source")).values;
        const auto mode = config.at("case").get<std::string>();
        J output{{"case", mode}, {"workflow_executed", false}};
        if (mode == "domain") output["result"] = domain_contract();
        else if (mode == "state") output["result"] = state_cycles(profile, config.value("recovery", false));
        else if (mode == "pending") output["result"] = pending_contract(profile);
        else if (mode == "plan") output["result"] = plan_contract(config, profile);
        else throw std::runtime_error("FORDRAIG_TEST_CASE_UNKNOWN");
        output["outcome"] = "PASS";
        const auto path = maafw::path_from_utf8(config.at("output"));
        require(path.is_absolute() && !std::filesystem::exists(path), "FORDRAIG_TEST_OUTPUT_NOT_NEW");
        std::ofstream stream(path);
        stream << output.dump(2); stream.close();
        require(!stream.fail(), "FORDRAIG_TEST_OUTPUT_FAILED");
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
