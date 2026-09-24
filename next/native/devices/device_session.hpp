#pragma once
#include "adb_command.hpp"
#include "backend.hpp"
#include "scrcpy_control.hpp"
#include "mumu_capture.hpp"
#include <json.hpp>
#include <memory>
#include <mutex>
#include <atomic>

namespace wvd::devices {
class DeviceSession final : public DeviceConnection, public LifecyclePort {
  public:
    DeviceSession(nlohmann::json binding, std::filesystem::path capture_host,
                  std::filesystem::path scrcpy_server,
                  std::stop_token cancellation = {});
    ~DeviceSession() noexcept override;
    bool offline() const override { return false; }
    bool verified_access() const override { return verified_; }
    bool connect() override;
    void prepare_input_channel(std::stop_token stop) override;
    void disconnect() override;
    bool release_owned_inputs() override;
    RawFrame capture() override;
    RawFrame capture(std::stop_token stop) override;
    RawFrame capture_preview() override;
    bool execute(const contracts::Command &command) override;
    bool execute(const contracts::Command &command, std::stop_token stop) override;
    bool context_matches(const contracts::FrameIdentity &identity,
                         const std::string &application) override;
    bool context_matches(const contracts::FrameIdentity &identity,
                         const std::string &application, std::stop_token stop) override;
    LifecyclePort *lifecycle_port() override { return this; }
    std::optional<LifecycleObservation> observe_lifecycle() override;
    bool execute_lifecycle(LifecycleOperation operation, const LifecycleTarget &target,
                           const std::function<bool()> &cancelled) override;
    LifecycleTarget lifecycle_target() const;
    bool matches_selection(const std::filesystem::path &manager, int index,
                           const std::string &serial) const;
    void set_vpn_required(bool value);
    nlohmann::json diagnostics() const;

  private:
    RawFrame capture_impl(bool preview, std::stop_token stop = {});
    std::string foreground(std::stop_token stop = {});
    android::ShellReply query(const std::string &command, int timeout = 5000,
                              std::stop_token stop = {});
    bool vpn_connected();
    bool start_package(const std::string &package);
    std::string clash_package();
    bool vpn_ui_step(const std::string &package, bool &start_clicked,
                     const std::function<bool()> &cancelled);
    void record(nlohmann::json item);
    nlohmann::json binding_;
    std::filesystem::path helper_path_, server_path_;
    AdbCommandClient adb_;
    std::unique_ptr<MumuCaptureClient> capture_host_;
    std::unique_ptr<ScrcpyControlClient> control_;
    mutable std::mutex mutex_;
    nlohmann::json diagnostics_ = nlohmann::json::array();
    // 状态页只读取这些标量；设备操作仍由唯一会话线程执行。
    std::atomic<std::uint64_t> generation_{};
    bool verified_{};
    std::atomic<bool> connected_{false}, fast_failed_{false};
    contracts::Size latest_size_{};
    int latest_rotation_{-1};
    std::string latest_foreground_;
    std::chrono::steady_clock::time_point metadata_at_{};
};
} // namespace wvd::devices
