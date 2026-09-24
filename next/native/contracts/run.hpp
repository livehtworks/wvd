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
enum class SessionEnd { Completed, BusinessFailed, Failed, UserStopped, RecoveryRequired, ExternalBlocked };
struct RootEvidence {
    std::uint64_t generation{};
    std::string source_path;
};
struct SessionResult {
    SessionEnd end{SessionEnd::Failed};
    std::string reason;
    std::string outcome_category;
    RootEvidence terminal;
    InputCounts inputs;
    bool quiescent{};
    RootEvidence checkpoint;
    nlohmann::json business;
    nlohmann::json unresolved_inputs = nlohmann::json::array();
};
struct RunSnapshot {
    std::uint64_t run_id{}, generation{};
    RunState state{RunState::Idle};
    std::string reason;
    std::string outcome_category;
    bool quiescent{true}, result_saved{};
    InputCounts inputs;
    std::string storage_error;
    std::vector<std::string> secondary_errors;
    nlohmann::json sessions = nlohmann::json::array();
    nlohmann::json business = nullptr;
    std::size_t completed_business_units{};
    nlohmann::json active_event = nullptr;
    nlohmann::json execution = nullptr;
    nlohmann::json unresolved_inputs = nlohmann::json::array();
};
} // namespace wvd::contracts
