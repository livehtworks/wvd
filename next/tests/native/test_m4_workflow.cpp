#include "runtime_fixture.hpp"
#include "loaded_modules.hpp"
#include "games/wvd/navigation/world_map.hpp"
#include "games/wvd/navigation/map_route.hpp"
#include "games/wvd/navigation/auto_route.hpp"
#include "games/wvd/navigation/time_leap.hpp"
#include "games/wvd/navigation/causality.hpp"
#include "games/wvd/navigation/dungeon_entry.hpp"
#include "games/wvd/navigation/world_travel.hpp"
#include "games/wvd/supply/party.hpp"
#include "games/wvd/chest/chest.hpp"
#include "games/wvd/diagnostics.hpp"
#include "games/wvd/state.hpp"
#include "storage/legacy_import.hpp"
#include "storage/profile_store.hpp"
#include "games/wvd/supply/inn.hpp"
#include "games/wvd/supply/dungeon_recover.hpp"
#include "games/wvd/combat/auto_combat.hpp"
#include "games/wvd/combat/turn.hpp"
#include "games/wvd/combat/encounter.hpp"
#include "games/wvd/recovery/boot.hpp"
#include "games/wvd/recovery/revival.hpp"
#include "games/wvd/tasks/workflow_session.hpp"
#include "games/wvd/tasks/public_flow_library.hpp"
#include "games/wvd/tasks/dungeon_route.hpp"
#include "games/wvd/tasks/departure.hpp"
#include "games/wvd/tasks/dungeon_iteration.hpp"
#include "games/wvd/tasks/fortress_trap.hpp"
#include "games/wvd/tasks/giant.hpp"
#include "games/wvd/tasks/dark_light.hpp"
#include "games/wvd/tasks/mining.hpp"
#include "games/wvd/tasks/manual_separation.hpp"
#include "games/wvd/tasks/bounty_visit.hpp"
#include "games/wvd/tasks/sleep_visits.hpp"
#include "games/wvd/tasks/bounty_cycle.hpp"
#include "games/wvd/tasks/fishing.hpp"
#include "games/wvd/tasks/fishing_supply.hpp"
#include "games/wvd/tasks/featured_request.hpp"
#include "games/wvd/tasks/golden_chest.hpp"
#include "games/wvd/tasks/sandman.hpp"
#include "games/wvd/tasks/gold_income.hpp"
#include "games/wvd/tasks/bull_cave.hpp"
#include "games/wvd/tasks/steel_trial.hpp"
#include "games/wvd/tasks/repel_forces.hpp"
#include "games/wvd/tasks/fordraig.hpp"
#include "games/wvd/tasks/cave_of_separation.hpp"
#include "games/wvd/tasks/task_handoff.hpp"
#include "games/wvd/recovery/leap_wait.hpp"
#include "games/wvd/quests/repel_forces.hpp"
#include "games/wvd/vision/recognizers.hpp"
#include "platform/windows/file_digest.hpp"
#include <iostream>

