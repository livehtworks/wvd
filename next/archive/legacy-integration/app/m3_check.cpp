#include "maafw/adb_backend.hpp"
#include "platform/windows/file_digest.hpp"
#include "runtime/run_coordinator.hpp"
#include <fstream>
#include <iostream>

#include <windows.h>

namespace {
using namespace wvd;
using J = nlohmann::json;
using namespace std::chrono_literals;
std::atomic<bool> stopping{false};
std::stop_source discovery_stop;
BOOL WINAPI interrupt(DWORD signal) {
    if (signal != CTRL_C_EVENT && signal != CTRL_BREAK_EVENT)
        return FALSE;
    stopping = true;
    discovery_stop.request_stop();
    return TRUE;
}
void save(const std::filesystem::path &path, const J &value) {
    std::ofstream file(path);
    file << value.dump(2);
    file.close();
    if (!file)
        throw std::runtime_error("EVIDENCE_WRITE_FAILED");
}
// 有限诊断动作只取帧；与业务动作共用 Context、门禁、会话和终态所有者。
// 不创建第二个 Tasker，不申请输入许可，也不在此处理游戏恢复。
bool capture_batch(maafw::Context &context, const J &, const J &parameters) {
    const auto output = maafw::path_from_utf8(parameters.at("output"));
    J captures = J::array();
    const int warmup = parameters.at("warmup"), formal = parameters.at("formal");
    if (warmup < 0 || warmup > 5 || formal < 1 || formal > 50)
        throw std::runtime_error("CAPTURE_BUDGET_INVALID");
    for (int attempt = -warmup; attempt < formal; ++attempt) {
        if (context.cancelled())
            return false;
        const auto begin = std::chrono::steady_clock::now();
        J item{{"attempt", attempt}, {"warmup", attempt < 0}};
        try {
            auto frame = context.capture();
            const auto end = std::chrono::steady_clock::now();
            const auto &identity = frame.identity;
            const auto relative = std::to_string(attempt) + ".png";
            std::ofstream image(output / relative, std::ios::binary);
            image.write(reinterpret_cast<const char *>(frame.encoded_image.data()),
                        frame.encoded_image.size());
            image.close();
            if (!image)
                throw std::runtime_error("IMAGE_WRITE_FAILED");
            item.update({{"outcome", "PASS"},
                         {"backend", identity.backend},
                         {"width", identity.raw_size.width},
                         {"height", identity.raw_size.height},
                         {"application", identity.foreground_application},
                         {"generation", identity.generation},
                         {"connection_generation", identity.connection_generation},
                         {"frame_id", identity.frame_id},
                         {"captured_monotonic_ns", identity.captured_at.time_since_epoch().count()},
                         {"capture_and_metadata_ms",
                          std::chrono::duration<double, std::milli>(end - begin).count()},
                         {"image", relative},
                         {"image_sha256", platform::file_sha256(output / relative)}});
        } catch (const std::exception &error) {
            item.update({{"outcome", "FAIL"}, {"error", error.what()}});
            captures.push_back(item);
            save(output / "captures.json", captures);
            // 失败后旧帧已失效，交回唯一协调器结束；不继续凑足成功样本。
            throw;
        }
        captures.push_back(item);
        save(output / "captures.json", captures);
    }
    return true;
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc != 5 || std::string(argv[1]) != "--binding" ||
            std::string(argv[3]) != "--capture-only")
            throw std::runtime_error(
                "usage: wvd_m3_check --binding PRIVATE.json --capture-only NEW_OUTPUT_DIRECTORY");
        SetConsoleCtrlHandler(interrupt, TRUE);
        const auto output = maafw::path_from_utf8(argv[4]);
        if (std::filesystem::exists(output))
            throw std::runtime_error("OUTPUT_MUST_BE_NEW");
        std::filesystem::create_directories(output / "bundle/pipeline");
        std::ifstream input(maafw::path_from_utf8(argv[2]));
        J binding;
        input >> binding;
        const auto pipeline = output / "bundle/pipeline/capture.json";
        save(pipeline, {{"Capture",
                         {{"recognition", "DirectHit"},
                          {"action", "Custom"},
                          {"custom_action", "CaptureBatch"},
                          {"next", {"Terminal"}}}},
                        {"Terminal",
                         {{"recognition", "DirectHit"},
                          {"action", "Custom"},
                          {"custom_action", "RootTerminal"}}}});
        const auto hash = platform::file_sha256(pipeline);
        maafw::Bundle bundle{output / "bundle", hash, {{"pipeline/capture.json", hash}}};
        auto registry = std::make_shared<runtime::BehaviorRegistry>("m3-read-only-check-1");
        registry->add_action({"m3.capture-batch", "1"}, capture_batch);
        registry->seal();
        runtime::RunCoordinator coordinator(output / "run-data", registry, 1024);
        J result{{"schema", 1},
                 {"sdk", MaaVersion()},
                 {"scope", "M3_CAPTURE_ONLY"},
                 {"input_permissions", J::array()},
                 {"groups", J::array()}};
        // 同一后端对象跨有限会话重建自己的连接；会话静止后才能进入下一组。
        auto ipc = std::make_shared<maafw::AdbBackend>(maafw::path_from_utf8(argv[2]), false,
                                                       discovery_stop.get_token());
        for (int group = 0; group < 3; ++group) {
            const std::string name = group == 0 ? "mumu" : group == 1 ? "encode" : "reconnect";
            const auto folder = output / name;
            std::filesystem::create_directories(folder);
            auto backend =
                group == 1 ? std::make_shared<maafw::AdbBackend>(maafw::path_from_utf8(argv[2]),
                                                                 true, discovery_stop.get_token())
                           : ipc;
            runtime::RunDefinition definition;
            definition.request_id = name;
            // 独立系统只读 viewport 使用每帧实测尺寸，绝不转置成 WVD 画面。
            // 此模式禁止所有输入策略，后续游戏运行仍使用固定的 9:16 契约。
            definition.policy = {binding.at("serial"),
                                 "wvd",
                                 "jp.co.drecom.wizardry.daphne",
                                 bundle.revision,
                                 "900x1600",
                                 {900, 1600},
                                 {},
                                 {},
                                 {},
                                 5000ms};
            definition.policy.observed_read_only_viewport = true;
            definition.initial = {bundle, "Capture", "Terminal", {}, 180000ms, 3000ms};
            definition.initial.actions = {{"CaptureBatch",
                                           {"m3.capture-batch", "1"},
                                           {{"output", maafw::utf8(folder)},
                                            {"warmup", group == 2 ? 0 : 5},
                                            {"formal", group == 2 ? 2 : 50}}}};
            coordinator.start(std::move(definition), backend);
            while (!coordinator.wait_for(50ms)) {
                if (stopping)
                    coordinator.request_stop();
                save(output / "live-status.json", storage::snapshot_json(coordinator.snapshot()));
            }
            const auto snapshot = coordinator.snapshot();
            result["groups"].push_back({{"requested", name},
                                        {"run", storage::snapshot_json(snapshot)},
                                        {"diagnostics", backend->diagnostics()}});
            save(output / "result.json", result);
            if (snapshot.state != contracts::RunState::Completed || !snapshot.quiescent ||
                !snapshot.result_saved || snapshot.inputs.backend_called)
                throw std::runtime_error("CAPTURE_SESSION_NOT_COMPLETED: " + snapshot.reason);
        }
        result["safe_system_navigation"] = {{"outcome", "BLOCKED"},
                                            {"reason", "NO_CONFIRMED_REVERSIBLE_SYSTEM_SCENE"}};
        result["quiescent"] = true;
        save(output / "result.json", result);
        std::cout << "M3 capture finished; no game input issued.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
