#include "runtime_fixture.hpp"
#include "loaded_modules.hpp"
#include "games/wvd/diagnostics.hpp"
#include "games/wvd/state.hpp"
#include "games/wvd/vision/recognizers.hpp"
#include "games/wvd/tasks/workflow_session.hpp"
#include "storage/legacy_import.hpp"
#include "platform/windows/file_digest.hpp"
#include <iostream>
#include <windows.h>
#include <winioctl.h>

using namespace fixture;
namespace {
J read(const std::filesystem::path &path) {
    J result;
    std::ifstream input(path);
    require(bool(input) && bool(input >> result), "DIAGNOSTIC_FIXTURE_MISSING");
    return result;
}
class Clock final : public contracts::MonotonicClock {
  public:
    std::int64_t milliseconds{};
    TimePoint now() const noexcept override { return TimePoint{std::chrono::milliseconds{milliseconds}}; }
};
class Device final : public OfflineDevice {
  public:
    int fail_capture{};
    devices::RawFrame capture() override {
        if (fail_capture && captures + 1 == fail_capture) {
            ++captures;
            throw std::runtime_error("DIAGNOSTIC_TEST_CAPTURE_FAILED");
        }
        return OfflineDevice::capture();
    }
};
games::tasks::CompiledWorkflow reward_workflow() {
    using C = games::tasks::PipelineCompiler;
    C graph("fixture.diagnostic.reward", 90s);
    graph.route("Entry", {"Reward"});
    const J reward{{"mode", "fishing_reward"}};
    graph.confirm("Reward", "fishing.reward.prepare", "fishing_reward_prepared", reward, {"Replay"});
    graph.confirm("Replay", "fishing.reward.prepare", "fishing_reward_prepared", reward, {"Close"});
    graph.click("Close", C::image("fishing/CloseFishInfo"), C::image("fishing/CloseFishInfo"),
                C::image("dungFlag"), {"Completed"});
    graph.confirm("Completed", "fishing.reward.done", "fishing_reward_completed", C::image("dungFlag"), {"Terminal"});
    return graph.finish();
}
// 两端都在本轮隔离根。junction不需要开发者模式，不借用户目录构造负例。
void junction(const std::filesystem::path &link, const std::filesystem::path &target) {
    std::filesystem::create_directory(link);
    const auto print = std::filesystem::absolute(target).native();
    const auto substitute = L"\\??\\" + print;
    struct Buffer {
        DWORD tag;
        WORD length, reserved, substitute_offset, substitute_length, print_offset, print_length;
        wchar_t names[2048];
    } buffer{};
    require(substitute.size() + print.size() + 2 < 2048, "JUNCTION_PATH_TOO_LONG");
    buffer.tag = IO_REPARSE_TAG_MOUNT_POINT;
    buffer.substitute_length = static_cast<WORD>(substitute.size() * sizeof(wchar_t));
    buffer.print_offset = static_cast<WORD>((substitute.size() + 1) * sizeof(wchar_t));
    buffer.print_length = static_cast<WORD>(print.size() * sizeof(wchar_t));
    buffer.length = static_cast<WORD>(8 + buffer.print_offset + buffer.print_length + sizeof(wchar_t));
    std::copy(substitute.begin(), substitute.end(), buffer.names);
    std::copy(print.begin(), print.end(), buffer.names + substitute.size() + 1);
    auto handle = CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                             FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    require(handle != INVALID_HANDLE_VALUE, "JUNCTION_OPEN_FAILED");
    DWORD returned{};
    const bool ok = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, &buffer, buffer.length + 8,
                                   nullptr, 0, &returned, nullptr) != FALSE;
    CloseHandle(handle);
    require(ok, "JUNCTION_CREATE_FAILED");
}
J store_case(const J &config) {
    const auto root = maafw::path_from_utf8(config.at("run_root"));
    const auto mode = config.at("case").get<std::string>();
    auto clock = std::make_shared<Clock>();
    const J definition{{"device_id", "diagnostic-offline"}, {"game_id", "wvd"},
        {"pack_revision", "diagnostic-fixture"}, {"viewport", "portrait"}};
    auto limits = storage::DiagnosticLimits{};
    if (mode == "store-quota") limits = {2, 2, 8 * 1024 * 1024};
    storage::RunStore store(root, "diagnostic-test", 1, definition, clock, limits);
    contracts::FrameEnvelope frame;
    frame.encoded_image = bytes(maafw::path_from_utf8(config.at("before")));
    frame.identity.device_id = "diagnostic-offline";
    frame.identity.game_id = "wvd";
    frame.identity.pack_revision = "diagnostic-fixture";
    frame.identity.viewport_id = "portrait";
    frame.identity.generation = frame.identity.frame_id = 1;
    frame.identity.raw_size = frame.identity.recognition_size = {900, 1600};
    frame.identity.captured_at = std::chrono::steady_clock::now();
    storage::DiagnosticRequest request{1, 1, 0, 1, 0, "Reward", "fishing.reward", "reward", "fish-0", ""};
    J output;
    if (mode == "store-reparse") {
        const auto outside = root / "isolated-outside";
        std::filesystem::create_directory(outside);
        junction(store.directory() / "diagnostics", outside);
        output["receipt"] = store.save_diagnostic(&frame, request);
        output["outside_empty"] = std::filesystem::is_empty(outside);
    } else if (mode == "store-write-fail") {
        output["first"] = store.save_diagnostic(&frame, request);
        std::filesystem::create_directory(store.directory() / "diagnostics/2.png");
        request.operation_id = "fish-1";
        output["receipt"] = store.save_diagnostic(&frame, request);
        output["replay"] = store.save_diagnostic(&frame, request);
        std::size_t temporary{};
        for (const auto &file : std::filesystem::directory_iterator(store.directory() / "diagnostics"))
            if (file.path().extension() == ".tmp") ++temporary;
        output["temporary_files"] = temporary;
    } else if (mode == "store-quota") {
        for (int i = 0; i < 3; ++i) {
            request.operation_id = "fish-" + std::to_string(i);
            output["rewards"].push_back(store.save_diagnostic(&frame, request));
        }
        request.stage = "pre_action";
        request.operation_id.clear();
        for (int i = 0; i < 3; ++i) {
            request.reason = "failure-" + std::to_string(i);
            output["failures"].push_back(store.save_diagnostic(&frame, request));
        }
    } else {
        for (int i = 0; i < 70; ++i) {
            request.operation_id = "fish-" + std::to_string(i);
            require(store.save_diagnostic(&frame, request).at("status") == "saved", "SEVENTY_REWARDS_NOT_SAVED");
        }
        output["duplicate"] = store.save_diagnostic(&frame, request);
        request.stage = "pre_action";
        request.operation_id.clear();
        request.reason = "../../not-a-path";
        output["initial"] = store.save_diagnostic(&frame, request);
        output["throttled"] = store.save_diagnostic(&frame, request);
        clock->milliseconds = 60000;
        output["sixty"] = store.save_diagnostic(&frame, request);
        request.reason = "pause_candidate_rejected";
        output["pause"] = store.save_diagnostic(&frame, request);
        clock->milliseconds = 179999;
        output["pause_throttled"] = store.save_diagnostic(&frame, request);
        clock->milliseconds = 180000;
        output["pause_boundary"] = store.save_diagnostic(&frame, request);
        request.reason = "wrong-run";
        request.run_id = 2;
        output["wrong_run"] = store.save_diagnostic(&frame, request);
        request.run_id = 1;
        request.reason = "wrong-generation";
        request.generation = 2;
        output["wrong_generation"] = store.save_diagnostic(&frame, request);
        request.generation = 1;
        request.reason = "not-png";
        frame.encoded_image[0] = 0;
        output["not_png"] = store.save_diagnostic(&frame, request);
        frame.encoded_image[0] = 137;
        request.reason = "wrong-png-size";
        frame.identity.recognition_size.width = 901;
        output["wrong_size"] = store.save_diagnostic(&frame, request);
        frame.identity.recognition_size.width = 900;
        request.reason = "oversize-png";
        frame.encoded_image.resize(8 * 1024 * 1024 + 1);
        output["oversize"] = store.save_diagnostic(&frame, request);
        request.reason = "pause_candidate_rejected";
        clock->milliseconds = 179999;
        output["clock_backwards"] = store.save_diagnostic(&frame, request);
    }
    contracts::RunSnapshot snapshot;
    snapshot.run_id = 1;
    snapshot.generation = 1;
    snapshot.state = contracts::RunState::Interrupted;
    snapshot.reason = "ORIGINAL_BUSINESS_REASON";
    snapshot.quiescent = true;
    contracts::SessionResult session;
    store.save_terminal(snapshot, session);
    output["saved"] = read(store.directory() / "result.json");
    output["late"] = store.save_diagnostic(&frame, request);
    output["directory"] = maafw::utf8(store.directory());
    output["native_run_executed"] = false; // 存储+真实SDK PNG解码，不冒充70次完整钓鱼。
    return output;
}
J runtime_case(const J &config) {
    const auto mode = config.at("case").get<std::string>();
    const auto root = maafw::path_from_utf8(config.at("run_root"));
    auto registry = std::make_shared<runtime::BehaviorRegistry>("diagnostic-fixture-1");
    games::vision::register_wvd(*registry);
    games::register_wvd_state(*registry);
    games::register_wvd_confirmations(*registry);
    registry->seal();
    auto device = std::make_shared<Device>();
    device->identity = "diagnostic-offline";
    device->before = bytes(maafw::path_from_utf8(config.at("before")));
    device->after = bytes(maafw::path_from_utf8(config.at("after")));
    if (mode == "recovery-capture-fail") device->fail_capture = 2;
    maafw::Bundle bundle{maafw::path_from_utf8(config.at("bundle")), "diagnostic-fixture", {}};
    for (const auto &file : config.at("files")) bundle.files.push_back({file.at("path"), file.at("sha256")});
    runtime::RunDefinition definition;
    definition.request_id = "diagnostic-source";
    definition.initial = {bundle, config.value("entry", "Entry"), "Terminal", {}, 90000ms, 200ms};
    if (mode == "reward") {
        definition.initial = games::tasks::publish_workflow(reward_workflow(), bundle, *registry, root.parent_path() / "compiled");
        storage::LegacyConfigImporter importer(read(maafw::path_from_utf8(config.at("descriptor"))));
        auto profile = importer.parse({{"GENERAL", {{"FARM_TARGET", "fishing"}}}});
        definition.state_factory = games::wvd_state_binding(profile.values);
    }
    definition.policy = {device->identity, "wvd", device->application, definition.initial.bundle.revision,
        device->viewport, {900, 1600}, {contracts::ActionKind::Click}, {contracts::ActionKind::Click}, {"wvd"}, 2000ms};
    runtime::RunCoordinator coordinator(root, registry);
    coordinator.start(definition, device);
    require(coordinator.wait_for(120000ms), "DIAGNOSTIC_RUNTIME_WATCHDOG_FAILED");
    return {{"saved", read(coordinator.run_directory() / "result.json")},
        {"directory", maafw::utf8(coordinator.run_directory())}, {"captures", device->captures.load()},
        {"inputs", device->calls.load()}, {"native_run_executed", true}};
}
}
int main(int argc, char **argv) {
    try {
        require(argc == 2, "CONFIG_REQUIRED");
        const auto config = read(maafw::path_from_utf8(argv[1]));
        const auto mode = config.at("case").get<std::string>();
        auto output = mode == "describe" ? J{{"images", reward_workflow().images}} :
            (mode.starts_with("store-") ? store_case(config) : runtime_case(config));
        output["loaded_modules"] = loaded_vision_modules();
        std::ofstream(maafw::path_from_utf8(config.at("output"))) << output.dump(2);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
