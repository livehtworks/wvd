#pragma once
#include <chrono>
#include <map>
#include <optional>
#include <set>
#include <json.hpp>
#include <string>
#include <vector>
#include "games/wvd/recovery/dialogue_policy.hpp"

namespace wvd::games::tasks {
// 任务构建器只描述业务图；推进、等待和事件优先级由原生 FlowExecutor 持有。
struct CompiledWorkflow {
    std::string kind;
    std::string entry{"Entry"};
    std::string terminal{"Terminal"};
    std::string checkpoint;
    nlohmann::json nodes = nlohmann::json::object();
    // 编译期已解析的节点作用域规则，封存后不可由运行时修改。
    nlohmann::json event_scopes = nlohmann::json::object();
    std::vector<std::string> images;
    std::vector<std::string> required_actions;
    std::chrono::milliseconds time_limit{60000};
    // 默认节点等待上限不等于调用累计期限；只有显式构造参数/作者声明才签发。
    std::optional<std::chrono::milliseconds> declared_budget;
    std::map<std::string, std::chrono::milliseconds> definition_budgets;
    nlohmann::json definition_checks = nlohmann::json::object();
    std::set<std::string> handoffs;
    recovery::DialoguePolicy dialogue_policy{recovery::DialoguePolicy::Default};
    // Bounty routes do not traverse the optional full-maze dialogue encounters.
    bool random_maze_events{true};
    // 作者定义和资源选择在编译后封存。
    nlohmann::json authoring = nlohmann::json::object();
    void refresh_images();
    void validate() const;
};

class PipelineCompiler {
  public:
    explicit PipelineCompiler(std::string kind);
    PipelineCompiler(std::string kind, std::chrono::milliseconds time_limit);
    static nlohmann::json image(const std::string &name);
    static nlohmann::json any(nlohmann::json conditions);
    static nlohmann::json all(nlohmann::json conditions);
    static nlohmann::json absent(nlohmann::json condition);
    static nlohmann::json business(const std::string &field, nlohmann::json value,
                                   const std::string &comparison = "eq");
    void route(const std::string &name, nlohmann::json next);
    void wait(const std::string &name, int milliseconds, nlohmann::json next);
    // 普通候选全部不符后的重观察，不是无条件成功边。ongoing 仅说明已知等待。
    void poll(const std::string &name, int milliseconds, nlohmann::json next,
              nlohmann::json ongoing = nullptr, nlohmann::json progress = nullptr);
    void diagnostic_candidate(const std::string &name);
    void mark_known_scene(const std::string &name);
    void check_policy(const std::string &phase, nlohmann::json inherit = nlohmann::json::array({"wvd-network-retry"}),
                      int debounce_ms = 1000, int interval_ms = 1000, bool protect_input = true);
    void handoff(const std::string &name, const std::string &port);
    void observe(const std::string &name, const nlohmann::json &condition, nlohmann::json next);
    void observe_business(const std::string &name, const nlohmann::json &condition, nlohmann::json next);
    void observe_ocr(const std::string &name, const std::vector<std::string> &expected,
                     nlohmann::json roi, nlohmann::json next);
    void click(const std::string &name, const nlohmann::json &scene, const nlohmann::json &target,
               const nlohmann::json &post, nlohmann::json next, nlohmann::json offset = {0, 0});
    void back(const std::string &name, const nlohmann::json &scene, const nlohmann::json &post,
              nlohmann::json next);
    void fixed_click(const std::string &name, const nlohmann::json &scene,
                     const nlohmann::json &post, nlohmann::json position, nlohmann::json next);
    void click_pair(const std::string &name, const nlohmann::json &scene, const nlohmann::json &target,
                    const nlohmann::json &post, nlohmann::json next);
    void swipe(const std::string &name, const nlohmann::json &scene,
               const nlohmann::json &post, nlohmann::json coordinates, nlohmann::json next, int duration = 400);
    // 内联子图的终点只能进入调用者指定后继。
    std::string append(const std::string &prefix, const CompiledWorkflow &child,
                       nlohmann::json next, const nlohmann::json &normal_exits = nlohmann::json::object());
    // 原生子任务独立持有命中预算；定义一次、有限调用，不复制整份战斗图。
    std::string define_child(const std::string &prefix, const CompiledWorkflow &child,
                             const std::vector<std::string> &normal_returns = {});
    void call_child(const std::string &name, const std::string &entry, nlohmann::json next,
                    nlohmann::json handoffs = nlohmann::json::object());
    void public_step(const std::string &name, const std::string &flow_id,
                     const nlohmann::json &arguments, nlohmann::json next,
                     nlohmann::json handoffs = nlohmann::json::object(),
                     nlohmann::json condition = nullptr);
    void recovery(const std::string &name, const std::string &reason);
    // 已知分类失败后的第五次未知观察才允许生成跳跃等待/转交意图，不发送输入。
    void unknown_leap(const std::string &name, nlohmann::json next,
                      nlohmann::json extra_known = nlohmann::json::array(), bool classified = false);
    void confirm(const std::string &name, const std::string &operation, const std::string &event,
                 const nlohmann::json &condition, nlohmann::json next,
                 nlohmann::json expected_step = nullptr, const std::string &enemy_rule = {});
    // 输入默认限制连续未确认尝试；作者显式业务循环可选择整次调用计数。
    void hit_limit(const std::string &name, int limit, bool invocation_count = false);
    void event_scope(const std::string &name, nlohmann::json rules);
    void failure_route(const std::string &name, nlohmann::json next);
    void business_failure(const std::string &name, const std::string &reason);
    void delay_after(const std::string &name, int milliseconds);
    void postcondition_budget(const std::string &name, int milliseconds);
    // 菜单操作的显式重试授权；ready 必须证实原页/按钮仍在且无阻塞。
    // 领奖/宝石购买不可重复；普通金币住宿、跳轮由游戏层显式授权。
    // restart_from仅允许本子流程的只读Route，重启后重新认页而非重跑根任务。
    void retry_menu_input(const std::string &name, const nlohmann::json &ready, int interval_ms = 5000,
                          unsigned max_submissions = 0, const std::string &restart_from = "");
    void input_effect(const std::string &name, const std::string &binding);
    void allowed_area(const std::string &name, nlohmann::json area);
    // 仅在正常候选全未命中、且异常处理器未接住时检查本作用域的插入出口。
    // 调用者必须把 BlockedExit 绑定到重新观察入口；独立运行则报告需要外层处理。
    void interrupt_on(nlohmann::json condition, std::string reason, std::string port = "");
    void use_dialogue(recovery::DialoguePolicy policy);
    // 此节点已可能产生副作用。后继遇覆盖层只能报告结果未确认，不能正常返回后重放。
    void stop_if_interrupted_after(const std::string &name, std::string reason);
    // 固定游戏行为 binding；不开放任意动作/任意实现名称。
    void combat_step(const std::string &name, const nlohmann::json &condition,
                     nlohmann::json parameters, nlohmann::json next);
    void chest_selection(const std::string &name, const nlohmann::json &condition,
                         int preferred, unsigned seed, nlohmann::json next);
    CompiledWorkflow finish();

  private:
    CompiledWorkflow workflow_;
    std::vector<std::string> local_nodes_;
    nlohmann::json interruption_;
    std::string interruption_reason_;
    std::string interruption_port_;
    std::map<std::string, std::string> uncertain_actions_;
    // 单次编译的同参数公共定义只展开一次；调用帧/计数仍由每次 Call 独占。
    std::map<std::string, std::string> public_steps_;
    void compile_interruption();
    void add(const std::string &name, nlohmann::json node);
    nlohmann::json request(const nlohmann::json &condition) const;
    void action(const std::string &name, const nlohmann::json &scene, const nlohmann::json &target,
                const nlohmann::json &post, nlohmann::json command, nlohmann::json next,
                nlohmann::json offset);
};
} // namespace wvd::games::tasks
