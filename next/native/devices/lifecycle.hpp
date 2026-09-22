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
// 初始启动只允许“可选 VPN -> 确保游戏运行/前台”，不允许停游戏或重启实例。
inline bool initial_lifecycle_plan(const LifecyclePlan &plan) {
    using O = LifecycleOperation;
    if (plan.attempt != 1) return false;
    if (plan.operations == std::vector<O>{O::StartApplication}) return !plan.target.vpn_required;
    if (!plan.target.vpn_required) return false;
    return plan.operations == std::vector<O>{O::EnsureVpn} ||
           plan.operations == std::vector<O>{O::EnsureVpn, O::StartApplication};
}
// 由当前后端/Session 借用；不暴露 Shell、坐标或任意包名输入。
// 实际端口仍由同一会话串行调用，停止请求不能从另一线程销毁 SDK 对象。
class LifecyclePort {
  public:
    virtual ~LifecyclePort() = default;
    virtual std::optional<LifecycleObservation> observe_lifecycle() = 0;
    virtual bool execute_lifecycle(LifecycleOperation operation, const LifecycleTarget &target,
                                   const std::function<bool()> &cancelled) = 0;
};
}
