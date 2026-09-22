#pragma once
#include "buffers.hpp"
#include "devices/android_probe.hpp"
#include <mutex>
#include <atomic>
#include "devices/backend.hpp"
#include "devices/screenshot_route.hpp"
#include <json.hpp>
#include <stop_token>

namespace wvd::maafw {
// 只管理内层 Controller。Tasker/Resource 仍由外层唯一 Gateway 持有。
class AdbBackend final : public devices::DeviceBackend, public devices::LifecyclePort {
  public:
    explicit AdbBackend(const std::filesystem::path &verified_binding, bool encode_only = false,
                        std::stop_token discovery_cancellation = {});
    explicit AdbBackend(nlohmann::json verified_binding, bool encode_only = false,
                        std::stop_token discovery_cancellation = {});
    ~AdbBackend();
    bool offline() const override { return false; }
    bool verified_access() const override { return verified_; }
    bool connect() override;
    void disconnect() override;
    devices::RawFrame capture() override;
    bool execute(const contracts::Command &command) override;
    bool context_matches(const contracts::FrameIdentity &, const std::string &) override;
    devices::LifecyclePort *lifecycle_port() override { return this; }
    std::optional<devices::LifecycleObservation> observe_lifecycle() override;
    bool execute_lifecycle(devices::LifecycleOperation operation,
                           const devices::LifecycleTarget &target,
                           const std::function<bool()> &cancelled) override;
    nlohmann::json diagnostics() const;
    // 仅在 Application 已排除活动 Run/设备作业后，从已保存的冻结配置更新。
    void set_vpn_required(bool required);
    devices::LifecycleTarget lifecycle_target() const;

  private:
    void record(nlohmann::json value);
    devices::android::ShellReply probe_reply(const std::string &command, int timeout = 5000);
    bool vpn_ui_step(const std::string &package, bool &start_clicked,
                     const std::function<bool()> &cancelled);
    bool open(bool encode);
    bool wait(MaaCtrlId id);
    std::string foreground_probe();
    std::string foreground();
    std::string shell(const std::string &command, int timeout = 5000);
    std::string shell_probe(const std::string &command, int timeout = 5000);
    bool start_package(const std::string &package);
    bool vpn_connected();
    std::string clash_package();
    bool launch_instance(const std::function<bool()> &cancelled);
    bool restart_instance(const std::function<bool()> &cancelled);
    devices::RawFrame capture_current();
    mutable std::mutex diagnostics_mutex_;
    nlohmann::json binding_, diagnostics_ = nlohmann::json::array();
    nlohmann::json route_failures_ = nlohmann::json::array();
    bool verified_{}, encode_only_{}, encode_{};
    devices::ScreenshotRoute route_;
    std::uint64_t connection_generation_{};
    Handle<MaaController, MaaControllerDestroy> controller_{nullptr, MaaControllerDestroy};
};
} // namespace wvd::maafw
