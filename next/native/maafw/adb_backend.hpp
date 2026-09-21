#pragma once
#include "buffers.hpp"
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
    nlohmann::json diagnostics() const {
        return {{"connections", diagnostics_}, {"failures", route_.failures()}};
    }
    devices::LifecycleTarget lifecycle_target() const;

  private:
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
    nlohmann::json binding_, diagnostics_ = nlohmann::json::array();
    bool verified_{}, encode_only_{}, encode_{};
    devices::ScreenshotRoute route_;
    std::uint64_t connection_generation_{};
    Handle<MaaController, MaaControllerDestroy> controller_{nullptr, MaaControllerDestroy};
};
} // namespace wvd::maafw
