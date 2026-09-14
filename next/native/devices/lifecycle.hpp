#pragma once
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace wvd::devices {
enum class LifecycleOperation { StopApplication, StartApplication, Reconnect, RestartInstance, EnsureVpn };
struct LifecycleTarget {
    std::string device_id, instance_id, application_id, vpn_application_id;
    bool vpn_required{};
};
struct LifecycleObservation {
    LifecycleTarget target;
    bool instance_running{}, connected{}, application_running{}, vpn_ready{};
    std::uint64_t connection_generation{};
    std::chrono::steady_clock::time_point observed_at{};
    bool application_foreground{};
};
struct LifecyclePlan {
    LifecycleTarget target;
    std::vector<LifecycleOperation> operations;
    unsigned attempt{};
    std::chrono::milliseconds step_timeout{5000};
};
// 由当前后端/Session 借用；不暴露 Shell、坐标或任意包名输入。
// 本阶段真实后端不提供此接口，只有隔离离线设备实现。
class LifecyclePort {
  public:
    virtual ~LifecyclePort() = default;
    virtual std::optional<LifecycleObservation> observe_lifecycle() = 0;
    virtual bool execute_lifecycle(LifecycleOperation operation, const LifecycleTarget &target,
                                   const std::function<bool()> &cancelled) = 0;
};
}
