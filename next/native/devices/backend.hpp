#pragma once
#include "contracts/action.hpp"
#include "lifecycle.hpp"
#include <filesystem>
#include <json.hpp>
#include <memory>
#include <stop_token>

namespace wvd::devices {
struct RawFrame {
    std::vector<std::uint8_t> encoded;
    contracts::Size size;
    std::string device_id, viewport_id, foreground_application;
    std::chrono::steady_clock::time_point captured_at{};
    std::string backend;
    std::uint64_t connection_generation{};
    std::chrono::steady_clock::time_point capture_finished_at{};
    int display_rotation{-1};
    std::shared_ptr<const std::vector<std::uint8_t>> raw_bgr;
};
// 内层始终使用原始设备坐标；控制权由一个会话独占。
class DeviceBackend {
  public:
    virtual ~DeviceBackend() = default;
    virtual bool offline() const = 0;
    virtual bool verified_access() const {
        return false;
    }
    virtual void disconnect() {}
    virtual bool release_owned_inputs() { return true; }
    virtual LifecyclePort *lifecycle_port() { return nullptr; }
    virtual bool context_matches(const contracts::FrameIdentity &, const std::string &) {
        return offline();
    }
    virtual bool context_matches(const contracts::FrameIdentity &identity,
                                 const std::string &application, std::stop_token stop) {
        return !stop.stop_requested() && context_matches(identity, application);
    }
    // 控制握手必须早于用于点击的截图；execute 不允许透明重连后使用旧坐标。
    virtual void prepare_input_channel(std::stop_token stop) {
        if (stop.stop_requested()) throw std::runtime_error("INPUT_PREPARATION_CANCELLED");
        if (!offline()) throw std::runtime_error("INPUT_PREPARATION_UNIMPLEMENTED");
    }
    virtual bool connect() = 0;
    virtual RawFrame capture() = 0;
    virtual RawFrame capture(std::stop_token stop) {
        if (stop.stop_requested()) throw std::runtime_error("CAPTURE_CANCELLED");
        return capture();
    }
    virtual RawFrame capture_preview() { return capture(); }
    virtual bool execute(const contracts::Command &command) = 0;
    virtual bool execute(const contracts::Command &command, std::stop_token stop) {
        return !stop.stop_requested() && execute(command);
    }
};

// 产品装配只依赖会话能力；离线验收也必须走同一个 Application 入口。
class DeviceConnection : public DeviceBackend {
  public:
    virtual LifecycleTarget lifecycle_target() const = 0;
    virtual bool matches_selection(const std::filesystem::path &manager, int index,
                                   const std::string &serial) const = 0;
    virtual void set_vpn_required(bool value) = 0;
    virtual nlohmann::json diagnostics() const = 0;
};
} // namespace wvd::devices
