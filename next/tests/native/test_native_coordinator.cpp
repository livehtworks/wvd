#include "runtime/native_run_coordinator.hpp"
#include "platform/windows/file_digest.hpp"
#include <atomic>
#include <fstream>
#include <iostream>
#include <thread>

namespace {
using namespace wvd;
using namespace std::chrono_literals;
class State final : public contracts::BusinessRunState {
  protected:
    void on_segment(contracts::SegmentBoundary, std::uint64_t, std::size_t) override {}
    nlohmann::json summarize() const override { return {{"confirmed", true}}; }
};
class Backend : public devices::DeviceBackend {
  public:
    explicit Backend(bool release_ok = true) : release_ok_(release_ok) {}
    bool offline() const override { return true; }
    bool verified_access() const override { return true; }
    bool connect() override { return true; }
    devices::RawFrame capture() override {
        devices::RawFrame result;
        result.raw_bgr = std::make_shared<const std::vector<std::uint8_t>>(
            900 * 1600 * 3, 0);
        result.size = {900, 1600};
        result.device_id = "native-test";
        result.viewport_id = "900x1600";
        result.foreground_application = "jp.co.drecom.wizardry.daphne";
        result.captured_at = std::chrono::steady_clock::now();
        result.capture_finished_at = result.captured_at;
        result.connection_generation = 1;
        result.display_rotation = 0;
        return result;
    }
    bool execute(const contracts::Command &) override {
        throw std::runtime_error("UNEXPECTED_TEST_INPUT");
    }
    bool release_owned_inputs() override { return release_ok_; }
  private:
    bool release_ok_;
};
class BlockingBackend final : public Backend {
  public:
    std::atomic<bool> entered{false};
    devices::RawFrame capture(std::stop_token stop) override {
        entered = true;
        while (!stop.stop_requested()) std::this_thread::sleep_for(5ms);
        throw std::runtime_error("CAPTURE_CANCELLED");
    }
};
}

