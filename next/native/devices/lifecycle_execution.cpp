#include "lifecycle_execution.hpp"
#include "adb_failure.hpp"
#include <thread>
#include <set>

namespace wvd::devices {
namespace {
using J = nlohmann::json;
using O = LifecycleOperation;
std::string name(O operation) {
    switch (operation) {
    case O::StopApplication: return "StopApplication";
    case O::StartApplication: return "StartApplication";
    case O::Reconnect: return "Reconnect";
    case O::RestartInstance: return "RestartInstance";
    case O::EnsureVpn: return "EnsureVpn";
    }
    throw std::runtime_error("LIFECYCLE_OPERATION_INVALID");
}
bool same_target(const LifecycleTarget &a, const LifecycleTarget &b) {
    return a.device_id == b.device_id && a.instance_id == b.instance_id &&
           a.application_id == b.application_id && a.vpn_application_id == b.vpn_application_id &&
           a.vpn_required == b.vpn_required;
}
J state_json(const LifecycleObservation &state) {
    return {{"instance_running", state.instance_running}, {"instance_exited", state.instance_exited}, {"connected", state.connected},
             {"application_running", state.application_running}, {"vpn_ready", state.vpn_ready},
             {"application_foreground", state.application_foreground},
             {"connection_generation", state.connection_generation}};
}
bool precondition(O operation, const LifecycleObservation &s) {
    if (operation == O::RestartInstance)
        return s.instance_exited && !s.instance_running && !s.connected;
    if (operation == O::Reconnect)
        return s.instance_running;
    if (!s.instance_running || !s.connected)
        return false;
    if (operation == O::StartApplication)
        return !s.application_foreground && (!s.target.vpn_required || s.vpn_ready);
    return true;
}
bool already_done(O operation, const LifecycleObservation &s) {
    return (operation == O::StopApplication && !s.application_running && !s.application_foreground && s.connected) ||
           (operation == O::EnsureVpn && s.vpn_ready && s.connected) ||
           (operation == O::StartApplication && s.application_running && s.application_foreground && s.connected &&
            (!s.target.vpn_required || s.vpn_ready));
}
bool postcondition(O operation, const LifecycleObservation &before, const LifecycleObservation &after) {
    if (!after.instance_running || !after.connected || !after.connection_generation)
        return false;
    if (operation == O::Reconnect || operation == O::RestartInstance)
        return after.connection_generation > before.connection_generation;
    return already_done(operation, after);
}
}
void validate_lifecycle_plan(const LifecyclePlan &plan) {
    const auto &t = plan.target;
    if (t.device_id.empty() || t.instance_id.empty() || t.application_id.empty() ||
        t.device_id.size() > 256 || t.instance_id.size() > 128 || t.application_id.size() > 256 ||
        t.vpn_application_id.size() > 256 || (t.vpn_required && t.vpn_application_id.empty()) ||
        plan.attempt < 1 || plan.operations.empty() || plan.operations.size() > 5 ||
        plan.step_timeout.count() < 1 || plan.step_timeout > std::chrono::minutes{3} ||
        plan.defer_for < std::chrono::milliseconds::zero() ||
        plan.defer_for > std::chrono::hours{24})
        throw std::runtime_error("LIFECYCLE_PLAN_INVALID");
    std::set<O> seen;
    for (auto operation : plan.operations) {
        name(operation);
        if (!seen.insert(operation).second || (operation == O::EnsureVpn && !t.vpn_required))
            throw std::runtime_error("LIFECYCLE_PLAN_INVALID");
    }
}
J lifecycle_plan_json(const LifecyclePlan &plan) {
    validate_lifecycle_plan(plan);
    J operations = J::array();
    for (auto operation : plan.operations)
        operations.push_back(name(operation));
    return {{"schema", 1}, {"device_id", plan.target.device_id}, {"instance_id", plan.target.instance_id},
             {"application_id", plan.target.application_id}, {"vpn_application_id", plan.target.vpn_application_id},
             {"vpn_required", plan.target.vpn_required}, {"attempt", plan.attempt},
             {"step_timeout_ms", plan.step_timeout.count()},
             {"defer_for_ms", plan.defer_for.count()}, {"operations", operations}};
}
LifecycleEnd execute_lifecycle_plan(const LifecyclePlan &plan, LifecyclePort &port,
    const std::function<bool()> &cancelled,
    const std::function<void(const std::string &, const J &)> &event) {
    validate_lifecycle_plan(plan);
    if (plan.defer_for != std::chrono::milliseconds::zero())
        throw std::runtime_error("LIFECYCLE_DEFER_NOT_CONSUMED");
    auto observe = [&] {
        auto value = port.observe_lifecycle();
        const auto now = std::chrono::steady_clock::now();
        if (!value || !same_target(value->target, plan.target) || value->observed_at > now ||
            now - value->observed_at > std::chrono::seconds(2) ||
            (value->application_foreground && !value->application_running) ||
            (value->connected && (!value->instance_running || !value->connection_generation)) ||
            (value->instance_exited && (value->instance_running || value->connected)))
            throw std::runtime_error("LIFECYCLE_OBSERVATION_INVALID");
        return *value;
    };
    for (auto operation : plan.operations) {
        const auto deadline = std::chrono::steady_clock::now() + plan.step_timeout;
        unsigned transport_failures = 0;
        bool operation_done = false;
        while (!operation_done && std::chrono::steady_clock::now() < deadline) try {
        if (cancelled())
            return LifecycleEnd::Cancelled;
        auto before = observe();
        const auto operation_name = name(operation);
        event("lifecycle.observed", {{"operation", operation_name}, {"state", state_json(before)}});
        if (already_done(operation, before)) {
            event("lifecycle.confirmed", {{"operation", operation_name}, {"skipped", true}});
            operation_done = true;
            break;
        }
        if (!precondition(operation, before)) {
            event("lifecycle.retry_required", {{"operation", operation_name}, {"reason", "PRECONDITION_MISSING"}});
            return LifecycleEnd::RetryRequired;
        }
        // 调用尝试与调用后的结果分开记录，底层阻塞时不能伪报已取消/静止。
        if (cancelled())
            return LifecycleEnd::Cancelled;
        event("lifecycle.backend_called", {{"operation", operation_name}});
        const auto stop_or_expired = [&] {
            return cancelled() || std::chrono::steady_clock::now() >= deadline;
        };
        if (!port.execute_lifecycle(operation, plan.target, stop_or_expired)) {
            if (cancelled())
                return LifecycleEnd::Cancelled;
            event("lifecycle.retry_required", {{"operation", operation_name}, {"reason", "BACKEND_RETURNED_FALSE"}});
            return LifecycleEnd::RetryRequired;
        }
        bool confirmed = false;
        auto next_start_retry = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        unsigned start_attempts = 1;
        do {
            if (cancelled())
                return LifecycleEnd::Cancelled;
            if (std::chrono::steady_clock::now() >= deadline) break;
            const auto after = observe();
            if (postcondition(operation, before, after)) {
                event("lifecycle.confirmed", {{"operation", operation_name}, {"skipped", false}, {"state", state_json(after)}});
                confirmed = true;
                break;
            }
            // Android can acknowledge am start before the task actually gains
            // focus. Recheck the bound target before bringing it forward again.
            if (operation == O::StartApplication && precondition(operation, after) &&
                std::chrono::steady_clock::now() >= next_start_retry && !stop_or_expired()) {
                event("lifecycle.backend_retried", {{"operation", operation_name},
                    {"attempt", ++start_attempts}, {"reason", "FOREGROUND_NOT_CONFIRMED"},
                    {"state", state_json(after)}});
                const bool accepted = port.execute_lifecycle(operation, plan.target, stop_or_expired);
                if (!accepted) event("lifecycle.retry_pending", {{"operation", operation_name},
                    {"reason", "BACKEND_RETURNED_FALSE"}, {"attempt", start_attempts}});
                next_start_retry = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } while (std::chrono::steady_clock::now() < deadline);
        if (!confirmed) {
            event("lifecycle.retry_required", {{"operation", operation_name}, {"reason", "POSTCONDITION_TIMEOUT"}});
            return LifecycleEnd::RetryRequired;
        }
        operation_done = true;
        } catch (const AdbCommandFailure &error) {
            const auto &failure = error.info();
            event("lifecycle.command_failed", {{"operation", name(operation)},
                {"code", failure.code}, {"command", failure.command}, {"serial", failure.serial},
                {"timeout_ms", failure.timeout.count()}, {"elapsed_ms", failure.elapsed.count()},
                {"retryable_transport", failure.retryable_transport}, {"attempt", ++transport_failures}});
            if (cancelled()) return LifecycleEnd::Cancelled;
            if (!failure.retryable_transport) throw;
            // An intent may have taken effect despite ADB timing out. Re-observe
            // the bound target before any repeat; never replay gameplay inputs.
            const auto wake = std::min(deadline, std::chrono::steady_clock::now() + std::chrono::milliseconds(250));
            while (!cancelled() && std::chrono::steady_clock::now() < wake)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (!operation_done) {
            if (cancelled()) return LifecycleEnd::Cancelled;
            event("lifecycle.retry_required", {{"operation", name(operation)},
                {"reason", "TRANSPORT_RETRY_WINDOW_EXHAUSTED"}, {"failures", transport_failures}});
            return LifecycleEnd::RetryRequired;
        }
    }
    return LifecycleEnd::ReadyForBoot;
}
}
