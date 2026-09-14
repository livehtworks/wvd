#pragma once
#include "action.hpp"
#include <json.hpp>
#include <string_view>

namespace wvd::contracts {
enum class RunState {
    Idle,
    Preparing,
    Running,
    Recovering,
    StopRequested,
    Completed,
    UserStopped,
    Failed,
    Interrupted
};
inline std::string_view name(RunState state) {
    switch (state) {
    case RunState::Idle:
        return "Idle";
    case RunState::Preparing:
        return "Preparing";
    case RunState::Running:
        return "Running";
    case RunState::Recovering:
        return "Recovering";
    case RunState::StopRequested:
        return "StopRequested";
    case RunState::Completed:
        return "Completed";
    case RunState::UserStopped:
        return "UserStopped";
    case RunState::Failed:
        return "Failed";
    case RunState::Interrupted:
        return "Interrupted";
    }
    return "Failed";
}
enum class SessionEnd { Completed, Failed, UserStopped, RecoveryRequired };
struct RootEvidence {
    std::int64_t task_id{};
    std::uint64_t generation{};
    int depth{-1};
    std::string node;
};
struct SessionResult {
    SessionEnd end{SessionEnd::Failed};
    std::string reason;
    int engine_status{};
    std::int64_t root_task_id{};
    RootEvidence terminal;
    InputCounts inputs;
    bool quiescent{};
    RootEvidence checkpoint;
    nlohmann::json business;
};
// 只允许尚未启动根任务、没有输入且已真实静止的连接失败进入显式恢复策略。
// 控制器回调异常有自己的首要错误码，不能被连接失败或重试掩盖。
inline bool connection_failed_before_task(const SessionResult &result) {
    return result.end == SessionEnd::Failed && result.reason == "CONTROLLER_CONNECT_FAILED" &&
           result.quiescent && result.root_task_id == 0 && result.inputs.attempted == 0 &&
           result.inputs.accepted == 0 && result.inputs.backend_called == 0;
}
struct RunSnapshot {
    std::uint64_t run_id{}, generation{};
    RunState state{RunState::Idle};
    std::string reason;
    bool quiescent{true}, result_saved{};
    int engine_status{};
    InputCounts inputs;
    std::string storage_error;
    std::vector<std::string> secondary_errors;
    nlohmann::json sessions = nlohmann::json::array();
    nlohmann::json business = nullptr;
    std::size_t completed_business_units{};
};
} // namespace wvd::contracts