int main() {
    try {
        const auto data_root = std::filesystem::temp_directory_path() /
            ("wvd-native-coordinator-" + wvd::platform::unique_id());
        std::filesystem::create_directories(data_root);
        const auto bundle_root = data_root / "bundle";
        std::filesystem::create_directories(bundle_root);
        { std::ofstream marker(bundle_root / "marker.txt", std::ios::binary);
          marker << "native bundle"; }
        recognition::Bundle bundle{bundle_root, "native-test",
            {{"marker.txt", platform::file_sha256(bundle_root / "marker.txt")}}};
        workflow::FlowProgram program;
        program.revision = "native-test";
        program.root_definition = "root";
        workflow::Definition root;
        root.id = "root";
        root.entry = "checkpoint";
        workflow::Step checkpoint;
        checkpoint.id = "checkpoint";
        checkpoint.source_path = "test/checkpoint";
        checkpoint.data = workflow::RegisteredOperation{"BusinessCheckpoint",
            nlohmann::json::object()};
        checkpoint.next = {"finish"};
        root.steps.emplace("checkpoint", std::move(checkpoint));
        workflow::Step finish;
        finish.id = "finish";
        finish.source_path = "test/finish";
        finish.data = workflow::Finish{};
        root.steps.emplace("finish", std::move(finish));
        program.definitions.emplace("root", std::move(root));
        runtime::NativeRunDefinition definition;
        definition.request_id = "native-coordinator-test";
        definition.policy.device_id = "native-test";
        definition.policy.game_id = "wvd";
        definition.policy.application_id = "jp.co.drecom.wizardry.daphne";
        definition.policy.pack_revision = "native-test";
        definition.policy.viewport_id = "900x1600";
        definition.policy.recognition_size = {900, 1600};
        definition.policy.allowed_scenes.insert("wvd");
        definition.units.push_back({std::make_shared<const workflow::FlowProgram>(std::move(program)), std::move(bundle), {},
            "test/checkpoint", 5s});
        definition.total_time_limit = 5s;
        definition.create_state = [](const auto &) { return std::make_unique<State>(); };
        definition.operations = [](auto &, auto, auto checkpoint_callback) {
            return [checkpoint_callback](runtime::NativeFlowPorts &) {
                return [checkpoint_callback](const std::string &binding, const auto &,
                    const auto &, const auto &, const std::string &source_path) {
                    if (binding != "BusinessCheckpoint")
                        return runtime::OperationResult{runtime::OperationState::Failed,
                            "UNEXPECTED_OPERATION"};
                    checkpoint_callback(source_path);
                    return runtime::OperationResult{runtime::OperationState::Done};
                };
            };
        };
        runtime::NativeRunCoordinator coordinator(data_root / "runs");
        auto backend = std::make_shared<Backend>();
        const auto fixture = [&] {
            runtime::NativeRunDefinition value;
            value.policy = definition.policy;
            value.units = definition.units; // 不可变程序共享；修改场景另行显式封存。
            value.total_time_limit = definition.total_time_limit;
            value.create_state = definition.create_state;
            value.operations = definition.operations;
            return value;
        };
        auto pending_definition = fixture();
        auto deferred_definition = fixture();
        auto capture_definition = fixture();
        pending_definition.request_id = "native-cleanup-pending";
        const auto started = coordinator.start(std::move(definition), backend);
        if (!started.run_id || !coordinator.wait_for(5s))
            throw std::runtime_error("COORDINATOR_NOT_FINISHED");
        const auto ended = coordinator.snapshot();
        if (ended.state != contracts::RunState::Completed || !ended.quiescent ||
            !ended.result_saved || ended.completed_business_units != 1)
            throw std::runtime_error("COORDINATOR_TERMINAL_INVALID:" + ended.reason +
                ":" + ended.storage_error);
        {
            runtime::NativeRunCoordinator pending(data_root / "pending-runs");
            auto unreleased = std::make_shared<Backend>(false);
            pending.start(std::move(pending_definition), unreleased);
            if (!pending.wait_for_worker(5s) || pending.wait_for(0ms))
                throw std::runtime_error("CLEANUP_PENDING_WAIT_SEMANTICS_INVALID");
            const auto state = pending.snapshot();
            if (state.state != contracts::RunState::Interrupted || state.quiescent)
                throw std::runtime_error("CLEANUP_PENDING_TERMINAL_INVALID");
        }
        {
            auto adjusted = std::make_shared<workflow::FlowProgram>(*deferred_definition.units.front().program);
            auto &root = adjusted->definitions.at("root");
            root.steps.at("checkpoint").data = workflow::Fail{"deferred.recovery"};
            deferred_definition.units.front().program = std::move(adjusted);
            deferred_definition.request_id = "native-deferred-stop";
            deferred_definition.total_time_limit = 10s;
            deferred_definition.recovery = [](const contracts::SessionResult &result,
                const contracts::BusinessRunState &, unsigned attempt)
                -> std::optional<devices::LifecyclePlan> {
                if (result.reason != "deferred.recovery" || attempt != 1)
                    throw std::runtime_error("DEFERRED_RECOVERY_NOT_REACHED");
                devices::LifecyclePlan plan;
                plan.target = {"native-test", "native-instance",
                    "jp.co.drecom.wizardry.daphne", "", false};
                plan.operations = {devices::LifecycleOperation::StopApplication,
                    devices::LifecycleOperation::StartApplication};
                plan.attempt = 1;
                plan.defer_for = 5s;
                return plan;
            };
            runtime::NativeRunCoordinator deferred(data_root / "deferred-runs");
            deferred.start(std::move(deferred_definition), backend);
            const auto deadline = std::chrono::steady_clock::now() + 2s;
            bool waiting = false;
            while (std::chrono::steady_clock::now() < deadline) {
                for (const auto &entry : deferred.events().value("events", nlohmann::json::array()))
                    if (entry.value("type", "") == "recovery.deferred") waiting = true;
                if (waiting) break;
                std::this_thread::sleep_for(10ms);
            }
            if (!waiting) throw std::runtime_error("DEFERRED_RECOVERY_NOT_WAITING");
            const auto stop_started = std::chrono::steady_clock::now();
            deferred.request_stop();
            if (!deferred.wait_for(500ms) ||
                std::chrono::steady_clock::now() - stop_started > 500ms ||
                deferred.snapshot().state != contracts::RunState::UserStopped)
                throw std::runtime_error("DEFERRED_RECOVERY_STOP_FAILED");
        }
        {
            auto adjusted = std::make_shared<workflow::FlowProgram>(*capture_definition.units.front().program);
            auto &root = adjusted->definitions.at("root");
            root.steps.at("checkpoint").guard = recognition::Request{
                "capture-check", "1", {0, 0, 900, 1600},
                recognition::CustomParameters{"WvdVision", nlohmann::json::object()}};
            capture_definition.units.front().program = std::move(adjusted);
            capture_definition.request_id = "native-capture-stop";
            runtime::NativeRunCoordinator capture(data_root / "capture-runs");
            auto blocked = std::make_shared<BlockingBackend>();
            capture.start(std::move(capture_definition), blocked);
            const auto deadline = std::chrono::steady_clock::now() + 2s;
            while (!blocked->entered && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(5ms);
            if (!blocked->entered) throw std::runtime_error("CAPTURE_NOT_ENTERED");
            const auto stop_started = std::chrono::steady_clock::now();
            capture.request_stop();
            if (!capture.wait_for(500ms) ||
                std::chrono::steady_clock::now() - stop_started > 500ms ||
                capture.snapshot().state != contracts::RunState::UserStopped ||
                capture.snapshot().inputs.backend_called != 0)
                throw std::runtime_error("CAPTURE_STOP_FAILED");
        }
        std::cout << "native terminal storage, cleanup pending, deferred and capture stop passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
