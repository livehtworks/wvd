#include "runtime_fixture.hpp"
#include "loaded_modules.hpp"
#include "games/wvd/chest/chest.hpp"
#include "games/wvd/combat/encounter.hpp"
#include "games/wvd/diagnostics.hpp"
#include "games/wvd/navigation/map_route.hpp"
#include "games/wvd/recovery/boot.hpp"
#include "games/wvd/recovery/dialogue.hpp"
#include "games/wvd/state.hpp"
#include "games/wvd/tasks/cave_of_separation.hpp"
#include "games/wvd/tasks/workflow_session.hpp"
#include "games/wvd/vision/recognizers.hpp"
#include "storage/legacy_import.hpp"
#include "storage/run_store.hpp"
#include <iostream>

using namespace fixture;
namespace {
using C = games::tasks::PipelineCompiler;
using D = games::recovery::DialoguePolicy;
using S = games::tasks::CaveOfSeparationSegment;

// 只接受本用例明确列出的输入，截图本身不推进剧情，也不提供任何设备生命周期入口。
class CaveDevice final : public OfflineDevice {
  public:
    std::vector<std::vector<std::uint8_t>> frames;
    J transitions = J::array();
    std::size_t cursor{};
    J mismatch;
    std::function<void()> after_input;
    devices::RawFrame capture() override {
        std::lock_guard lock(mutex);
        ++captures;
        return {frames.at(cursor), size, identity, viewport, application};
    }
    bool execute(const contracts::Command &command) override {
        std::lock_guard lock(mutex);
        ++calls;
        sent.push_back(command);
        const J actual{{"kind", int(command.kind)}, {"x", command.x}, {"y", command.y},
            {"x2", command.x2}, {"y2", command.y2}, {"duration", command.duration}, {"key", command.key}};
        if (cursor >= transitions.size()) {
            mismatch = {{"index", cursor}, {"actual", actual}, {"expected", nullptr}};
            return false;
        }
        const auto &expected = transitions.at(cursor);
        for (const auto &[key, value] : actual.items()) {
            if (value != expected.value(key, 0)) {
                mismatch = {{"index", cursor}, {"actual", actual}, {"expected", expected}};
                return false;
            }
        }
        if (after_input) after_input();
        if (expected.value("reject", false)) return false;
        ++cursor;
        return true;
    }
};

games::WvdQuestDefinition quest() { return {"CaveOfSeperation", "quest", {{"_TYPE", "quest"}}}; }
games::tasks::CompiledWorkflow compile(const J &config, const J &profile) {
    const auto policy = games::recovery::dialogue_policy_from_name(config.value("dialogue_task", ""));
    const auto kind = config.at("workflow").get<std::string>();
    if (kind == "common") return games::recovery::clear_common_screens(true, policy);
    if (kind == "special") return games::recovery::choose_special_dialogue(policy);
    if (kind == "default") {
        auto graph = games::recovery::choose_default_dialogue();
        C wrapper("fixture.cos.default", graph.time_limit + 10s);
        wrapper.use_dialogue(policy);
        wrapper.route("Entry", {"Choose"});
        const auto child = wrapper.define_child("Dialogue", graph);
        wrapper.call_child("Choose", child, {"Terminal"});
        return wrapper.finish();
    }
    if (kind == "probe" || kind == "forbidden-click") {
        C graph("fixture.cos." + kind, 10s);
        graph.use_dialogue(policy);
        if (kind == "probe") {
            graph.route("Entry", {"Matched", "Missing"});
            graph.observe("Matched", config.at("condition"), {"Terminal"});
            graph.observe("Missing", C::absent(config.at("condition")), {"NoHit"});
            graph.recovery("NoHit", "fixture.cos_probe_no_hit");
        } else {
            graph.route("Entry", {"Click"});
            graph.fixed_click("Click", {{"mode", "task_stop"}}, C::image("Inn"), {450, 760}, {"Terminal"});
        }
        return graph.finish();
    }
    if (kind == "map" || kind == "route") {
        const auto segment = policy == D::CaveOfSeparationEna ? S::B2 : S::B3;
        const auto stop = policy == D::CaveOfSeparationEna ? games::tasks::DungeonTaskStop::CaveEna :
            games::tasks::DungeonTaskStop::CaveRequest;
        require(policy == D::CaveOfSeparationEna || policy == D::CaveOfSeparationRequest, "FIXTURE_STOP_POLICY_REQUIRED");
        const auto plan = games::tasks::cave_of_separation_plan(quest(), segment);
        const auto route = kind == "map" ? games::navigation::reach_map_target(plan.route().front()) :
            games::tasks::traverse_dungeon(plan, profile, {}, true, policy, stop);
        C graph("fixture.cos." + kind, route.time_limit + 30s);
        graph.use_dialogue(policy);
        graph.route("Entry", {"Route"});
        const auto child = graph.define_child("Dungeon", route, kind == "map" ? std::vector<std::string>{"BlockedExit"} : std::vector<std::string>{});
        graph.call_child("Route", child, {"FreshStop", "Missing"});
        // 这是共享停点链的分项断言，不冒充整个 COS 的阶段/周期回执。
        graph.observe("FreshStop", C::image(games::tasks::dungeon_task_stop_image(stop)), {"Terminal"});
        graph.recovery("Missing", "fixture.cos_stop_missing");
        return graph.finish();
    }
    throw std::runtime_error("FIXTURE_COS_WORKFLOW_INVALID");
}
}

