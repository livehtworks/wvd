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
    Poll,
    Call,
    Return,
    BusinessConfirm,
    RegisteredOperation,
    Finish,
    ExternalBlocked,
    Fail
};

// Overlay 是作者显式声明的抢占观察；Exception/Special 只在业务结果不符时扫描。
// 战斗和宝箱的正常阶段由各自 Definition 推进，不注册成全局页面穷举事件。
enum class EventClass { Overlay, Encounter, Exception, Special };
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
    bool on_device_restart{}; // 仅经生命周期退出/重开证据触发，不参与逐帧事件扫描。
};

struct Observe {
    recognition::Request request;
};
struct Route {
    std::vector<std::string> candidates;
};
// 只对业务明确允许重复的菜单动作声明。原页重新确认不是目标成功；
// 重试仍走正式输入门禁，并共用首次提交的等待期限。
struct InputRetry {
    recognition::Request ready;
    std::chrono::milliseconds interval{5000};
    unsigned max_submissions{}; // 0不限制次数，但原结果窗口、间隔与总期限保持有效。
    // 游戏层允许重做的操作在重启后回到本子流程的只读选路点；空值恢复原候选。
    // 不允许跳回根任务、清业务账目或绕过宝石购买禁令。
    std::string restart_from;
};
struct Input {
    recognition::Request scene;
    recognition::Request target;
    nlohmann::json command;
    contracts::Box allowed_area;
    std::optional<contracts::Point> target_offset;
    bool use_target_center{true};
    bool clip_target_to_area{};
    std::optional<InputRetry> retry;
    std::string effect_binding; // 业务层准入/实际提交记账；运行层不解释币种。
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
// Poll 是“没有可执行的正常后继，稍后用新帧重看”，不是一次成功推进。
// ongoing 可证明本阶段仍在正常等待；它不能授权输入，也不能证明任务终点。
// 候选中的 Poll 必须最后兜底；普通候选和本阶段诊断均有机会先处理现场。
struct Poll {
    std::chrono::milliseconds interval;
    std::optional<recognition::Request> ongoing;
    // 只有业务区域的实际变化才能续期；页面仍存在本身不是进展。
    std::optional<recognition::Request> progress;
};
struct Call {
    std::string definition;
    std::map<std::string, std::vector<std::string>> handoffs;
};
struct Return {
    std::string outcome;
    std::string reason;
    std::string port;
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

using StepData = std::variant<Observe, Route, Input, AwaitResult, Wait, Poll, Call, Return,
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
    // 非输入的0表示无隐式经过上限，仍受节点/调用/会话期限和显式业务限制约束。
    int max_hit{1};
    bool consecutive_input_limit{};
    bool handles_business_failure{};
    std::string check_group{"business"};
    // 仅作为正常候选全部未命中后的出口，不能抢在正常业务之前识图。
    bool unexpected_only{};
    // 由业务构建器显式声明的正面场景证据；不是任意 template/absent 命中。
    // 用于结束“连续未知”窗口，不改变路线进度和业务计数。
    bool marks_known_scene{};
    std::vector<EventRule> event_policy;
    std::set<std::string> disabled_events;
};

struct Definition {
    std::string id;
    std::string entry;
    std::map<std::string, Step> steps;
    std::optional<std::chrono::milliseconds> cumulative_budget;
    // 历史作者文档保留显式事件继承；WVD 活动块声明必要规则白名单。
    struct CheckPolicy {
        std::string phase{"business"};
        std::set<std::string> inherit;
        std::chrono::milliseconds debounce{1000};
        std::chrono::milliseconds interval{1000};
    };
    std::optional<CheckPolicy> checks;
    std::vector<EventRule> events;
    std::set<std::string> handoffs;
};

struct FlowProgram {
    static constexpr int schema = 7; // 轮询以业务进展续期，不以正常经过次数限制战斗。
    std::string engine_kind{"wvd_native"};
    std::string revision;
    std::string root_definition;
    std::map<std::string, Definition> definitions;
    void validate() const;
};
} // namespace wvd::workflow
