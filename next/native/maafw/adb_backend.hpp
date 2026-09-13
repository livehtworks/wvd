#pragma once
#include "buffers.hpp"
#include "devices/backend.hpp"
#include "devices/screenshot_route.hpp"
#include <json.hpp>

namespace wvd::maafw {
// 只管理内层 Controller。Tasker/Resource 仍由外层唯一 Gateway 持有。
class AdbBackend final : public devices::DeviceBackend {
  public:
    explicit AdbBackend(const std::filesystem::path &verified_binding, bool encode_only = false);
    ~AdbBackend();
    bool offline() const override {
        return false;
    }
    bool verified_access() const override {
        return verified_;
    }
    bool connect() override;
    void disconnect() override;
    devices::RawFrame capture() override;
    bool execute(const contracts::Command &command) override;
    bool context_matches(const contracts::FrameIdentity &, const std::string &) override;
    nlohmann::json diagnostics() const {
        return {{"connections", diagnostics_}, {"failures", route_.failures()}};
    }

  private:
    bool open(bool encode);
    bool wait(MaaCtrlId id);
    std::string foreground();
    devices::RawFrame capture_current();
    nlohmann::json binding_, diagnostics_ = nlohmann::json::array();
    bool verified_{}, encode_only_{}, encode_{};
    devices::ScreenshotRoute route_;
    std::uint64_t connection_generation_{};
    Handle<MaaController, MaaControllerDestroy> controller_{nullptr, MaaControllerDestroy};
};
} // namespace wvd::maafw
