#include "runtime_fixture.hpp"
#include "loaded_modules.hpp"
#include "games/wvd/navigation/world_map.hpp"
#include "games/wvd/navigation/map_route.hpp"
#include "games/wvd/navigation/auto_route.hpp"
#include "games/wvd/navigation/time_leap.hpp"
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
    J time_event;
    std::optional<std::chrono::steady_clock::time_point> time_event_due;
    unsigned time_event_count{};
    bool allow_lifecycle{}, stale_lifecycle{}, wrong_instance{}, hold_lifecycle{}, ignore_lifecycle_cancel{};
    std::atomic<bool> release_lifecycle{};
    int failed_starts{}, start_attempts{};
    std::size_t restart_frame{1}, restart_action{};
    std::chrono::milliseconds returned_frame_age{};
    std::atomic<unsigned> lifecycle_count{};
    J lifecycle_calls = J::array();
    bool enforce_connection_state{}, connection_throws{};
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
        if (operation == O::EnsureVpn)
            lifecycle_state.vpn_ready = true;
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
        std::lock_guard lock(mutex);
        // 只按显式时钟事件推进动画；重复截图本身不能产生业务进展。
        if (time_event_due && std::chrono::steady_clock::now() >= *time_event_due) {
            cursor = time_event.at("frame").get<std::size_t>();
            time_event_due.reset();
            ++time_event_count;
        }
        ++captures;
        return {frames.at(cursor), size, identity, viewport, application,
                std::chrono::steady_clock::now() - returned_frame_age, "fixture",
                allow_lifecycle ? lifecycle_state.connection_generation : 0};
    }
    bool execute(const contracts::Command &c) override {
        std::lock_guard lock(mutex);
        ++calls;
        sent.push_back(c);
        if (action_cursor >= transitions.size()) {
            mismatch = true;
            return false;
        }
        const auto &expected = transitions.at(action_cursor);
        if (int(c.kind) != expected.at("kind").get<int>() || c.x != expected.value("x", 0) ||
            c.y != expected.value("y", 0) || c.key != expected.value("key", 0) ||
            c.x2 != expected.value("x2", 0) || c.y2 != expected.value("y2", 0) ||
            c.duration != expected.value("duration", 0)) {
            mismatch = true;
            return false;
        }
        if (expected.value("reject", false))
            return false;
        if (after_input)
            after_input();
        if (!expected.value("stay", false)) {
            ++cursor;
            ++action_cursor;
        }
        if (time_event.is_object() && !time_event_count && !time_event_due &&
            action_cursor == time_event.at("after_input").get<std::size_t>())
            time_event_due = std::chrono::steady_clock::now() +
                             std::chrono::milliseconds(time_event.at("delay_ms").get<int>());
        return true;
    }
};
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
            if (config.contains("max_crashes"))
                profile["MAX_CRASH_LIMIT"] = config.at("max_crashes");
        }
        J task_plan;
        auto workflow = [&] {
            const auto kind = config.at("workflow").get<std::string>();
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
            if (kind == "time-leap")
                return games::navigation::time_leap_without_causality(config.at("leap_target"),
                    config.value("leap_chapter", "cursedwheel_impregnableFortress"), config.value("allow_download", true));
            if (kind == "bounty-visit")
                return games::tasks::visit_bounty_board(config.value("report", false) ? games::tasks::BountyVisit::Report : games::tasks::BountyVisit::Reveal);
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
                return games::recovery::clear_common_screens(config.value("allow_download", true));
            if (kind == "fortress-trap" || kind == "giant" || kind == "dark-light" || kind == "mining" || kind == "manual-separation" || kind == "scorpion") {
                nlohmann::ordered_json source;
                std::ifstream(maafw::path_from_utf8(config.at("quest_catalog"))) >> source;
                games::WvdQuestCatalog catalog(source);
                const auto &task = catalog.at(kind == "scorpion" ? (config.value("hands", false) ? "Scorpionesses_plus_6_hands" : "Scorpionesses") : kind == "manual-separation" ? "manualSepDemon" : kind == "mining" ? "FFXI-Org" : kind == "dark-light" ? "darkLight" : kind == "giant" ? "gaintKiller" : "fortress-B8F_trap");
                std::set<std::string> images;
                for (const auto &file : config.at("files")) {
                    const auto path = file.at("path").get<std::string>();
                    if (path.starts_with("image/"))
                        images.insert(path.substr(6));
                }
                if (kind == "scorpion") {
                    task_plan = games::tasks::scorpion_plan(task).inspect();
                    return games::tasks::scorpion_cycle(task, profile, images, config.value("allow_download", true));
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
                games::WvdQuestDefinition definition{"route-fixture", config.value("route_type", std::string("dungeon")),
                    {{"_EOT", {{"press", "Dist", {1, 1}, 1}}}, {"_TARGETINFOLIST", config.at("route_targets")}}};
                if (config.contains("floor"))
                    definition.source["_FloorCheck"] = config.at("floor");
                if (config.contains("entry_steps"))
                    definition.source["_EOT"] = config.at("entry_steps");
                if (config.contains("return_destination"))
                    definition.source["_RTT"] = config.at("return_destination");
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
                games::WvdQuestDefinition definition{"entry-fixture", "dungeon",
                    {{"_EOT", config.at("entry_steps")}, {"_TARGETINFOLIST", {{"chest"}}}}};
                if (config.contains("pre_entry"))
                    definition.source["_preEOTcheck"] = config.at("pre_entry");
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
        if (config.value("attach_recovery", false))
            workflow = games::recovery::with_boot_recovery(workflow, config.value("allow_download", true));
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
        if (config.contains("time_event")) {
            device->time_event = config.at("time_event");
            require(device->time_event.at("after_input").get<std::size_t>() > 0 &&
                        device->time_event.at("after_input").get<std::size_t>() <= device->transitions.size() &&
                        device->time_event.at("frame").get<std::size_t>() < device->frames.size() &&
                        device->time_event.at("delay_ms").get<int>() > 0 &&
                        device->time_event.at("delay_ms").get<int>() <= 10000, "FIXTURE_TIME_EVENT_INVALID");
        }
        const bool recovering = config.at("workflow") == "recover" || config.value("attach_recovery", false);
        device->allow_lifecycle = recovering && !config.value("no_lifecycle_port", false);
        device->stale_lifecycle = config.value("stale_lifecycle", false);
        device->wrong_instance = config.value("other_lifecycle_instance", false);
        device->failed_starts = config.value("fail_starts", 0);
        device->restart_frame = config.value("restart_frame", std::size_t{1});
        const auto frame_age = config.value("returned_frame_age_ms", 0);
        require(frame_age >= 0 && frame_age <= 60000, "FIXTURE_FRAME_AGE_INVALID");
        device->returned_frame_age = std::chrono::milliseconds{frame_age};
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
        if (config.value("destination_exists", false))
            std::filesystem::create_directory(root.parent_path() / "compiled");
        try {
            session = games::tasks::publish_workflow(workflow, bundle, *registry,
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
            2000ms};
        policy.permissions.clear();
        for (const auto &kind : workflow.required_actions)
            policy.permissions.insert(kind == "Click"      ? contracts::ActionKind::Click
                                      : kind == "ClickKey" ? contracts::ActionKind::ClickKey
                                                           : contracts::ActionKind::Swipe);
        runtime::RunCoordinator coordinator(maafw::path_from_utf8(config.at("run_root")), registry);
        runtime::RunDefinition definition;
        definition.request_id = "m4-causal";
        definition.policy = policy;
        definition.initial = std::move(session);
        const auto units = config.at("workflow") == "scorpion" ? (config.value("hands", false) ? 4u : 3u) :
            config.at("workflow") == "manual-separation" ? 2u : config.value("normal_units", 1u);
        require(units > 0 && units <= 4, "FIXTURE_NORMAL_UNITS_INVALID");
        if (config.at("workflow") == "manual-separation")
            games::tasks::configure_manual_separation_units(definition);
        else if (config.at("workflow") == "scorpion")
            games::tasks::configure_scorpion_units(definition, config.value("hands", false));
        else {
            definition.max_business_units = units;
            for (unsigned i = 1; i < units; ++i)
                definition.continuation_units.push_back(definition.initial);
        }
        if (recovering && !config.value("omit_recovery_policy", false)) {
            auto target = device->lifecycle_state.target;
            if (config.value("other_lifecycle_app", false))
                target.application_id = "not-the-game";
            definition.recover = games::recovery::recovery_binding(target, config.value("force_restart", false),
                config.value("max_crashes", std::int64_t(10)));
            definition.recovery_limit = 3;
        }
        if (config.value("with_state", false))
            definition.state_factory = games::wvd_state_binding(profile);
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
        until(
            [&] {
                auto s = coordinator.snapshot();
                return s.quiescent && s.result_saved;
            },
            definition.initial.time_limit * (definition.recovery_limit + units) + 10000ms);
        J output{{"snapshot", storage::snapshot_json(coordinator.snapshot())},
                 {"task_plan", task_plan},
                 {"backend_calls", device->calls.load()},
                 {"cursor", device->cursor},
                 {"mismatch", device->mismatch},
                 {"captures", device->captures.load()},
                 {"images", workflow.images},
                 {"required_actions", workflow.required_actions},
                 {"kind", workflow.kind},
                 {"node_count", workflow.nodes.size()},
                 {"time_limit_ms", workflow.time_limit.count()},
                 {"image_sources", image_sources},
                 {"lifecycle_calls", device->lifecycle_calls},
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
