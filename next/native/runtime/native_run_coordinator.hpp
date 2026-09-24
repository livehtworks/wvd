#pragma once

#include "contracts/business_state.hpp"
#include "contracts/run.hpp"
#include "devices/backend.hpp"
#include "platform/windows/runtime_files.hpp"
#include "recognition/service.hpp"
#include "runtime/native_execution_session.hpp"
#include "storage/run_store.hpp"
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace wvd::runtime {
struct NativeUnit {
    workflow::FlowProgram program;
    recognition::Bundle bundle;
    recognition::Handlers recognizers;
    std::string checkpoint_source_path;
    std::chrono::milliseconds time_limit{60000};
};

struct NativeRunDefinition {
    std::string request_id;
    nlohmann::json handoff_parent = nullptr;
    contracts::InputPolicy policy;
    std::vector<NativeUnit> units;
    std::optional<devices::LifecyclePlan> startup;
    std::chrono::milliseconds total_time_limit{std::chrono::minutes{30}};
    std::function<std::unique_ptr<contracts::BusinessRunState>(
        const contracts::StateCreationContext &)> create_state;
    std::function<NativeExecutionSession::OperationFactory(
        contracts::BusinessRunState &,
        std::function<void(const std::string &, const nlohmann::json &)>,
        std::function<void(const std::string &)>)> operations;
    std::function<std::optional<devices::LifecyclePlan>(
        const contracts::SessionResult &, const contracts::BusinessRunState &, unsigned)> recovery;
    std::function<bool(const contracts::SessionResult &,
                       const contracts::BusinessRunState &)> handoff_ready;
};

// 控制面只保护短状态；设备与执行器由单个worker顺序拥有。
class NativeRunCoordinator final {
  public:
    explicit NativeRunCoordinator(std::filesystem::path data_root,
                                  std::size_t event_capacity = 256);
    ~NativeRunCoordinator();
    contracts::RunSnapshot start(NativeRunDefinition definition,
                                 std::shared_ptr<devices::DeviceBackend> backend);
    void request_stop();
    contracts::RunSnapshot snapshot() const;
    std::optional<contracts::RunSnapshot> request_snapshot(const std::string &request_id) const;
    bool wait_for(std::chrono::milliseconds duration);
    bool wait_for_worker(std::chrono::milliseconds duration);
    nlohmann::json events(std::uint64_t after = 0) const;
    nlohmann::json diagnostics() const;
    std::filesystem::path run_directory() const;

  private:
    void drive(NativeRunDefinition definition,
               std::shared_ptr<devices::DeviceBackend> backend) noexcept;
    void publish_state(contracts::RunState state, std::string reason = {});
    const std::filesystem::path data_root_;
    const std::string instance_id_;
    const std::size_t event_capacity_;
    mutable std::mutex mutex_;
    std::mutex start_mutex_;
    std::condition_variable complete_;
    std::jthread worker_;
    std::atomic<bool> stop_{false};
    bool active_{};
    bool execution_finished_{};
    bool terminal_recorded_{true};
    contracts::RunSnapshot snapshot_;
    std::string request_id_;
    std::shared_ptr<NativeExecutionSession> session_;
    std::unique_ptr<platform::DeviceLease> lease_;
    std::shared_ptr<storage::EventJournal> journal_;
    std::unique_ptr<storage::RunStore> store_;
};
} // namespace wvd::runtime