using namespace fixture;
// 场景是因果图，不是随 capture 次数前进的录像。额外输入、错误坐标和错误按键都会失败。
class WorkflowDevice final : public OfflineDevice, public devices::LifecyclePort {
  public:
    std::vector<std::vector<std::uint8_t>> frames;
    J transitions;
    std::function<void()> after_input;
    std::size_t cursor{};
    std::size_t action_cursor{};
    bool mismatch{};
    J mismatch_detail;
    J time_events = J::array();
    J capture_events = J::array();
    std::size_t capture_event_count{};
    std::optional<std::chrono::steady_clock::time_point> time_event_due;
    unsigned time_event_count{};
    bool allow_lifecycle{}, stale_lifecycle{}, wrong_instance{}, hold_lifecycle{}, ignore_lifecycle_cancel{};
    std::atomic<bool> release_lifecycle{};
    int failed_starts{}, start_attempts{}, failed_vpns{};
    std::size_t restart_frame{1}, restart_action{};
    std::chrono::milliseconds returned_frame_age{};
    std::chrono::milliseconds capture_delay{};
    bool change_connection_after_first_input{};
    std::uint64_t synthetic_connection_generation{};
    std::atomic<unsigned> lifecycle_count{};
    J lifecycle_calls = J::array();
    bool enforce_connection_state{}, connection_throws{};
    bool verified_real{};
    bool verified_access() const override { return verified_real; }
    devices::LifecycleObservation lifecycle_state{
        {"m2-offline", "fixture-instance", "fixture.app", "fixture.vpn", true}, true, true, true, false, 1, {}, true};
    devices::LifecyclePort *lifecycle_port() override { return allow_lifecycle ? this : nullptr; }
    bool connect() override {
        if (connection_throws)
            throw std::runtime_error("FIXTURE_CONNECTION_EXCEPTION");
        if (enforce_connection_state) {
            std::lock_guard lock(mutex);
            ++connections;
            return lifecycle_state.instance_running && lifecycle_state.connected;
        }
        return OfflineDevice::connect();
    }
    std::optional<devices::LifecycleObservation> observe_lifecycle() override {
        std::lock_guard lock(mutex);
        auto result = lifecycle_state;
        result.observed_at = std::chrono::steady_clock::now() - (stale_lifecycle ? 3s : 0s);
        if (wrong_instance)
            result.target.instance_id = "different-instance";
        return result;
    }
    bool execute_lifecycle(devices::LifecycleOperation operation, const devices::LifecycleTarget &target,
                           const std::function<bool()> &cancelled) override {
        using O = devices::LifecycleOperation;
        static const std::map<O, std::string> names{{O::StopApplication, "StopApplication"},
            {O::StartApplication, "StartApplication"}, {O::Reconnect, "Reconnect"},
            {O::RestartInstance, "RestartInstance"}, {O::EnsureVpn, "EnsureVpn"}};
        {
            std::lock_guard lock(mutex);
            require(target.application_id == "fixture.app" && target.instance_id == "fixture-instance", "FIXTURE_LIFECYCLE_WRONG_TARGET");
            lifecycle_calls.push_back(names.at(operation));
            ++lifecycle_count;
        }
        if (hold_lifecycle) {
            while (!release_lifecycle && (ignore_lifecycle_cancel || !cancelled()))
                std::this_thread::sleep_for(5ms);
            return false;
        }
        std::lock_guard lock(mutex);
        if (cancelled())
            return false;
        if (operation == O::EnsureVpn) {
            if (failed_vpns > 0) {
                --failed_vpns;
                return false;
            }
            lifecycle_state.vpn_ready = true;
        }
        else if (operation == O::StopApplication) {
            lifecycle_state.application_running = false;
            lifecycle_state.application_foreground = false;
        }
        else if (operation == O::StartApplication) {
            if (++start_attempts <= failed_starts)
                return false;
            lifecycle_state.application_running = true;
            lifecycle_state.application_foreground = true;
            cursor = restart_frame; // 只有确认的启动操作切换场景；capture 不推进状态。
            action_cursor = restart_action;
        } else {
            ++lifecycle_state.connection_generation;
            lifecycle_state.instance_running = true;
            lifecycle_state.connected = true;
            if (operation == O::RestartInstance) {
                lifecycle_state.application_running = false;
                lifecycle_state.application_foreground = false;
                lifecycle_state.vpn_ready = false;
            }
        }
        return true;
    }
    devices::RawFrame capture() override {
        if (capture_delay.count()) std::this_thread::sleep_for(capture_delay);
        std::lock_guard lock(mutex);
        // 只按显式时钟事件推进动画；重复截图本身不能产生业务进展。
        if (time_event_due && std::chrono::steady_clock::now() >= *time_event_due) {
            cursor = time_events.at(time_event_count).at("frame").get<std::size_t>();
            time_event_due.reset();
            ++time_event_count;
        }
        const auto capture_number = ++captures;
        if (capture_event_count < capture_events.size() &&
            capture_number == capture_events.at(capture_event_count).at("after_capture").get<std::size_t>()) {
            cursor = capture_events.at(capture_event_count).at("frame").get<std::size_t>();
            ++capture_event_count;
        }
        return {frames.at(cursor), size, identity, viewport, application,
                std::chrono::steady_clock::now() - returned_frame_age, "fixture",
                allow_lifecycle ? lifecycle_state.connection_generation : synthetic_connection_generation};
    }
    bool execute(const contracts::Command &c) override {
        std::lock_guard lock(mutex);
        ++calls;
        sent.push_back(c);
        const J actual{{"kind", int(c.kind)}, {"x", c.x}, {"y", c.y}, {"x2", c.x2}, {"y2", c.y2}, {"key", c.key}, {"duration", c.duration}};
        if (action_cursor >= transitions.size()) {
            mismatch = true;
            mismatch_detail = {{"action_index", action_cursor}, {"expected", nullptr}, {"actual", actual}};
            return false;
        }
        const auto &expected = transitions.at(action_cursor);
        if (int(c.kind) != expected.at("kind").get<int>() || c.x != expected.value("x", 0) ||
            c.y != expected.value("y", 0) || c.key != expected.value("key", 0) ||
            c.x2 != expected.value("x2", 0) || c.y2 != expected.value("y2", 0) ||
            c.duration != expected.value("duration", 0)) {
            mismatch = true;
            mismatch_detail = {{"action_index", action_cursor}, {"expected", expected}, {"actual", actual}};
            return false;
        }
        if (expected.value("reject", false))
            return false;
        if (after_input)
            after_input();
        if (change_connection_after_first_input && action_cursor == 0)
            ++synthetic_connection_generation;
        if (!expected.value("stay", false)) {
            ++cursor;
            ++action_cursor;
        }
        if (time_event_count < time_events.size() && !time_event_due &&
            action_cursor == time_events.at(time_event_count).at("after_input").get<std::size_t>())
            time_event_due = std::chrono::steady_clock::now() +
                             std::chrono::milliseconds(time_events.at(time_event_count).at("delay_ms").get<int>());
        return true;
    }
};
J initial_vpn_contract(const runtime::RunDefinition &source, runtime::RunCoordinator &coordinator,
                       const std::shared_ptr<WorkflowDevice> &device,
                       const std::shared_ptr<runtime::BehaviorRegistry> &registry) {
    using O = devices::LifecycleOperation;
    using B = contracts::SegmentBoundary;
    require(source.initial.lifecycle.has_value(), "INITIAL_VPN_PLAN_MISSING");
    J output;
    const auto rejected = [](auto action) {
        try { action(); } catch (const std::exception &error) { return std::string(error.what()); }
        return std::string{};
    };
    for (const auto *name : {"stop", "start", "reconnect", "restart", "mixed", "attempt", "continuation",
                             "real", "verified-real", "device", "application", "read-only", "registry-timeout"}) {
        auto run = source;
        auto &plan = *run.initial.lifecycle;
        if (std::string(name) == "stop") plan.operations = {O::StopApplication};
        if (std::string(name) == "start") plan.operations = {O::StartApplication};
        if (std::string(name) == "reconnect") plan.operations = {O::Reconnect};
        if (std::string(name) == "restart") plan.operations = {O::RestartInstance};
        if (std::string(name) == "mixed") plan.operations.push_back(O::StartApplication);
        if (std::string(name) == "attempt") plan.attempt = 2;
        if (std::string(name) == "registry-timeout") plan.step_timeout = 0ms;
        if (std::string(name) == "device") plan.target.device_id = "other-device";
        if (std::string(name) == "application") plan.target.application_id = "other-app";
        if (std::string(name) == "read-only") {
            run.policy.observed_read_only_viewport = true;
            run.policy.permissions.clear();
            run.policy.capabilities.clear();
            run.policy.allowed_scenes.clear();
        }
        if (std::string(name) == "continuation") {
            run.max_business_units = 2;
            run.continuation_units = {run.initial};
        }
        device->verified_real = std::string(name) == "verified-real";
        device->real = std::string(name) == "real" || device->verified_real;
        output["coordinator"][name] = rejected([&] { coordinator.start(run, device); });
        require(!output["coordinator"][name].get<std::string>().empty(), "INVALID_INITIAL_RUN_ACCEPTED");
        storage::EventJournal events("initial-vpn-contract", 1, 256);
        output["session"][name] = rejected([&] {
            runtime::ExecutionSession session(run.initial, *device, run.policy, 1, 1, events, registry,
                nullptr, std::string(name) == "continuation" ? B::Continuation : B::Initial);
        });
        require(!output["session"][name].get<std::string>().empty(), "INVALID_INITIAL_SESSION_ACCEPTED");
        device->real = false;
        device->verified_real = false;
    }
    for (const auto *name : {"profile", "unauthorized", "vpn-id", "recovery", "no-recovery", "budget",
                             "continuation", "already-bound", "device", "application"}) {
        auto run = source;
        auto profile = run.state_factory->parameters.at("profile");
        auto target = source.initial.lifecycle->target;
        if (std::string(name) != "already-bound") run.initial.lifecycle.reset();
        if (std::string(name) == "profile") profile["FARM_TARGET_TEXT"] = "different-profile";
        if (std::string(name) == "unauthorized") target.vpn_required = false;
        if (std::string(name) == "vpn-id") target.vpn_application_id.clear();
        if (std::string(name) == "recovery") run.recover->parameters["max_crashes"] = 1234;
        if (std::string(name) == "no-recovery") run.recover.reset();
        if (std::string(name) == "budget") run.recovery_limit = 0;
        if (std::string(name) == "continuation") run.continuation_units = {source.initial};
        if (std::string(name) == "device") run.policy.device_id = "other-device";
        if (std::string(name) == "application") run.policy.application_id = "other-app";
        output["helper"][name] = rejected([&] { games::recovery::bind_initial_vpn(run, target, profile); });
        require(!output["helper"][name].get<std::string>().empty(), "INVALID_INITIAL_BINDING_ACCEPTED");
    }
    storage::EventJournal events("initial-vpn-contract-positive", 1, 256);
    output["valid_initial"] = rejected([&] {
        runtime::ExecutionSession session(source.initial, *device, source.policy, 1, 1, events, registry, nullptr, B::Initial);
    });
    output["plain_recovery"] = rejected([&] {
        runtime::ExecutionSession session(source.initial, *device, source.policy, 1, 1, events, registry, nullptr, B::Recovery);
    });
    output["connections"] = device->connections.load();
    output["backend_calls"] = device->calls.load();
    output["lifecycle_calls"] = device->lifecycle_calls;
    output["native_run_executed"] = false;
    output["loaded_modules"] = loaded_vision_modules();
    return output;
}
int main(int argc, char **argv) {
    try {
        require(argc == 2, "CONFIG_REQUIRED");
        J config;
        std::ifstream(maafw::path_from_utf8(argv[1])) >> config;
        J profile;
        if (config.value("with_state", false)) {
            J descriptor;
            std::ifstream(maafw::path_from_utf8(config.at("descriptor"))) >> descriptor;
            storage::LegacyConfigImporter importer(descriptor);
            profile = importer.parse({{"GENERAL", J::object()}}).values;
            profile.update(config.value("profile", J::object()));
        }
        J task_plan;
        std::vector<games::tasks::CompiledWorkflow> stage_workflows;
        auto workflow = [&] {
            const auto kind = config.at("workflow").get<std::string>();
            if (kind == "author-event") {
                games::tasks::PublicFlowLibrary library(config.at("author_library"));
                require(library.task_profiles(config.at("author_document"), J::object(), "en").empty(),
                        "FIXTURE_AUTHOR_PROFILE_UNEXPECTED");
                return library.compile(config.at("author_document"), {}, J::object(), "en").workflow;
            }
            if (kind == "staged-publication") {
                using C = games::tasks::PipelineCompiler;
                const std::array<std::string, 3> images{"Inn", "Stay", "Economy"};
                for (unsigned i = 0; i < 2; ++i) {
                    C graph("fixture.stage" + std::to_string(i));
                    graph.route("Entry", {"Click"});
                    graph.click("Click", C::image(images[i]), C::image(images[i]), C::image(images[i + 1]), {"Confirm"});
                    graph.confirm("Confirm", "observed", "dungeon_entered", C::image(images[i + 1]), {"Terminal"});
                    stage_workflows.push_back(graph.finish());
                }
                return stage_workflows.front();
            }
            if (kind == "fordraig" || kind == "cave-of-separation") {
                nlohmann::ordered_json source;
                std::ifstream(maafw::path_from_utf8(config.at("quest_catalog"))) >> source;
                games::WvdQuestCatalog catalog(source);
                const auto &task = catalog.at(kind == "fordraig" ? "fordraig" : "CaveOfSeperation");
                std::set<std::string> images;
                for (const auto &file : config.at("files")) {
                    const auto path = file.at("path").get<std::string>();
                    if (path.starts_with("image/")) images.insert(path.substr(6));
                }
                if (kind == "fordraig") {
                    task_plan = games::tasks::fordraig_plan(task).inspect();
                    stage_workflows = games::tasks::fordraig_cycle(task, profile, images, config.value("allow_download", true));
                } else {
                    task_plan = J::array();
                    for (unsigned i = 0; i < games::quests::CaveOfSeparation::segments_per_cycle; ++i) {
                        const auto segment = static_cast<games::tasks::CaveOfSeparationSegment>(i);
                        task_plan.push_back(games::tasks::cave_of_separation_plan(task, segment).inspect());
                        stage_workflows.push_back(games::tasks::cave_of_separation_segment(task, profile, images,
                            segment, config.value("allow_download", true)));
                    }
                }
                return stage_workflows.front();
            }
            if (kind == "city")
                return games::navigation::enter_city(config.at("city"));
            if (kind == "fishing-cast")
                return games::tasks::cast_fishing_line(config.value("far", false));
            if (kind == "fishing-reward")
                return games::tasks::collect_fishing_reward();
            if (kind == "fishing-round")
                return games::tasks::fishing_round(config.value("far", false), config.value("allow_download", true));
            if (kind == "fishing-seek")
                return games::tasks::seek_fishing_position();
            if (kind == "inn")
                return games::supply::rest_at_inn(config.value("royal", false));
            if (kind == "causality")
                return games::navigation::adjust_causality({"LBC/symbolofalliance", {{"LBC/EnaWasSaved", {2, 1, 0}}}});
            if (kind == "time-leap" && config.value("causality", false))
                return games::navigation::time_leap_with_causality(config.at("leap_target"),
                    {"LBC/symbolofalliance", {{"LBC/EnaWasSaved", {2, 1, 0}}}},
                    config.value("leap_chapter", "cursedwheel_impregnableFortress"), config.value("allow_download", true));
            if (kind == "time-leap")
                return games::navigation::time_leap_without_causality(config.at("leap_target"),
                    config.value("leap_chapter", "cursedwheel_impregnableFortress"), config.value("allow_download", true));
            if (kind == "bounty-visit")
                return games::tasks::visit_bounty_board(config.value("report", false) ? games::tasks::BountyVisit::Report : games::tasks::BountyVisit::Reveal);
            if (kind == "featured-request")
                return games::tasks::accept_featured_request(config.value("bull", true) ? games::tasks::FeaturedRequest::BullCave : games::tasks::FeaturedRequest::GoldenChest,
                    config.value("royal", false));
            if (kind == "sleep-batch") {
                nlohmann::ordered_json source;
                std::ifstream(maafw::path_from_utf8(config.at("quest_catalog"))) >> source;
                return games::tasks::sleep_visits(games::WvdQuestCatalog(source).at("lovesleep"), profile);
            }
            if (kind == "inn-tracked") {
                using C = games::tasks::PipelineCompiler;
                C graph("fixture.tracked_inn");
                const auto child = graph.define_child("Inn", games::supply::rest_at_inn(config.value("royal", false), true));
                graph.route("Entry", {"First"});
                graph.call_child("First", child, {"Second"});
                graph.call_child("Second", child, {"Terminal"});
                return graph.finish();
            }
            if (kind == "departure") {
                games::WvdQuestDefinition definition{"departure-fixture", "dungeon",
                    {{"_EOT", {{"press", "Dist", {1, 1}, 1}}}, {"_TARGETINFOLIST", {{"chest"}}}}};
                if (config.contains("return_destination"))
                    definition.source["_RTT"] = config.at("return_destination");
                return games::tasks::prepare_departure(games::WvdTaskPlan::parse(definition), profile,
                                                       config.value("force_rest", false));
            }
            if (kind == "heal")
                return games::supply::recover_in_dungeon();
            if (kind == "revival")
                return games::recovery::revive_after_defeat();
            if (kind == "common")
                return games::recovery::clear_common_screens(config.value("allow_download", true),
                    config.value("golden_dialogue", false) ? games::recovery::DialoguePolicy::GoldenChest : config.value("jier_dialogue", false) ? games::recovery::DialoguePolicy::Jier : games::recovery::DialoguePolicy::Default);
            if (kind == "repel-forces" || kind == "steel-trial" || kind == "fortress-trap" || kind == "giant" || kind == "dark-light" || kind == "mining" || kind == "manual-separation" || kind == "scorpion" || kind == "fishing-cycle" || kind == "jier" || kind == "golden-chest" || kind == "sandman" || kind == "gold-income" || kind == "bull-cave") {
                nlohmann::ordered_json source;
                std::ifstream(maafw::path_from_utf8(config.at("quest_catalog"))) >> source;
                games::WvdQuestCatalog catalog(source);
                const auto &task = catalog.at(kind == "repel-forces" ? "repelEnemyForces" : kind == "steel-trial" ? "steeltrail" : kind == "bull-cave" ? "LBC-oneGorgon" : kind == "gold-income" ? "7000G" : kind == "sandman" ? "sandman" : kind == "golden-chest" ? "SSC-goldenchest" : kind == "jier" ? "jier" : kind == "fishing-cycle" ? (config.value("far", false) ? "fishing2" : "fishing") : kind == "scorpion" ? (config.value("hands", false) ? "Scorpionesses_plus_6_hands" : "Scorpionesses") : kind == "manual-separation" ? "manualSepDemon" : kind == "mining" ? "FFXI-Org" : kind == "dark-light" ? "darkLight" : kind == "giant" ? "gaintKiller" : "fortress-B8F_trap");
                std::set<std::string> images;
                for (const auto &file : config.at("files")) {
                    const auto path = file.at("path").get<std::string>();
                    if (path.starts_with("image/"))
                        images.insert(path.substr(6));
                }
                if (kind == "gold-income") return games::tasks::gold_income_cycle(task, config.value("allow_download", true));
                if (kind == "repel-forces") {
                    task_plan = games::tasks::repel_forces_plan(task).inspect();
                    return games::tasks::repel_forces_cycle(task, profile, images, config.value("allow_download", true));
                }
                if (kind == "steel-trial") {
                    task_plan = games::tasks::steel_trial_plan(task).inspect();
                    return games::tasks::steel_trial_cycle(task, profile, images, config.value("allow_download", true));
                }
                if (kind == "bull-cave") {
                    task_plan = games::tasks::bull_cave_plan(task).inspect();
                    return games::tasks::bull_cave_cycle(task, profile, images, config.value("allow_download", true));
                }
                if (kind == "sandman") {
                    task_plan = games::tasks::sandman_plan(task).inspect();
                    return games::tasks::sandman_cycle(task, profile, images, config.value("allow_download", true));
                }
                if (kind == "golden-chest") {
                    task_plan = games::tasks::golden_chest_plan(task).inspect();
                    return games::tasks::golden_chest_cycle(task, profile, images, config.value("allow_download", true));
                }
                if (kind == "fishing-cycle") {
                    task_plan = games::WvdTaskPlan::parse(task).inspect();
                    return games::tasks::fishing_cycle(task, profile, images, config.value("allow_download", true));
                }
                if (kind == "scorpion" || kind == "jier") {
                    task_plan = (kind == "jier" ? games::tasks::jier_plan(task) : games::tasks::scorpion_plan(task)).inspect();
                    return games::tasks::bounty_cycle(task, profile, images, config.value("allow_download", true));
                }
                if (kind == "manual-separation") {
                    task_plan = J::array({games::tasks::manual_separation_plan(task, false).inspect(),
                        games::tasks::manual_separation_plan(task, true).inspect()});
                    return games::tasks::manual_separation(task, profile, images, config.value("allow_download", true));
                }
                if (kind == "mining") {
                    task_plan = games::WvdTaskPlan::parse(task).inspect();
                    return games::tasks::mining_iteration(task, profile, config.value("allow_download", true));
                }
                if (kind == "dark-light") {
                    task_plan = games::WvdTaskPlan::parse(task).inspect();
                    return games::tasks::dark_light(task, profile, images, config.value("allow_download", true));
                }
                if (kind == "giant") {
                    task_plan = games::tasks::giant_plan(task).inspect();
                    return games::tasks::giant_iteration(task, profile, images, config.value("allow_download", true));
                }
                task_plan = games::tasks::fortress_trap_plan(task).inspect();
                return games::tasks::fortress_trap_iteration(task, profile, images, config.value("allow_download", true));
            }
            if (kind == "dungeon-route" || kind == "iteration") {
                auto definition = [&] {
                    if (config.contains("catalog_task_id")) {
                        for (const auto *field : {"route_targets", "entry_steps", "floor", "return_destination", "route_type"})
                            require(!config.contains(field), "FIXTURE_CATALOG_OVERRIDE_FORBIDDEN");
                        nlohmann::ordered_json source;
                        std::ifstream(maafw::path_from_utf8(config.at("quest_catalog"))) >> source;
                        const games::WvdQuestCatalog catalog(source);
                        const auto task = catalog.at(config.at("catalog_task_id").get<std::string>());
                        require(task.type == "dungeon", "FIXTURE_CATALOG_DUNGEON_REQUIRED");
                        return task;
                    }
                    games::WvdQuestDefinition task{"route-fixture", config.value("route_type", std::string("dungeon")),
                        {{"_EOT", {{"press", "Dist", {1, 1}, 1}}}, {"_TARGETINFOLIST", config.at("route_targets")}}};
                    if (config.contains("floor")) task.source["_FloorCheck"] = config.at("floor");
                    if (config.contains("entry_steps")) task.source["_EOT"] = config.at("entry_steps");
                    if (config.contains("return_destination")) task.source["_RTT"] = config.at("return_destination");
                    return task;
                }();
                task_plan = games::WvdTaskPlan::parse(definition).inspect();
                std::set<std::string> images;
                for (const auto &file : config.at("files")) {
                    const auto path = file.at("path").get<std::string>();
                    if (path.starts_with("image/"))
                        images.insert(path.substr(6));
                }
                return kind == "iteration" ? games::tasks::dungeon_iteration(games::WvdTaskPlan::parse(definition), profile, images, config.value("allow_download", true))
                                           : games::tasks::traverse_dungeon(games::WvdTaskPlan::parse(definition), profile, images, config.value("allow_download", true));
            }
            if (kind == "child") {
                using C = games::tasks::PipelineCompiler;
                auto inn = games::supply::rest_at_inn(false);
                for (auto &node : inn.nodes)
                    node["max_hit"] = 1;
                C graph("fixture.native_children");
                auto entry = graph.define_child("Inn", inn);
                graph.route("Entry", {"Call0"});
                for (int i = 0; i < 3; ++i)
                    graph.call_child("Call" + std::to_string(i), entry,
                        {i == 2 ? "Terminal" : "Call" + std::to_string(i + 1)});
                auto result = graph.finish();
                if (config.value("nested_child", false)) {
                    C outer("fixture.nested_native_children");
                    const auto nested = outer.define_child("Nested", result);
                    outer.route("Entry", {"Invoke"});
                    outer.call_child("Invoke", nested, {"Terminal"});
                    return outer.finish();
                }
                return result;
            }
            if (kind == "auto")
                return games::combat::enable_auto();
            if (kind == "recover")
                return games::recovery::with_boot_recovery(games::supply::rest_at_inn(false),
                    config.value("allow_download", true));
            if (kind == "auto-route")
                return games::navigation::auto_route(config.value("auto_target", "chest_auto"));
            if (kind == "entry") {
                auto definition = [&] {
                    if (config.contains("catalog_task_id")) {
                        require(!config.contains("entry_steps") && !config.contains("pre_entry"), "FIXTURE_CATALOG_OVERRIDE_FORBIDDEN");
                        nlohmann::ordered_json source;
                        std::ifstream(maafw::path_from_utf8(config.at("quest_catalog"))) >> source;
                        const games::WvdQuestCatalog catalog(source);
                        const auto task = catalog.at(config.at("catalog_task_id").get<std::string>());
                        require(task.type == "dungeon", "FIXTURE_CATALOG_DUNGEON_REQUIRED");
                        return task;
                    }
                    games::WvdQuestDefinition task{"entry-fixture", "dungeon",
                        {{"_EOT", config.at("entry_steps")}, {"_TARGETINFOLIST", {{"chest"}}}}};
                    if (config.contains("pre_entry")) task.source["_preEOTcheck"] = config.at("pre_entry");
                    return task;
                }();
                task_plan = games::WvdTaskPlan::parse(definition).inspect();
                return games::navigation::enter_dungeon(games::WvdTaskPlan::parse(definition));
            }
            if (kind == "turn" || kind == "encounter") {
                std::set<std::string> images;
                for (const auto &file : config.at("files")) {
                    const auto path = file.at("path").get<std::string>();
                    if (path.starts_with("image/"))
                        images.insert(path.substr(6));
                }
                return kind == "turn" ? games::combat::take_turn(profile, images)
                                      : games::combat::fight_encounter(profile, images, config.value("max_turns", 2u),
                                                                      config.value("max_auto_polls", 128u));
            }
            if (kind == "chest")
                return games::chest::open_chest(config.value("preferred", 1), config.value("quick", false), 42);
            if (kind == "city-travel")
                return games::navigation::travel_city_to_city({"City_RoyalCityLuknalia", games::TaskSwipe{{450, 150}, {500, 150}}, {550, 1}});
            if (kind == "travel") {
                const auto coords = config.value("swipe", J{450, 150, 500, 150});
                games::WorldDestination destination{config.at("city"), {}, {550, 1}};
                if (!coords.is_null())
                    destination.swipe = games::TaskSwipe{{coords[0], coords[1]}, {coords[2], coords[3]}};
                return games::navigation::travel_world(destination, config.value("returning", true)
                    ? games::navigation::WorldArrival::City : games::navigation::WorldArrival::DungeonEntrance);
            }
            if (kind == "party" || kind == "party-rest") {
                const auto party = config.contains("party_image") ? std::optional(config.at("party_image").get<std::string>()) : std::nullopt;
                return kind == "party" ? games::supply::assemble_party(party)
                                       : games::supply::assemble_and_rest(config.value("royal", false), party);
            }
            if (kind == "city-inn") {
                games::tasks::PipelineCompiler graph("supply.city_and_rest");
                const auto rest = graph.append("Rest", games::supply::rest_at_inn(false), {"Terminal"});
                const auto city = graph.append("City", games::navigation::enter_city(config.at("city")), {rest});
                graph.route("Entry", {city});
                return graph.finish();
            }
            if (kind == "map" || kind == "map-confirm") {
                games::WvdQuestDefinition definition{"map-fixture", "dungeon",
                    {{"_TARGETINFOLIST", J::array({config.at("map_target")})},
                     {"_EOT", J::array({J::array({"press", "Dist", nullptr, 1})})}}};
                auto plan = games::WvdTaskPlan::parse(definition);
                auto route = games::navigation::reach_map_target(plan.route().at(0),
                    config.contains("floor") ? std::optional(config.at("floor").get<std::string>()) : std::nullopt);
                if (kind == "map")
                    return route;
                using C = games::tasks::PipelineCompiler;
                C graph("navigation.confirmed_point");
                const auto done = C::all({C::image("mapFlag"), J{{"mode", "reached"}, {"position", {500, 600}}}});
                const auto entry = graph.append("Point", route, {"Confirm"});
                graph.route("Entry", {entry});
                graph.confirm("Confirm", "point.0", "target_completed", done,
                               {"Replay"}, config.value("expected_step", 0));
                graph.confirm("Replay", "point.0", "target_completed", done, {"Terminal"}, 0);
                return graph.finish();
            }
            if (kind == "state-route") {
                using C = games::tasks::PipelineCompiler;
                C graph("navigation.state_routing");
                const auto map = C::image("mapFlag");
                const auto dungeon = C::image("dungFlag");
                const J combat{{"mode", "combat_active"}};
                graph.route("Entry", {"Begin"});
                graph.confirm("Begin", "route.begin", "dungeon_entered", map, {"Dispatch"});
                graph.route("Dispatch", {"Done", "Step0", "Step1"});
                graph.observe("Done", C::all({map, C::business("/task_step", 2)}), {"Terminal"});
                for (int i = 0; i < 2; ++i) {
                    games::MapTarget target{"position", {std::nullopt}, games::MapTarget::Hint::Position,
                        games::TaskPoint{i == 0 ? 500 : 700, i == 0 ? 600 : 700}, {}, {}};
                    const auto suffix = std::to_string(i);
                    const auto start = graph.append("Point" + suffix,
                        games::navigation::reach_map_target(target), {"Confirm" + suffix},
                        {{"EncounterExit", {"Encounter"}}});
                    graph.observe("Step" + suffix, C::business("/task_step", i), {start});
                    graph.confirm("Confirm" + suffix, "point." + suffix, "target_completed",
                        C::all({map, J{{"mode", "reached"}, {"position", *target.position}}}), {"Dispatch"}, i);
                }
                graph.confirm("Encounter", "combat.observed", "combat_observed", combat, {"FixtureCombat"});
                // 这是普通插入边的合成业务夹具，不宣称此单动作就是完整 WVD 战斗。
                graph.fixed_click("FixtureCombat", combat, dungeon, {850, 1100}, {"Resume"});
                graph.confirm("Resume", "combat.resumed", "dungeon_resumed", dungeon, {"Dispatch"});
                return graph.finish();
            }
            throw std::runtime_error("WORKFLOW_UNKNOWN");
        }();
        if (config.value("attach_recovery", false)) {
            if (stage_workflows.empty())
                workflow = games::recovery::with_boot_recovery(workflow, config.value("allow_download", true));
            else {
                for (auto &stage : stage_workflows)
                    stage = games::recovery::with_boot_recovery(stage, config.value("allow_download", true));
                workflow = stage_workflows.front();
            }
        }
        if (config.contains("invalid")) {
            if (config["invalid"] == "raw-input")
                workflow.nodes["Entry"]["action"] = "Click";
            else if (config["invalid"] == "unknown-next")
                workflow.nodes["Entry"]["next"] = {"Missing"};
            else if (config["invalid"] == "unbounded")
                workflow.nodes["Entry"]["max_hit"] = 0;
            else if (config["invalid"] == "zero-session-budget")
                workflow.time_limit = 0ms;
            else if (config["invalid"] == "large-session-budget")
                workflow.time_limit = std::chrono::minutes{31};
            else if (config["invalid"] == "empty")
                workflow.nodes = J::object();
            else if (config["invalid"] == "missing-terminal")
                workflow.nodes.erase(workflow.terminal);
            else if (config["invalid"] == "stale-images")
                workflow.images.clear();
            else if (config["invalid"] == "shell-permission")
                workflow.nodes["Click0"]["custom_action_param"]["command"]["kind"] = "Shell";
            else if (config["invalid"] == "unknown-recognition")
                workflow.nodes["Click0"]["custom_recognition"] = "Missing";
            else if (config["invalid"] == "child-unknown")
                workflow.nodes["Call0"]["custom_action_param"]["entry"] = "Missing";
            else if (config["invalid"] == "child-recursive")
                workflow.nodes["Call0"]["custom_action_param"]["entry"] = "Entry";
            else if (config["invalid"] == "child-override")
                workflow.nodes["Call0"]["custom_action_param"]["overrides"] = J::object();
            else if (config["invalid"] == "child-crossing")
                workflow.nodes["Call0"]["next"] = {"Inn_Entry", "Call1"};
            else if (config["invalid"] == "child-reset-parent")
                workflow.nodes["Call0"]["custom_action_param"]["reset_hit_counts"].push_back("Call0");
            else if (config["invalid"] == "child-shared-depth") {
                workflow.nodes["Call1"]["custom_action_param"]["entry"] = "Deep0_Entry";
                for (int i = 0; i < 7; ++i) {
                    const auto prefix = "Deep" + std::to_string(i);
                    const auto target = i == 6 ? "Inn_Entry" : "Deep" + std::to_string(i + 1) + "_Entry";
                    workflow.nodes[prefix + "_Entry"] = {{"action", "DoNothing"}, {"max_hit", 1}, {"next", {prefix + "_Call"}}};
                    workflow.nodes[prefix + "_Call"] = {{"action", "Custom"}, {"max_hit", 1}, {"custom_action", "RunChild"},
                        {"custom_action_param", {{"entry", target}, {"clone", false}, {"reset_hit_counts", J::array()}}},
                        {"next", {prefix + "_End"}}};
                    workflow.nodes[prefix + "_End"] = {{"action", "DoNothing"}, {"max_hit", 1}};
                }
            }
            else
                throw std::runtime_error("UNKNOWN_INVALID_CASE");
            J output;
            try {
                workflow.validate();
                output["error"] = "";
            } catch (const std::exception &e) {
                output["error"] = e.what();
            }
            output["backend_calls"] = 0;
            output["loaded_modules"] = loaded_vision_modules();
            std::ofstream(maafw::path_from_utf8(config.at("output"))) << output.dump(2);
            return 0;
        }
        const auto root = maafw::path_from_utf8(config.at("bundle"));
        maafw::Bundle bundle{root, "m4-causal-1", {}};
        for (const auto &file : config.at("files"))
            bundle.files.push_back({file.at("path"), file.at("sha256")});
        std::optional<maafw::Bundle> mod;
        if (config.contains("mod_bundle")) {
            const auto &input = config.at("mod_bundle");
            mod.emplace(maafw::Bundle{maafw::path_from_utf8(input.at("root")), "private-mod-fixture", {}});
            for (const auto &file : input.at("files"))
                mod->files.push_back({file.at("path"), file.at("sha256")});
        }
        auto device = std::make_shared<WorkflowDevice>();
        for (const auto &frame : config.at("frames"))
            device->frames.push_back(bytes(maafw::path_from_utf8(frame)));
        device->transitions = config.at("transitions");
        device->capture_events = config.value("capture_events", J::array());
        require(device->capture_events.is_array(), "FIXTURE_CAPTURE_EVENTS_INVALID");
        std::size_t previous_capture = 0;
        for (const auto &event : device->capture_events) {
            const auto after = event.at("after_capture").get<std::size_t>();
            require(after > previous_capture && event.at("frame").get<std::size_t>() < device->frames.size(),
                    "FIXTURE_CAPTURE_EVENT_INVALID");
            previous_capture = after;
        }
        require(!(config.contains("time_event") && config.contains("time_events")), "FIXTURE_TIME_EVENT_AMBIGUOUS");
        if (config.contains("time_event")) device->time_events.push_back(config.at("time_event"));
        if (config.contains("time_events")) device->time_events = config.at("time_events");
        require(device->time_events.is_array(), "FIXTURE_TIME_EVENTS_INVALID");
        std::size_t previous_input = 0;
        for (const auto &event : device->time_events) {
            const auto after = event.at("after_input").get<std::size_t>();
            require(after > previous_input && after <= device->transitions.size() &&
                        event.at("frame").get<std::size_t>() < device->frames.size() &&
                        event.at("delay_ms").get<int>() > 0 &&
                        event.at("delay_ms").get<int>() <= 10000, "FIXTURE_TIME_EVENT_INVALID");
            previous_input = after;
        }
        const bool recovering = config.at("workflow") == "recover" || config.value("attach_recovery", false);
        device->allow_lifecycle = recovering && !config.value("no_lifecycle_port", false);
        device->failed_vpns = config.value("fail_vpns", 0);
        device->stale_lifecycle = config.value("stale_lifecycle", false);
        device->wrong_instance = config.value("other_lifecycle_instance", false);
        device->failed_starts = config.value("fail_starts", 0);
        device->restart_frame = config.value("restart_frame", std::size_t{1});
        const auto frame_age = config.value("returned_frame_age_ms", 0);
        require(frame_age >= 0 && frame_age <= 60000, "FIXTURE_FRAME_AGE_INVALID");
        device->returned_frame_age = std::chrono::milliseconds{frame_age};
        const auto capture_delay = config.value("capture_delay_ms", 0);
        require(capture_delay >= 0 && capture_delay <= 3000, "FIXTURE_CAPTURE_DELAY_INVALID");
        device->capture_delay = std::chrono::milliseconds{capture_delay};
        device->change_connection_after_first_input = config.value("change_connection_after_first_input", false);
        device->restart_action = config.value("restart_action", std::size_t{0});
        device->hold_lifecycle = config.value("stop_during_lifecycle", false) || config.value("late_lifecycle_release", false);
        device->ignore_lifecycle_cancel = config.value("late_lifecycle_release", false);
        const auto initial_connection = config.value("initial_connection", std::string("ready"));
        device->enforce_connection_state = initial_connection != "ready";
        device->connection_throws = initial_connection == "exception";
        if (initial_connection == "offline" || initial_connection == "closed") {
            device->lifecycle_state.connected = false;
            device->lifecycle_state.application_running = false;
            device->lifecycle_state.application_foreground = false;
            device->lifecycle_state.instance_running = initial_connection != "closed";
        }
        auto registry = std::make_shared<runtime::BehaviorRegistry>("m4-workflows-1");
        if (!config.value("missing_binding", false))
            games::vision::register_wvd(*registry);
        games::register_wvd_state(*registry);
        games::register_wvd_confirmations(*registry);
        games::combat::register_combat(*registry);
        games::chest::register_chest(*registry);
        games::recovery::register_recovery(*registry);
        registry->seal();
        runtime::SessionDefinition session;
        std::vector<runtime::SessionDefinition> stage_sessions;
        if (config.value("destination_exists", false))
            std::filesystem::create_directory(root.parent_path() / "compiled");
        try {
            if (!stage_workflows.empty()) {
                stage_sessions = games::tasks::publish_workflow_stages(stage_workflows, bundle, *registry,
                    root.parent_path() / "compiled", config.value("aliases", J::object()), mod ? &*mod : nullptr);
                session = stage_sessions.front();
            } else session = games::tasks::publish_workflow(workflow, bundle, *registry,
                                                     root.parent_path() / "compiled",
                                                     config.value("aliases", J::object()), mod ? &*mod : nullptr);
        } catch (const std::exception &e) {
            J output{{"publish_error", e.what()},
                     {"backend_calls", device->calls.load()},
                     {"connections", device->connections.load()},
                     {"loaded_modules", loaded_vision_modules()}};
            std::ofstream(maafw::path_from_utf8(config.at("output"))) << output.dump(2);
            return 0;
        }
        J image_sources;
        if (mod) {
            std::ifstream(session.bundle.root / "parameters/image-sources.json") >> image_sources;
            if (config.value("mutate_mod_after_publish", false))
                for (const auto &file : mod->files)
                    std::ofstream(mod->root / maafw::path_from_utf8(file.relative_path), std::ios::binary) << "changed fixture";
        }
        contracts::InputPolicy policy{
            "m2-offline",
            "wvd",
            "fixture.app",
            session.bundle.revision,
            "portrait",
            {900, 1600},
            {contracts::ActionKind::Click, contracts::ActionKind::ClickKey, contracts::ActionKind::Swipe},
            {contracts::ActionKind::Click, contracts::ActionKind::ClickKey, contracts::ActionKind::Swipe},
            {"wvd"},
            config.at("workflow") == "author-event" ? 0ms : 2000ms};
        policy.permissions.clear();
        auto actions = workflow.required_actions;
        for (const auto &stage : stage_workflows)
            actions.insert(actions.end(), stage.required_actions.begin(), stage.required_actions.end());
        for (const auto &kind : actions)
            policy.permissions.insert(kind == "Click"      ? contracts::ActionKind::Click
                                      : kind == "ClickKey" ? contracts::ActionKind::ClickKey
                                                           : contracts::ActionKind::Swipe);
        runtime::RunCoordinator coordinator(maafw::path_from_utf8(config.at("run_root")), registry);
        runtime::RunDefinition definition;
        definition.request_id = "m4-causal";
        definition.policy = policy;
        definition.initial = std::move(session);
        if (config.at("workflow") == "author-event")
            definition.total_time_limit = std::chrono::milliseconds{
                config.value("total_time_limit_ms", workflow.time_limit.count())};
        const auto units = !stage_sessions.empty() ? stage_sessions.size() : config.at("workflow") == "repel-forces" ? games::quests::RepelForces::rounds(profile) + 2 : config.at("workflow") == "bull-cave" ? (profile.at("ACTIVE_REST").get<bool>() ? 3u : 2u) : config.at("workflow") == "jier" ? 3u : config.at("workflow") == "scorpion" ? (config.value("hands", false) ? 4u : 3u) :
            config.at("workflow") == "manual-separation" || config.at("workflow") == "golden-chest" || config.at("workflow") == "sandman" ? 2u : config.value("normal_units", 1u);
        require(units > 0 && units <= (!stage_sessions.empty() || config.at("workflow") == "repel-forces" ? 256 : 4), "FIXTURE_NORMAL_UNITS_INVALID");
        if (config.at("workflow") == "fordraig")
            games::tasks::configure_fordraig_units(definition, stage_sessions);
        else if (config.at("workflow") == "cave-of-separation") {
            std::array<runtime::SessionDefinition, games::quests::CaveOfSeparation::segments_per_cycle> stages;
            require(stage_sessions.size() == stages.size(), "FIXTURE_SEGMENTS_INVALID");
            std::copy(stage_sessions.begin(), stage_sessions.end(), stages.begin());
            games::tasks::configure_cave_of_separation_units(definition, stages);
        } else if (config.at("workflow") == "staged-publication") {
            definition.max_business_units = stage_sessions.size();
            definition.continuation_units.assign(stage_sessions.begin() + 1, stage_sessions.end());
        } else if (config.at("workflow") == "manual-separation")
            games::tasks::configure_manual_separation_units(definition);
        else if (config.at("workflow") == "scorpion" || config.at("workflow") == "jier")
            games::tasks::configure_bounty_units(definition, config.value("hands", false));
        else if (config.at("workflow") == "fishing-cycle")
            games::tasks::configure_fishing_units(definition, units);
        else if (config.at("workflow") == "golden-chest")
            games::tasks::configure_golden_chest_units(definition);
        else if (config.at("workflow") == "sandman")
            games::tasks::configure_sandman_units(definition);
        else if (config.at("workflow") == "bull-cave")
            games::tasks::configure_bull_cave_units(definition, profile.at("ACTIVE_REST").get<bool>());
        else if (config.at("workflow") == "repel-forces")
            games::tasks::configure_repel_forces_units(definition, profile);
        else {
            definition.max_business_units = units;
            for (unsigned i = 1; i < units; ++i)
                definition.continuation_units.push_back(definition.initial);
        }
        if (recovering && !config.value("omit_recovery_policy", false)) {
            auto target = device->lifecycle_state.target;
            if (config.value("other_lifecycle_app", false))
                target.application_id = "not-the-game";
            definition.recover = games::recovery::recovery_binding(target, profile, config.value("force_restart", false));
            device->lifecycle_state.target.vpn_required = definition.recover->parameters.at("vpn_required").get<bool>();
            device->lifecycle_state.vpn_ready = config.value("vpn_ready", false);
            definition.recovery_limit = 3;
            if (!stage_sessions.empty()) {
                J entries = J::object();
                for (std::size_t i = 0; i < stage_sessions.size(); ++i) {
                    require(stage_workflows[i].nodes.contains("Boot_Entry"), "FIXTURE_STAGE_BOOT_NOT_WRAPPED");
                    entries[stage_sessions[i].checkpoint_node] = "Stage" + std::to_string(i) + "_Boot_Entry";
                }
                definition.recover->parameters["boot_entries"] = std::move(entries);
            }
        }
        if (config.value("with_state", false))
            definition.state_factory = games::wvd_state_binding(profile);
        if (definition.state_factory) {
            auto target = device->lifecycle_state.target;
            if (config.value("other_lifecycle_app", false))
                target.application_id = "not-the-game";
            games::recovery::bind_initial_vpn(definition, target, profile);
        }
        if (config.value("mutate_profile_after_freeze", false))
            profile.update({{"AUTO_START_CLASH", false}, {"MAX_CRASH_LIMIT", 99}, {"FARM_TARGET_TEXT", "changed-after-freeze"}});
        const auto has_unknown_leap = [](const runtime::SessionDefinition &unit) {
            return std::any_of(unit.actions.begin(), unit.actions.end(),
                [](const auto &binding) { return binding.name == "WvdUnknownLeap"; });
        };
        if (definition.recover && definition.state_factory && (has_unknown_leap(definition.initial) ||
            std::any_of(definition.continuation_units.begin(), definition.continuation_units.end(), has_unknown_leap)))
            games::recovery::bind_leap_wait(definition);
        std::unique_ptr<storage::ProfileStore> karma_store;
        J original_profile;
        struct HeldFile {
            HANDLE value{INVALID_HANDLE_VALUE};
            ~HeldFile() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
        } profile_handle;
        if (config.value("karma_profile", false)) {
            J descriptor;
            std::ifstream(maafw::path_from_utf8(config.at("descriptor"))) >> descriptor;
            storage::LegacyConfigImporter importer(descriptor);
            const auto path = maafw::path_from_utf8(config.at("output")).parent_path() / "private-profile.json";
            karma_store = std::make_unique<storage::ProfileStore>(path, descriptor);
            original_profile = karma_store->create(importer.parse({{"GENERAL", profile}}));
            const auto path_utf8 = path.u8string();
            definition.state_factory->parameters["profile_store"] = {
                {"path", std::string(path_utf8.begin(), path_utf8.end())}, {"descriptor", descriptor}, {"revision", original_profile.at("revision")}};
            const auto fault = config.value("karma_save_fault", "");
            if (!fault.empty())
                device->after_input = [&, path, fault] {
                    if (device->calls != 1)
                        return;
                    if (fault == "conflict") {
                        auto external = original_profile;
                        external["values"]["KARMA_ADJUST"] = "+9";
                        karma_store->compare_exchange(original_profile.at("revision"), external);
                    } else {
                        auto locked_path = path;
                        if (fault == "lock")
                            locked_path += ".lock";
                        profile_handle.value = CreateFileW(locked_path.c_str(), GENERIC_READ,
                            fault == "lock" ? 0 : FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
                        require(profile_handle.value != INVALID_HANDLE_VALUE, "FIXTURE_PROFILE_LOCK_FAILED");
                    }
                };
        }
        if (config.value("initial_vpn_contract", false)) {
            std::ofstream(maafw::path_from_utf8(config.at("output"))) <<
                initial_vpn_contract(definition, coordinator, device, registry).dump(2);
            return 0;
        }
        coordinator.start(definition, device);
        J lifecycle_stop;
        if (device->hold_lifecycle) {
            until([&] { return device->lifecycle_count > 0 || coordinator.snapshot().quiescent; }, 15000ms);
            coordinator.request_stop();
            if (device->ignore_lifecycle_cancel) {
                // 专属测试端口故意迟到返回；无论断言结果如何，都释放本测试持有的阻塞。
                struct Release {
                    WorkflowDevice &device;
                    ~Release() { device.release_lifecycle = true; }
                } release{*device};
                until([&] { return coordinator.snapshot().reason == "STOP_TIMEOUT"; }, 5000ms);
                lifecycle_stop = storage::snapshot_json(coordinator.snapshot());
                auto other = definition;
                other.request_id = "late-lifecycle-other";
                try {
                    coordinator.start(other, device);
                    lifecycle_stop["new_run_error"] = "";
                } catch (const std::exception &e) {
                    lifecycle_stop["new_run_error"] = e.what();
                }
            }
        }
        if (config.value("stop_after_first", false) || config.contains("stop_after_calls")) {
            // 前置观察可能超过通用夹具的 5 秒。等待本 Session 公开预算内的首个输入，
            // 停止响应时间仍由 request_stop 后的正式 stop_timeout 约束。
            const auto stop_after = config.value("stop_after_calls", 1u);
            require(stop_after > 0 && stop_after <= device->transitions.size(), "FIXTURE_STOP_COUNT_INVALID");
            until([&] { return device->calls.load() >= stop_after || coordinator.snapshot().quiescent; }, definition.initial.time_limit);
            coordinator.request_stop();
        }
        bool stop_node_observed = false;
        if (config.contains("stop_at_node")) {
            until([&] {
                const auto journal = coordinator.events();
                for (const auto &event : journal.at("events"))
                    if (event.at("type") == "Node.Action.Starting" &&
                        event.at("payload").value("name", "") == config.at("stop_at_node").get<std::string>())
                        stop_node_observed = true;
                return stop_node_observed || coordinator.snapshot().quiescent;
            }, 15000ms);
            coordinator.request_stop();
        }
        auto progress_at = std::chrono::steady_clock::now();
        until(
            [&] {
                auto s = coordinator.snapshot();
                const auto now = std::chrono::steady_clock::now();
                if (now - progress_at >= 30s) {
                    // 只读夹具诊断，不改变画面、时钟或终态；长流程不再直到结束才有任何进度。
                    std::cout << J{{"fixture_progress", {{"backend_calls", device->calls.load()},
                        {"captures", device->captures.load()}, {"quiescent", s.quiescent}}}}.dump() << std::endl;
                    progress_at = now;
                }
                return s.quiescent && s.result_saved;
            },
            definition.initial.time_limit * (definition.recovery_limit + units) + 10000ms);
        J output{{"snapshot", storage::snapshot_json(coordinator.snapshot())},
                 {"task_plan", task_plan},
                 {"backend_calls", device->calls.load()},
                 {"connections", device->connections.load()},
                 {"cursor", device->cursor},
                 {"mismatch", device->mismatch},
                 {"mismatch_detail", device->mismatch_detail},
                 {"captures", device->captures.load()},
                 {"images", workflow.images},
                 {"required_actions", workflow.required_actions},
                 {"kind", workflow.kind},
                 {"node_count", workflow.nodes.size()},
                 {"time_limit_ms", workflow.time_limit.count()},
                 {"image_sources", image_sources},
                 {"lifecycle_calls", device->lifecycle_calls},
                 {"recovery_parameters", definition.recover ? definition.recover->parameters : J(nullptr)},
                 {"lifecycle_stop", lifecycle_stop},
                 {"time_event_count", device->time_event_count},
                 {"stop_node_observed", stop_node_observed},
                 {"loaded_modules", loaded_vision_modules()}};
        if (karma_store) {
            output["profile_before"] = original_profile;
            output["profile_after"] = karma_store->load();
        }
        std::ofstream(maafw::path_from_utf8(config.at("output"))) << output.dump(2);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what();
        return 1;
    }
}
