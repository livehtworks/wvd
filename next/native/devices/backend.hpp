#pragma once
#include "contracts/action.hpp"
#include "lifecycle.hpp"

namespace wvd::devices {
struct RawFrame {
    std::vector<std::uint8_t> encoded;
    contracts::Size size;
    std::string device_id, viewport_id, foreground_application;
    std::chrono::steady_clock::time_point captured_at{};
    std::string backend;
    std::uint64_t connection_generation{};
};
// 内层始终使用原始设备坐标。M2 的具体设备只存在于 tests，真实适配在 M3。
class DeviceBackend {
  public:
    virtual ~DeviceBackend() = default;
    virtual bool offline() const = 0;
    virtual bool verified_access() const {
        return false;
    }
    virtual void disconnect() {}
    virtual LifecyclePort *lifecycle_port() { return nullptr; }
    virtual bool context_matches(const contracts::FrameIdentity &, const std::string &) {
        return offline();
    }
    virtual bool connect() = 0;
    virtual RawFrame capture() = 0;
    virtual bool execute(const contracts::Command &command) = 0;
};
} // namespace wvd::devices
