#include "runtime_fixture.hpp"
#include "loaded_modules.hpp"
#include "games/wvd/navigation/world_map.hpp"
#include "games/wvd/navigation/map_route.hpp"
#include "games/wvd/navigation/world_travel.hpp"
#include "games/wvd/supply/party.hpp"
#include "games/wvd/chest/chest.hpp"
#include "games/wvd/diagnostics.hpp"
#include "games/wvd/state.hpp"
#include "storage/legacy_import.hpp"
#include "games/wvd/supply/inn.hpp"
#include "games/wvd/combat/auto_combat.hpp"
#include "games/wvd/tasks/workflow_session.hpp"
#include "games/wvd/vision/recognizers.hpp"
#include "platform/windows/file_digest.hpp"
#include <iostream>

using namespace fixture;
// 场景是因果图，不是随 capture 次数前进的录像。额外输入、错误坐标和错误按键都会失败。
class WorkflowDevice final : public OfflineDevice {
  public:
    std::vector<std::vector<std::uint8_t>> frames;
    J transitions;
    std::size_t cursor{};
    bool mismatch{};
    devices::RawFrame capture() override {
        std::lock_guard lock(mutex);
        ++captures;
        return {frames.at(cursor), size, identity, viewport, application};
    }
    bool execute(const contracts::Command &c) override {
        std::lock_guard lock(mutex);
        ++calls;
        sent.push_back(c);
        if (cursor >= transitions.size()) {
            mismatch = true;
            return false;
        }
        const auto &expected = transitions.at(cursor);
        if (int(c.kind) != expected.at("kind").get<int>() || c.x != expected.value("x", 0) ||
            c.y != expected.value("y", 0) || c.key != expected.value("key", 0) ||
            c.x2 != expected.value("x2", 0) || c.y2 != expected.value("y2", 0) ||
            c.duration != expected.value("duration", 0)) {
            mismatch = true;
            return false;
        }
        if (expected.value("reject", false))
            return false;
        if (!expected.value("stay", false))
            ++cursor;
        return true;
    }
};
int main(int argc, char **argv) {
    try {
        require(argc == 2, "CONFIG_REQUIRED");
        J config;
        std::ifstream(maafw::path_from_utf8(argv[1])) >> config;
        auto workflow = [&] {
            const auto kind = config.at("workflow").get<std::string>();
            if (kind == "city")
                return games::navigation::enter_city(config.at("city"));
            if (kind == "inn")
                return games::supply::rest_at_inn(config.value("royal", false));
            if (kind == "auto")
                return games::combat::enable_auto();
            if (kind == "chest")
                return games::chest::open_chest(config.value("preferred", 1), config.value("quick", false), 42);
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
        if (config.contains("invalid")) {
            if (config["invalid"] == "raw-input")
                workflow.nodes["Entry"]["action"] = "Click";
            else if (config["invalid"] == "unknown-next")
                workflow.nodes["Entry"]["next"] = {"Missing"};
            else if (config["invalid"] == "unbounded")
                workflow.nodes["Entry"]["max_hit"] = 0;
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
        auto device = std::make_shared<WorkflowDevice>();
        for (const auto &frame : config.at("frames"))
            device->frames.push_back(bytes(maafw::path_from_utf8(frame)));
        device->transitions = config.at("transitions");
        auto registry = std::make_shared<runtime::BehaviorRegistry>("m4-workflows-1");
        if (!config.value("missing_binding", false))
            games::vision::register_wvd(*registry);
        games::register_wvd_state(*registry);
        games::register_wvd_confirmations(*registry);
        registry->seal();
        runtime::SessionDefinition session;
        if (config.value("destination_exists", false))
            std::filesystem::create_directory(root.parent_path() / "compiled");
        try {
            session = games::tasks::publish_workflow(workflow, bundle, *registry,
                                                     root.parent_path() / "compiled",
                                                     config.value("aliases", J::object()));
        } catch (const std::exception &e) {
            J output{{"publish_error", e.what()},
                     {"backend_calls", device->calls.load()},
                     {"connections", device->connections.load()},
                     {"loaded_modules", loaded_vision_modules()}};
            std::ofstream(maafw::path_from_utf8(config.at("output"))) << output.dump(2);
            return 0;
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
        if (config.value("with_state", false)) {
            J descriptor;
            std::ifstream(maafw::path_from_utf8(config.at("descriptor"))) >> descriptor;
            storage::LegacyConfigImporter importer(descriptor);
            auto profile = importer.parse({{"GENERAL", J::object()}});
            definition.state_factory = games::wvd_state_binding(profile.values);
        }
        coordinator.start(definition, device);
        if (config.value("stop_after_first", false)) {
            until([&] { return device->calls.load() > 0 || coordinator.snapshot().quiescent; });
            coordinator.request_stop();
        }
        until(
            [&] {
                auto s = coordinator.snapshot();
                return s.quiescent && s.result_saved;
            },
            70000ms);
        J output{{"snapshot", storage::snapshot_json(coordinator.snapshot())},
                 {"backend_calls", device->calls.load()},
                 {"cursor", device->cursor},
                 {"mismatch", device->mismatch},
                 {"captures", device->captures.load()},
                 {"images", workflow.images},
                 {"required_actions", workflow.required_actions},
                 {"kind", workflow.kind},
                 {"loaded_modules", loaded_vision_modules()}};
        std::ofstream(maafw::path_from_utf8(config.at("output"))) << output.dump(2);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what();
        return 1;
    }
}
