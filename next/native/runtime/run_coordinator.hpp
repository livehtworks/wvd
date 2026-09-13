#pragma once
#include "execution_session.hpp"
#include "platform/windows/runtime_files.hpp"

namespace wvd::runtime {
struct RunDefinition {
    std::string request_id;
    contracts::InputPolicy policy;
    SessionDefinition initial;
    std::size_t recovery_limit{};
    std::function<std::optional<SessionDefinition>(const contracts::SessionResult &)> recover;
};
// 一个用户运行的唯一所有者。监督线程不做原生阻塞调用；Session 工作线程独占 SDK 对象。
// STOP_TIMEOUT 只改变可观察故障状态，不能提前 join、释放设备租约或接受另一运行。
class RunCoordinator {
  public:
    explicit RunCoordinator(std::filesystem::path data_root);
    ~RunCoordinator();
    contracts::RunSnapshot start(RunDefinition definition,
                                 std::shared_ptr<devices::DeviceBackend> backend);
    void request_stop();
    contracts::RunSnapshot snapshot() const;
    bool wait_for(std::chrono::milliseconds duration);
    nlohmann::json events(std::uint64_t after = 0) const;
    std::filesystem::path run_directory() const;

  private:
    void drive(RunDefinition definition, std::shared_ptr<devices::DeviceBackend> backend) noexcept;
    static void validate(const RunDefinition &definition, const devices::DeviceBackend &backend);
    const std::filesystem::path data_root_;
    const std::string instance_id_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    contracts::RunSnapshot snapshot_;
    std::string last_request_;
    struct RequestRecord {
        nlohmann::json definition;
        contracts::RunSnapshot result;
    };
    std::map<std::string, RequestRecord> requests_;
    std::atomic<bool> stop_{false};
    bool active_{};
    std::optional<std::chrono::steady_clock::time_point> stopped_at_;
    std::unique_ptr<platform::DeviceLease> lease_;
    std::shared_ptr<ExecutionSession> session_;
    std::unique_ptr<storage::EventJournal> journal_;
    std::unique_ptr<storage::RunStore> store_;
    std::thread supervisor_;
};
} // namespace wvd::runtime
