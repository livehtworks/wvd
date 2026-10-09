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
    // 相同发布图只持有一份；各 Session 的游标、次数和事件栈仍完全独立。
    std::shared_ptr<const workflow::FlowProgram> program;
    recognition::Bundle bundle;
    recognition::Handlers recognizers;
    std::string checkpoint_source_path;
    std::chrono::milliseconds time_limit{60000};
};

struct NativeRunDefinition {
    NativeRunDefinition() = default;
    NativeRunDefinition(const NativeRunDefinition &) = delete;
    NativeRunDefinition &operator=(const NativeRunDefinition &) = delete;
    NativeRunDefinition(NativeRunDefinition &&) = default;
    NativeRunDefinition &operator=(NativeRunDefinition &&) = default;
    std::string request_id;
    nlohmann::json handoff_parent = nullptr;
    contracts::InputPolicy policy;
    std::vector<NativeUnit> units;
    std::shared_ptr<recognition::MatchBudget> match_budget;
    storage::LoggingPolicy logging;
    nlohmann::json preparation = nlohmann::json::object();
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
    struct ReadView {
        contracts::RunSnapshot snapshot;
        std::string instance_id, request_id;
        std::shared_ptr<storage::EventJournal> journal;
        std::shared_ptr<storage::RunStore> store;
        bool worker_joined{}, heap_maintenance_complete{}, heap_maintenance_succeeded{};
    };
    explicit NativeRunCoordinator(std::filesystem::path data_root,
                                  std::size_t event_capacity = 256, std::string instance_id = {});
    ~NativeRunCoordinator();
    contracts::RunSnapshot start(NativeRunDefinition definition,
                                 std::shared_ptr<devices::DeviceBackend> backend);
    void request_stop();
    bool owns_request(const std::string &request_id) const;
    contracts::RunSnapshot snapshot() const;
    ReadView read_view() const;
    std::optional<contracts::RunSnapshot> request_snapshot(const std::string &request_id) const;
    bool wait_for(std::chrono::milliseconds duration);
    bool wait_for_worker(std::chrono::milliseconds duration);
    bool collect_finished_worker();
    void record_batch_release();
    void record_preparation(const nlohmann::json &metrics);
    nlohmann::json events(std::uint64_t after = 0) const;
    nlohmann::json diagnostics() const;
    std::filesystem::path run_directory() const;

  private:
    friend struct NativeCoordinatorTestAccess;
    // 每个Session只保留当前动作的小型元数据；不持有历史帧或业务对象。
    struct CombatDiagnosticState {
        nlohmann::json selection = nlohmann::json::object();
        nlohmann::json submission = nullptr;
        nlohmann::json opening = nullptr;
    };
    void record_combat_diagnostic(std::uint64_t generation, std::size_t index,
        const std::string &type, const nlohmann::json &data,
        const contracts::FrameEnvelope *frame, CombatDiagnosticState &state) noexcept;
    void save_application_restart_diagnostic(std::uint64_t generation, std::size_t index,
        const nlohmann::json &progress, const contracts::FrameEnvelope *frame) noexcept;
    // definition 由线程闭包拥有，drive 只借用；严禁在线程入口再复制整图。
    void drive(const NativeRunDefinition &definition,
               const std::shared_ptr<devices::DeviceBackend> &backend);
    void worker_failed(const std::shared_ptr<devices::DeviceBackend> &backend) noexcept;
    void join_worker();
    void record_memory_boundary(const char *phase) noexcept;
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
    bool worker_joined_{}, heap_maintenance_complete_{}, heap_maintenance_succeeded_{};
    contracts::RunSnapshot snapshot_;
    std::string request_id_;
    std::shared_ptr<NativeExecutionSession> session_;
    std::unique_ptr<platform::DeviceLease> lease_;
    std::shared_ptr<storage::EventJournal> journal_;
    std::shared_ptr<storage::RunStore> store_;
};
} // namespace wvd::runtime
