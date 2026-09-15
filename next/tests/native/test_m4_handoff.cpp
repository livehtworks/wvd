#include "runtime_fixture.hpp"
#include "loaded_modules.hpp"
#include "games/wvd/state.hpp"
#include "games/wvd/diagnostics.hpp"
#include "games/wvd/combat/turn.hpp"
#include "games/wvd/chest/chest.hpp"
#include "games/wvd/recovery/boot.hpp"
#include "games/wvd/recovery/leap_wait.hpp"
#include "games/wvd/tasks/task_handoff.hpp"
#include "games/wvd/tasks/gold_income.hpp"
#include "games/wvd/tasks/workflow_session.hpp"
#include "games/wvd/vision/recognizers.hpp"
#include "storage/legacy_import.hpp"
#include "platform/windows/file_digest.hpp"
#include <iostream>

using namespace fixture;
namespace {
class Clock final : public contracts::MonotonicClock {
  public:
    std::atomic<std::int64_t> milliseconds{};
    TimePoint now() const noexcept override { return TimePoint{std::chrono::milliseconds{milliseconds.load()}}; }
};
class Device final : public OfflineDevice, public devices::LifecyclePort {
  public:
    devices::LifecycleObservation lifecycle{
        {"m4-handoff-offline", "handoff-instance", "fixture.app", "", false},
        true, true, true, false, 1, {}, true};
    J lifecycle_calls = J::array();
    std::atomic<bool> restarted{};
    bool wait_case{};
    std::filesystem::path source_result;
    devices::LifecyclePort *lifecycle_port() override { return this; }
    std::optional<devices::LifecycleObservation> observe_lifecycle() override {
        std::lock_guard lock(mutex);
        auto copy = lifecycle;
        copy.observed_at = std::chrono::steady_clock::now();
        return copy;
    }
    bool execute_lifecycle(devices::LifecycleOperation operation, const devices::LifecycleTarget &target,
                           const std::function<bool()> &cancelled) override {
        std::lock_guard lock(mutex);
        if (cancelled())
            return false;
        require(target.device_id == identity && target.instance_id == "handoff-instance", "WRONG_LIFECYCLE_TARGET");
        if (operation == devices::LifecycleOperation::StopApplication) {
            lifecycle_calls.push_back("StopApplication");
            lifecycle.application_running = lifecycle.application_foreground = false;
        } else if (operation == devices::LifecycleOperation::StartApplication) {
            lifecycle_calls.push_back("StartApplication");
            lifecycle.application_running = lifecycle.application_foreground = true;
            restarted = true;
        } else if (operation == devices::LifecycleOperation::EnsureVpn) {
            require(target.vpn_required && target.vpn_application_id == "fixture.vpn", "VPN_NOT_AUTHORIZED");
            lifecycle_calls.push_back("EnsureVpn");
            lifecycle.vpn_ready = true;
        } else
            throw std::runtime_error("UNEXPECTED_LIFECYCLE_UPGRADE");
        return true;
    }
    devices::RawFrame capture() override {
        ++captures;
        return {(wait_case ? restarted.load() : changed.load()) ? after : before,
            size, identity, viewport, application, std::chrono::steady_clock::now(),
            "handoff-fixture", 1};
    }
    bool execute(const contracts::Command &command) override {
        // 核心交接断言检查真实backend调用时的旧结果，不用模拟的quiescent布尔值。
        J saved;
        std::ifstream input(source_result);
        require(bool(input) && bool(input >> saved), "INPUT_BEFORE_SOURCE_RESULT");
        require(saved.at("quiescent").get<bool>() && saved.at("result_saved").get<bool>() &&
            saved.at("state") == "Interrupted", "INPUT_BEFORE_SOURCE_QUIESCENT");
        return OfflineDevice::execute(command);
    }
};
J read(const std::filesystem::path &path) {
    J value;
    std::ifstream input(path);
    require(bool(input) && bool(input >> value), "FIXTURE_DOCUMENT_MISSING");
    return value;
}
games::tasks::CompiledWorkflow source_workflow() {
    using C = games::tasks::PipelineCompiler;
    C graph("fixture.unknown_leap", 120s);
    graph.route("Entry", {"Known", "Sample", "CheckLeap"});
    graph.confirm("Known", "fixture.dungeon", "dungeon_entered", C::image("dungFlag"), {"Terminal"});
    graph.observe("Sample", {{"mode", "unknown_frozen"}}, {"Frozen"});
    graph.recovery("Frozen", "dungeon.unknown_static_window");
    graph.route("CheckLeap", {"Leap", "Again"});
    graph.unknown_leap("Leap", {"LeapExit"});
    graph.recovery("LeapExit", "leap.unknown");
    graph.route("Again", {"Entry"});
    graph.delay_after("Again", 1100);
    for (auto name : {"Entry", "CheckLeap", "Again"}) graph.hit_limit(name, 8);
    return games::recovery::with_boot_recovery(graph.finish(), false);
}
runtime::SessionDefinition fixture_session(const games::tasks::CompiledWorkflow &workflow,
    const maafw::Bundle &assets, const std::filesystem::path &destination, const runtime::BehaviorRegistry &registry) {
    // 有限外层测试场景使用正式编译器和发布器，不在生成后篡改节点或绕过资源封存。
    auto session = games::tasks::publish_workflow(workflow, assets, registry, destination,
        {{"returntoTown.png", "returntotown.png"}});
    session.stop_timeout = 200ms;
    return session;
}
J state_boundaries(games::WvdProfile profile, const games::WvdQuestCatalog &catalog,
                   const std::shared_ptr<const runtime::BehaviorRegistry> &registry) {
    auto clock = std::make_shared<Clock>();
    profile.values["ACTIVE_BEG_MONEY"] = true;
    runtime::RunDefinition definition;
    definition.request_id = "state-boundaries";
    definition.state_factory = games::wvd_state_binding(profile.values);
    auto source = games::tasks::freeze_handoff_source(definition, profile, catalog.at("fortress-B8F_trap"), catalog);
    definition.state_factory->parameters["handoff_source"] = source;
    auto state = registry->create_state(*definition.state_factory, {"state-test", 1, clock});
    state->enter_segment(contracts::SegmentBoundary::Initial, 1, 0);
    auto &wvd = dynamic_cast<games::WvdRunState &>(*state);
    require(!wvd.observe_unknown_leap(4, 1, 4), "FOURTH_UNKNOWN_TRIGGERED");
    require(wvd.observe_unknown_leap(5, 1, 5), "FIFTH_UNKNOWN_MISSING");
    require(!wvd.observe_unknown_leap(6, 1, 6), "INTENT_REPLAYED");
    auto stale = std::string{};
    try { (void)wvd.observe_unknown_leap(6, 2, 6); }
    catch (const std::exception &e) { stale = e.what(); }
    require(stale == "LEAP_OBSERVATION_STALE", "STALE_OBSERVATION_ACCEPTED");
    auto bad = source;
    bad["task"]["id"] = "7000G";
    auto changed = std::string{};
    try { games::tasks::validate_handoff_source(bad, profile.values); }
    catch (const std::exception &e) { changed = e.what(); }
    require(changed == "HANDOFF_SOURCE_INVALID", "SOURCE_MUTATION_ACCEPTED");
    games::recovery::LeapWait wait;
    wait.begin(clock->now());
    J slices = J::array();
    for (std::uint64_t i = 1; i <= 5; ++i) {
        require(!wait.poll(clock->now(), i), "WAIT_SEGMENT_STARTED_COMPLETE");
        clock->milliseconds = static_cast<std::int64_t>(i) * 1460000 - 1;
        require(!wait.poll(clock->now(), i), "WAIT_EARLY_BOUNDARY");
        ++clock->milliseconds;
        require(wait.poll(clock->now(), i), "WAIT_BOUNDARY_MISSING");
        slices.push_back(wait.summary(clock->now()));
    }
    require(wait.summary(clock->now()).at("ready").get<bool>(), "WAIT_7300_NOT_READY");
    auto reversed = std::string{};
    --clock->milliseconds;
    try { (void)wait.summary(clock->now()); }
    catch (const std::exception &e) { reversed = e.what(); }
    require(reversed == "WVD_CLOCK_MOVED_BACKWARD", "CLOCK_REVERSAL_ACCEPTED");
    ++clock->milliseconds;
    wait.restarted(clock->now());
    require(!wait.summary(clock->now()).at("active").get<bool>(), "WAIT_RESTART_NOT_CLEARED");
    return {{"state", state->summary()}, {"stale_error", stale}, {"source_error", changed},
        {"clock_error", reversed}, {"slices", slices}, {"native_run_executed", false}};
}
J extension_state_boundaries(games::WvdProfile profile,
                            const std::shared_ptr<const runtime::BehaviorRegistry> &registry) {
    J output;
    for (const auto *kind : {"fordraig", "CaveOfSeperation"}) {
        auto values = profile.values;
        values["FARM_TARGET"] = kind;
        values["DEFAULT_OVERALL_STRATEGY"] = "Manual";
        values["STRATEGY"] = J::array({{{"group_name", "Manual"}, {"complete_one_as_all", false},
            {"skill_settings", J::array({{{"role_var", "A"}, {"skill_var", "fixture-skill"},
                {"target_var", "next"}, {"freq_var", "fixture-frequency"}, {"skill_lvl", 1}}})}}});
        auto clock = std::make_shared<Clock>();
        auto state = registry->create_state(games::wvd_state_binding(values), {"extensions", 1, clock});
        auto &wvd = dynamic_cast<games::WvdRunState &>(*state);
        std::uint64_t generation = 1, frame = 1;
        std::size_t unit = 0, serial = 0;
        state->enter_segment(contracts::SegmentBoundary::Initial, generation, unit);
        const auto original_strategy = state->summary().at("strategy").at("current");
        require(!state->summary().at("strategy").at("automatic").get<bool>(), "MANUAL_FIXTURE_REQUIRED");
        auto emit = [&](const std::string &event, std::optional<std::size_t> point = {}) {
            const auto operation = wvd.confirmation_id("fixture." + std::to_string(++serial), event);
            require(wvd.confirm_event(operation, event, generation, ++frame, point), "EXTENSION_EVENT_MISSING");
            require(!wvd.confirm_event(operation, event, generation, ++frame, point), "EXTENSION_EVENT_REPLAYED");
        };
        auto next = [&] { state->enter_segment(contracts::SegmentBoundary::Continuation, ++generation, ++unit); };
        auto route = [&](std::size_t points) {
            emit("dungeon_entered");
            for (std::size_t i = 0; i < points; ++i) emit("target_completed", i);
        };
        auto rest = [&] { emit("inn_payment_prepared"); emit("inn_rest_completed"); };
        auto blocked_pending = [&](const char *field) {
            const auto before = state->summary().at(field);
            state->enter_segment(contracts::SegmentBoundary::LifecycleRecovery, ++generation, unit);
            require(state->summary().at(field) == before, "EXTENSION_RECOVERY_CLEARED_PENDING");
            contracts::SessionResult result;
            result.end = contracts::SessionEnd::RecoveryRequired;
            result.quiescent = true; result.business = state->summary();
            result.reason = "fixture.pending";
            const auto decision = registry->recover(games::recovery::recovery_binding(
                {"m4-handoff-offline", "handoff-instance", "fixture.app", "", false}, values), result, {});
            require(!decision, "EXTENSION_PENDING_RESTARTED");
            emit("game_restarted");
        };
        for (int cycle = 0; cycle < 2; ++cycle) {
            clock->milliseconds = cycle * 12500;
            if (std::string(kind) == "fordraig") {
                emit("fordraig_started"); emit("fordraig_leap_prepared");
                if (cycle == 0) blocked_pending("fordraig");
                emit("fordraig_leaped"); next();
                emit("featured_visit_started"); rest(); emit("featured_visit_completed"); emit("fordraig_requested"); next();
                emit("fordraig_entered"); next();
                for (int trap = 1; trap <= 2; ++trap) {
                    route(2);
                    const auto prefix = "fordraig_trap" + std::to_string(trap);
                    emit(prefix + "_routed");
                    require(!state->summary().at("fordraig").at("continuation_ready").get<bool>(), "TRAP_PUSH_SKIPPED");
                    emit(prefix + "_prepared"); emit(prefix + "_completed"); next();
                }
                route(4); emit("fordraig_trap3_completed"); next();
                require(state->summary().at("strategy").at("automatic").get<bool>(), "PREBOSS_AUTO_MISSING");
                route(1); emit("fordraig_preboss_completed"); next();
                require(!state->summary().at("strategy").at("automatic").get<bool>(), "BOSS_STRATEGY_OVERRIDDEN");
                require(state->summary().at("strategy").at("current") == original_strategy, "BOSS_STRATEGY_ROWS_CHANGED");
                route(1); emit("fordraig_boss_completed"); next();
                require(state->summary().at("strategy").at("automatic").get<bool>(), "EXIT_AUTO_MISSING");
                route(1); emit("fordraig_exited"); next(); emit("fordraig_completed");
            } else {
                emit("cos_started"); emit("cos_leap_prepared");
                if (cycle == 0) blocked_pending("cave_of_separation");
                emit("cos_leaped"); emit("cos_fortress"); emit("cos_royal"); emit("cos_request_observed");
                if (cycle == 1) emit("cos_request_prepared");
                emit("cos_requested"); rest(); emit("cos_rested"); emit("cos_entered"); next();
                route(4); emit("cos_b1_completed"); next();
                emit("cos_ena_confirmed"); next(); // 停点不借上一段task_step要求本段全部坐标。
                emit("cos_request_confirmed"); next();
                route(7); emit("cos_back_completed"); next();
                emit("cos_guild_prepared"); emit("cos_guild_entered"); emit("cos_completed");
            }
            if (cycle == 0) next();
        }
        const auto summary = state->summary();
        const char *field = std::string(kind) == "fordraig" ? "fordraig" : "cave_of_separation";
        require(summary.at(field).at("completed_cycles") == 2 && summary.at("dungeons") == 2,
            "EXTENSION_CYCLE_COUNT_INVALID");
        require(summary.at("strategy").at("current") == original_strategy, "STRATEGY_ROWS_CHANGED");
        output[kind] = summary;
    }
    output["native_run_executed"] = false; // 这是状态/纯恢复策略验证，不冒充Maa完整任务证据。
    return output;
}
}
int main(int argc, char **argv) {
    try {
        require(argc == 2, "CONFIG_REQUIRED");
        const auto config = read(maafw::path_from_utf8(argv[1]));
        const auto mode = config.at("case").get<std::string>();
        nlohmann::ordered_json catalog_source;
        std::ifstream(maafw::path_from_utf8(config.at("quest_catalog"))) >> catalog_source;
        games::WvdQuestCatalog catalog(catalog_source);
        const auto workflow = source_workflow();
        if (mode == "describe") {
            auto images = workflow.images;
            const auto gold = games::recovery::with_boot_recovery(games::tasks::gold_income_cycle(catalog.at("7000G"), false), false);
            images.insert(images.end(), gold.images.begin(), gold.images.end());
            images.push_back("cursedWheel_timeLeap");
            std::ofstream(maafw::path_from_utf8(config.at("output"))) << J{{"images", images}}.dump(2);
            return 0;
        }
        storage::LegacyConfigImporter importer(read(maafw::path_from_utf8(config.at("descriptor"))));
        auto profile = importer.parse(config.at("profile_source"));
        auto registry = std::make_shared<runtime::BehaviorRegistry>("m4-handoff-tests-1");
        games::vision::register_wvd(*registry);
        games::register_wvd_state(*registry);
        games::register_wvd_confirmations(*registry);
        games::combat::register_combat(*registry);
        games::chest::register_chest(*registry);
        games::recovery::register_recovery(*registry);
        registry->seal();
        J output;
        if (mode == "state")
            output = state_boundaries(profile, catalog, registry);
        else if (mode == "extensions")
            output = extension_state_boundaries(profile, registry);
        else {
            maafw::Bundle assets{maafw::path_from_utf8(config.at("bundle")), "handoff-assets", {}};
            for (const auto &file : config.at("files")) assets.files.push_back({file.at("path"), file.at("sha256")});
            const auto folder = maafw::path_from_utf8(config.at("run_root")).parent_path();
            auto device = std::make_shared<Device>();
            if (config.value("authorize_vpn", false)) {
                device->lifecycle.target.vpn_application_id = "fixture.vpn";
                device->lifecycle.target.vpn_required = true;
            }
            device->identity = "m4-handoff-offline";
            device->before = bytes(maafw::path_from_utf8(config.at("before")));
            device->after = bytes(maafw::path_from_utf8(config.at("after")));
            device->wait_case = mode == "wait" || mode == "stop-wait" || mode == "wait-budget";
            device->block_connect = mode == "late-connect";
            auto clock = std::make_shared<Clock>();
            runtime::RunCoordinator coordinator(maafw::path_from_utf8(config.at("run_root")), registry, 4096, clock);
            Unblock unblock{device};
            runtime::RunDefinition definition;
            definition.request_id = "handoff-source";
            definition.initial = fixture_session(workflow, assets, folder / "compiled", *registry);
            definition.policy = {device->identity, "wvd", device->application, definition.initial.bundle.revision,
                device->viewport, {900, 1600},
                {contracts::ActionKind::Click, contracts::ActionKind::ClickKey, contracts::ActionKind::Swipe},
                {contracts::ActionKind::Click, contracts::ActionKind::ClickKey, contracts::ActionKind::Swipe}, {"wvd"}, 2000ms};
            definition.state_factory = games::wvd_state_binding(profile.values);
            definition.recover = games::recovery::recovery_binding(device->lifecycle.target, profile.values);
            definition.recovery_limit = 3;
            games::recovery::bind_leap_wait(definition);
            games::recovery::bind_initial_vpn(definition, device->lifecycle.target, profile.values);
            device->lifecycle.target.vpn_required = definition.recover->parameters.at("vpn_required").get<bool>();
            if (mode == "wait-budget") definition.recovery_limit = 2; // 明确注入用尽，不能继续重启。
            games::tasks::TaskHandoff handoff(coordinator, registry, assets,
                {{"returntoTown.png", "returntotown.png"}});
            const auto initial = handoff.start_source(definition, profile, catalog.at("fortress-B8F_trap"), catalog, device);
            const auto source_directory = coordinator.run_directory();
            device->source_result = source_directory / "result.json";
            require(!handoff.poll(folder / "next-compiled"), "EARLY_HANDOFF_ACCEPTED");
            if (mode == "storage-fail") std::filesystem::create_directory(device->source_result);
            if (mode == "late-connect") {
                until([&] { return device->connections > 0; });
                handoff.request_stop();
                until([&] { return coordinator.snapshot().reason == "STOP_TIMEOUT"; });
                output["held"] = storage::snapshot_json(coordinator.snapshot());
                require(!handoff.poll(folder / "next-compiled"), "HANDOFF_WHILE_CONNECT_HELD");
                device->unblock = true;
            } else if (device->wait_case) {
                for (std::uint64_t slice = 1; slice <= (mode == "wait-budget" ? 2u : 5u); ++slice) {
                    until([&] {
                        const auto events = coordinator.events();
                        for (const auto &event : events.at("events"))
                            if (event.at("type") == "business.leap.wait_started" &&
                                event.at("payload").at("slices") == slice) return true;
                        return coordinator.wait_for(0ms);
                    }, 45000ms);
                    require(!coordinator.wait_for(0ms), "WAIT_EXITED_BEFORE_BOUNDARY");
                    require(!device->restarted, "APPLICATION_RESTARTED_EARLY");
                    if (mode == "stop-wait") { handoff.request_stop(); break; }
                    // 注入的仅是业务单调时钟；SDK、识别、生命周期端口和静止仍真实执行。
                    clock->milliseconds = static_cast<std::int64_t>(slice) * 1460000;
                }
            }
            require(coordinator.wait_for(45000ms), "SOURCE_WATCHDOG_FAILED");
            output["source"] = storage::snapshot_json(coordinator.snapshot());
            output["source_directory"] = maafw::utf8(source_directory);
            output["source_result_exists"] = std::filesystem::is_regular_file(device->source_result);
            if (output.at("source_result_exists").get<bool>()) output["source_saved"] = read(device->source_result);
            if (mode == "money") {
                if (config.value("vpn_lost_before_handoff", false)) {
                    std::lock_guard lock(device->mutex);
                    device->lifecycle.vpn_ready = false;
                }
                const auto first = handoff.poll(folder / "next-compiled");
                require(first && first->run_id == initial.run_id + 1, "NEXT_RUN_NOT_STARTED");
                const auto replay = handoff.poll(folder / "unused-on-replay");
                require(replay && replay->run_id == first->run_id, "HANDOFF_REPLAY_STARTED_ANOTHER_RUN");
                until([&] { return device->calls > 0 || coordinator.wait_for(0ms); }, 45000ms);
                require(device->calls > 0, "NEXT_GOLD_PIPELINE_NOT_EXECUTED");
                handoff.request_stop();
                require(coordinator.wait_for(15000ms), "NEXT_STOP_WATCHDOG_FAILED");
                output["next"] = storage::snapshot_json(coordinator.snapshot());
                output["next_record"] = read(coordinator.run_directory() / "run.json");
                output["next_saved"] = read(coordinator.run_directory() / "result.json");
                output["replay_run_id"] = replay->run_id;
                output["replay_created_directory"] = std::filesystem::exists(folder / "unused-on-replay");
            } else if (mode == "storage-fail") {
                std::string error;
                try { (void)handoff.poll(folder / "next-compiled"); }
                catch (const std::exception &e) { error = e.what(); }
                require(error == "HANDOFF_RESULT_NOT_COMMITTED", "STORAGE_FAILURE_DISPATCHED");
                output["dispatch_error"] = error;
            } else {
                require(!handoff.poll(folder / "next-compiled"), "UNEXPECTED_NEXT_RUN");
            }
            output["inputs"] = device->calls.load();
            output["connections"] = device->connections.load();
            output["lifecycle"] = device->lifecycle_calls;
            output["next_directory_exists"] = std::filesystem::exists(folder / "next-compiled");
            output["native_run_executed"] = true;
        }
        output["loaded_modules"] = loaded_vision_modules();
        std::ofstream(maafw::path_from_utf8(config.at("output"))) << output.dump(2);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
