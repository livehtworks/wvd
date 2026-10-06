#pragma once
#include "games/wvd/native_operations.hpp"
#include "games/wvd/state.hpp"
#include "games/wvd/tasks/native_program.hpp"
#include "storage/legacy_import.hpp"
#include "runtime/native_run_coordinator.hpp"
#include <opencv2/imgcodecs.hpp>

namespace {
using namespace wvd;
using namespace std::chrono_literals;
// Only device pixels/delivery and visual leaves are scripted. The coordinator,
// input receipts, WVD operations, business state and PNG storage are production.
int combat_diagnostic_drive(const std::filesystem::path &root) {
    using J = nlohmann::json;
    using C = games::tasks::PipelineCompiler;
    const auto check = [](bool value, const char *code) {
        if (!value) throw std::runtime_error(code);
    };
    const auto read = [](const std::filesystem::path &path) {
        std::ifstream input(path); return J::parse(input);
    };
    const J skill{{"role_var", "0 面具"}, {"skill_var", "左上技能"}, {"skill_lvl", 2},
        {"target_var", "左上角色"}, {"freq_var", "用完后移除"}};
    auto profile = storage::LegacyConfigImporter(read("packs/wvd/parameters/legacy-config-fields.json"))
        .parse({{"GENERAL", J::object()}}).values;
    profile["TASK_SPECIFIC_CONFIG"] = false;
    profile["DEFAULT_OVERALL_STRATEGY"] = "diagnostic-drive";
    profile["STRATEGY"] = J::array({J{{"group_name", "diagnostic-drive"},
        {"complete_one_as_all", false}, {"skill_settings", J::array({skill})}}});
    const J menu{{"mode", "drive_menu"}}, button{{"mode", "drive_button"}},
        detail{{"mode", "drive_detail"}}, defended{{"mode", "drive_defended"}};
    C graph("closure.combat_diagnostic_drive", 10s);
    graph.route("Entry", {"Prepare"});
    graph.combat_step("Prepare", menu, {{"operation", "prepare"},
        {"portraits", J::array({J{{"role", "0 面具"}, {"image", "fixture-portrait"}}})},
        {"catalog", J::array({skill})}}, {"Open"});
    graph.click("Open", menu, button, detail, {"UnexpectedDetail"});
    graph.postcondition_budget("Open", 2200);
    graph.retry_menu_input("Open", menu, 1000);
    graph.failure_route("Open", {"Defend"});
    graph.recovery("UnexpectedDetail", "FIXTURE_DETAIL_MUST_STAY_CLOSED");
    graph.fixed_click("Defend", menu, defended, {570, 1200}, {"ConfirmDefend"});
    graph.combat_step("ConfirmDefend", defended,
        {{"operation", "defend_fallback_confirmed"}, {"index", 0}}, {"Terminal"});
    auto flow = graph.finish();
    const J paths{{"Open", J::array({J{{"flow_id", "combat-open-detail"}, {"node_id", "open"}}})}};
    auto program = games::tasks::compile_native_program(flow, paths, "native-test");
    class CombatBackend final : public devices::DeviceBackend {
      public:
        std::atomic<unsigned> open_inputs{0}, defend_inputs{0};
        bool offline() const override { return true; }
        bool verified_access() const override { return true; }
        bool connect() override { return true; }
        bool release_owned_inputs() override { return true; }
        devices::RawFrame capture() override {
            devices::RawFrame frame;
            frame.size = {900, 1600};
            frame.device_id = "native-test";
            frame.viewport_id = "900x1600";
            frame.foreground_application = "jp.co.drecom.wizardry.daphne";
            frame.captured_at = frame.capture_finished_at = std::chrono::steady_clock::now();
            frame.connection_generation = 1;
            frame.display_rotation = 0;
            const auto color = defend_inputs ? 83 : 37;
            frame.raw_bgr = std::make_shared<const std::vector<std::uint8_t>>(900 * 1600 * 3,
                static_cast<std::uint8_t>(color));
            return frame;
        }
        bool execute(const contracts::Command &command) override {
            if (command.x == 570 && command.y == 1200) ++defend_inputs;
            else ++open_inputs;
            return true;
        }
    };
    const auto backend = std::make_shared<CombatBackend>();
    const auto bundle_root = root / "drive-bundle";
    std::filesystem::create_directories(bundle_root);
    { std::ofstream marker(bundle_root / "marker.txt"); marker << "isolated diagnostic drive"; }
    recognition::Bundle bundle{bundle_root, "native-test",
        {{"marker.txt", platform::file_sha256(bundle_root / "marker.txt")}}};
    recognition::Handlers leaves;
    leaves["WvdVision"] = [backend](const auto &, auto, const J &condition, const auto &, auto &) {
        std::function<bool(const J &)> evaluate = [&](const J &value) {
            const auto mode = value.value("mode", "");
            if (mode == "all" || mode == "any" || mode == "not") {
                bool all = true, any = false;
                for (const auto &child : value.at("conditions")) {
                    const auto hit = evaluate(child); all &= hit; any |= hit;
                }
                return mode == "all" ? all : mode == "any" ? any : !any;
            }
            return mode == "portrait" || mode == "input_clear" || mode == "region_quiet" ||
                mode == "drive_button" || (mode == "drive_menu" && !backend->defend_inputs) ||
                (mode == "drive_defended" && backend->defend_inputs);
        };
        const bool hit = evaluate(condition);
        return J{{"schema", 1}, {"outcome", hit ? "Hit" : "NoHit"}, {"box", {400, 800, 50, 50}},
            {"target", hit}, {"evidence", {{"best_score", hit ? .97 : .1},
                {"identity_basis", "isolated_visual_leaf"}}}};
    };
    runtime::NativeRunDefinition definition;
    definition.request_id = "combat-diagnostic-drive";
    definition.policy.device_id = "native-test";
    definition.policy.game_id = "wvd";
    definition.policy.application_id = "jp.co.drecom.wizardry.daphne";
    definition.policy.pack_revision = "native-test";
    definition.policy.viewport_id = "900x1600";
    definition.policy.recognition_size = {900, 1600};
    definition.policy.allowed_scenes.insert("wvd");
    definition.policy.capabilities.insert(contracts::ActionKind::Click);
    definition.policy.permissions.insert(contracts::ActionKind::Click);
    definition.units.push_back({std::make_shared<const workflow::FlowProgram>(std::move(program)),
        std::move(bundle), std::move(leaves), J::array({J{{"native_node", flow.checkpoint}}}).dump(), 10s});
    definition.total_time_limit = 10s;
    definition.create_state = [profile](const auto &creation) {
        return std::make_unique<games::WvdRunState>(profile, creation);
    };
    definition.operations = [](auto &state, auto event, auto checkpoint) {
        return games::wvd_operation_factory(dynamic_cast<games::WvdRunState &>(state),
            std::move(event), std::move(checkpoint));
    };
    definition.logging.level = storage::LogLevel::Off;
    definition.logging.performance = false;
    runtime::NativeRunCoordinator coordinator(root / "drive");
    coordinator.start(std::move(definition), backend);
    check(coordinator.wait_for(12s) && coordinator.wait_for_worker(2s), "DIAGNOSTIC_DRIVE_DID_NOT_FINISH");
    const auto snapshot = coordinator.snapshot();
    std::cout << "drive state=" << static_cast<int>(snapshot.state) << " reason=" << snapshot.reason << '\n';
    check(snapshot.state == contracts::RunState::Completed && snapshot.quiescent &&
        backend->open_inputs >= 2 && backend->defend_inputs == 1, "DIAGNOSTIC_DRIVE_RESULT_OR_INPUT_COUNT");
    const auto diagnostics = coordinator.diagnostics();
    const auto &entries = diagnostics.at("entries");
    check(diagnostics.at("complete") == true && entries.size() == 2 &&
        entries.at(0).at("operation_id") == entries.at(1).at("operation_id"), "DIAGNOSTIC_DRIVE_PAIR_MISSING");
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto &entry = entries.at(i);
        const auto path = coordinator.run_directory() / entry.at("path").get<std::string>();
        const auto image = cv::imread(path.string());
        check(entry.at("status") == "saved" && image.rows == 1600 && image.cols == 900 &&
            image.at<cv::Vec3b>(0, 0) == cv::Vec3b::all(i ? 83 : 37) &&
            platform::file_sha256(path) == entry.at("sha256").get<std::string>() &&
            entry.at("context").at("selection").at("configured_skill") == skill,
            "DIAGNOSTIC_DRIVE_ORIGINAL_FRAME_OR_CONFIG");
    }
    const auto result = read(coordinator.run_directory() / "result.json");
    check(result.at("state") == "Completed" && result.at("business").at("strategy")
        .at("current").at("skill_settings").size() == 1, "DIAGNOSTIC_DRIVE_CONSUMED_FAILED_SKILL");
    std::ifstream events(coordinator.run_directory() / "execution-events.jsonl");
    std::string line;
    unsigned no_progress = 0, diagnostic_events = 0, defenses = 0;
    while (std::getline(events, line)) {
        const auto event = J::parse(line);
        const auto &payload = event.at("payload");
        no_progress += event.at("type") == "input.result" && payload.value("outcome", "") == "no_progress";
        diagnostic_events += event.at("type") == "diagnostic.combat_no_progress";
        defenses += event.at("type") == "combat" && payload.value("operation", "") == "defend_fallback_confirmed";
    }
    check(no_progress == 1 && diagnostic_events == 2 && defenses == 1, "DIAGNOSTIC_DRIVE_PERMANENT_EVENTS_MISSING");
    std::cout << "combat diagnostic drive: actual no_progress, WVD defense, two original PNGs, Completed with skill retained\n";
    std::cout << "Evidence: " << coordinator.run_directory() << '\n';
    return 0;
}
}
