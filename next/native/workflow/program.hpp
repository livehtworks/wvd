#pragma once

#include "recognition/request.hpp"
#include <chrono>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

namespace wvd::workflow {
enum class StepKind {
    Observe,
    Route,
    Input,
    AwaitResult,
    Wait,
    Call,
    Return,
    BusinessConfirm,
    RegisteredOperation,
    Finish,
    ExternalBlocked,
    Fail
};

enum class EventClass { Overlay, Encounter };
enum class EventDisposition { Handle, ExternalBlocked };
enum class ResumeMode { Reobserve, Replan };

struct EventRule {
    std::string id;
    EventClass category{EventClass::Overlay};
    int priority{};
    recognition::Request detect;
    EventDisposition disposition{EventDisposition::Handle};
    std::string handler_definition;
    ResumeMode resume{ResumeMode::Reobserve};
    std::string replan_step;
    std::string reason;
    std::chrono::milliseconds exit_budget{5000};
    std::chrono::milliseconds ambiguity_budget{5000};
    std::optional<recognition::Request> resume_guard;
};

struct Observe {
    recognition::Request request;
};
struct Route {
    std::vector<std::string> candidates;
};
struct Input {
    recognition::Request scene;
    recognition::Request target;
    nlohmann::json command;
    contracts::Box allowed_area;
    std::optional<contracts::Point> target_offset;
    bool use_target_center{true};
    bool clip_target_to_area{};
};
struct AwaitResult {
    recognition::Request condition;
    std::chrono::milliseconds budget;
    std::chrono::milliseconds initial_delay{0};
    std::chrono::milliseconds poll_interval{50};
};
struct Wait {
    std::chrono::milliseconds duration;
};
struct Call {
    std::string definition;
};
struct Return {
    std::string outcome;
    std::string reason;
};
struct BusinessConfirm {
    std::string operation;
    recognition::Request condition;
    nlohmann::json parameters;
    std::string binding; // 游戏编译层提供绑定名；通用执行器不选择 WVD 实现。
};
struct RegisteredOperation {
    std::string binding;
    nlohmann::json parameters;
};
struct Finish {};
struct BusinessFail {
    std::string reason;
};
struct ExternalBlocked {
    std::string reason;
};
struct Fail {
    std::string reason;
};

using StepData = std::variant<Observe, Route, Input, AwaitResult, Wait, Call, Return,
                              BusinessConfirm, RegisteredOperation, Finish, BusinessFail,
                              ExternalBlocked, Fail>;

struct Step {
    std::string id;
    std::string source_path;
    StepData data;
    std::optional<recognition::Request> guard;
    // 只读业务标量条件，不携带视觉证据，也不能授权输入。
    std::optional<nlohmann::json> business_guard;
    std::vector<std::string> next;
    std::vector<std::string> on_error;
    std::chrono::milliseconds time_limit{60000};
    std::chrono::milliseconds delay_after{0};
    int max_hit{1};
    bool handles_business_failure{};
    std::vector<EventRule> event_policy;
    std::set<std::string> disabled_events;
};

struct Definition {
    std::string id;
    std::string entry;
    std::map<std::string, Step> steps;
    std::optional<std::chrono::milliseconds> cumulative_budget;
};

struct FlowProgram {
    static constexpr int schema = 4; // 每次调用的累计预算及独立纯业务 guard。
    std::string engine_kind{"wvd_native"};
    std::string revision;
    std::string root_definition;
    std::map<std::string, Definition> definitions;
    void validate() const;
};
} // namespace wvd::workflow