int main(int argc, char **argv) {
    if (argc != 2) return 1;
    const auto input = std::filesystem::absolute(maafw::path_from_utf8(argv[1]));
    const auto root = input.parent_path();
    std::shared_ptr<CaveDevice> device;
    auto output = [&](J result) {
        result["loaded_modules"] = loaded_vision_modules();
        std::ofstream file(root / "output.json", std::ios::binary);
        file << result.dump(2);
        file.close();
        require(bool(file), "FIXTURE_RESULT_WRITE_FAILED");
    };
    try {
        J config, descriptor;
        std::ifstream(input) >> config;
        require(maafw::path_from_utf8(config.at("isolated_case")) == root, "FIXTURE_ISOLATION_REQUIRED");
        std::ifstream(maafw::path_from_utf8(config.at("descriptor"))) >> descriptor;
        storage::LegacyConfigImporter importer(descriptor);
        auto profile = importer.parse({{"GENERAL", J::object()}}).values;
        profile.update(config.value("profile", J::object()));
        if (config.at("workflow") == "cos-plan") {
            J rows = J::array();
            for (const auto segment : {S::Preparation, S::B1, S::B2, S::B3, S::Back, S::ReturnCity}) {
                const auto graph = games::tasks::cave_of_separation_segment(quest(), profile, {}, segment);
                rows.push_back({{"segment", int(segment)}, {"plan", games::tasks::cave_of_separation_plan(quest(), segment).inspect()},
                    {"images", graph.images}, {"dialogue_task", games::recovery::dialogue_policy_name(graph.dialogue_policy)},
                    {"time_limit_ms", graph.time_limit.count()}, {"checkpoint", graph.checkpoint}, {"node_count", graph.nodes.size()}});
            }
            output({{"segments", rows}, {"backend_calls", 0}, {"connections", 0}});
            return 0;
        }
        const auto workflow = compile(config, profile);
        if (config.value("inspect", false)) {
            output({{"images", workflow.images}, {"nodes", workflow.nodes}, {"time_limit_ms", workflow.time_limit.count()},
                {"backend_calls", 0}, {"connections", 0}});
            return 0;
        }
        require(!std::filesystem::exists(root / "compiled") && !std::filesystem::exists(root / "run"), "FIXTURE_OUTPUT_EXISTS");
        maafw::Bundle bundle{root / "bundle", "cos-isolated-fixture", {}};
        for (const auto &file : config.at("files")) bundle.files.push_back({file.at("path"), file.at("sha256")});
        device = std::make_shared<CaveDevice>();
        for (const auto &frame : config.at("frames")) device->frames.push_back(bytes(root / maafw::path_from_utf8(frame)));
        device->transitions = config.at("transitions");
        require(device->frames.size() == device->transitions.size() + 1, "FIXTURE_CAUSAL_FRAME_COUNT_INVALID");
        auto registry = std::make_shared<runtime::BehaviorRegistry>("m4-cos-isolated-1");
        games::vision::register_wvd(*registry);
        games::register_wvd_state(*registry);
        games::register_wvd_confirmations(*registry);
        games::combat::register_combat(*registry);
        games::chest::register_chest(*registry);
        registry->seal();
        const auto session = games::tasks::publish_workflow(workflow, bundle, *registry, root / "compiled",
            config.value("aliases", J::object()));
        contracts::InputPolicy policy{"m2-offline", "wvd", "fixture.app", session.bundle.revision, "portrait", {900, 1600},
            {contracts::ActionKind::Click, contracts::ActionKind::ClickKey, contracts::ActionKind::Swipe},
            {contracts::ActionKind::Click, contracts::ActionKind::ClickKey, contracts::ActionKind::Swipe}, {"wvd"}, 2000ms};
        runtime::RunDefinition definition;
        definition.request_id = "cos-isolated"; definition.policy = policy; definition.initial = session;
        definition.state_factory = games::wvd_state_binding(profile);
        runtime::RunCoordinator coordinator(root / "run", registry);
        if (config.value("stop_after_input", false)) device->after_input = [&] { coordinator.request_stop(); };
        coordinator.start(definition, device);
        until([&] {
            const auto snapshot = coordinator.snapshot();
            return snapshot.quiescent && (snapshot.result_saved || !snapshot.storage_error.empty());
        }, session.time_limit + 10000ms);
        const auto before = device->calls.load();
        std::this_thread::sleep_for(100ms);
        require(before == device->calls.load(), "FIXTURE_INPUT_AFTER_QUIESCENCE");
        output({{"snapshot", storage::snapshot_json(coordinator.snapshot())}, {"events", coordinator.events()},
            {"backend_calls", before}, {"connections", device->connections.load()}, {"mismatch", device->mismatch},
            {"cursor", device->cursor}, {"images", workflow.images}});
        return 0;
    } catch (const std::exception &error) {
        output({{"error", error.what()}, {"backend_calls", device ? device->calls.load() : 0},
            {"connections", device ? device->connections.load() : 0}});
        return 0;
    }
}
