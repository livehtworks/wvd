#pragma once
#include <chrono>
#include <map>
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
    recovery::DialoguePolicy dialogue_policy{recovery::DialoguePolicy::Default};
    // 作者定义和资源选择在编译后封存。
    nlohmann::json authoring = nlohmann::json::object();
    void refresh_images();
    void validate() const;
};

class PipelineCompiler {
  public:
    explicit PipelineCompiler(std::string kind, std::chrono::milliseconds time_limit = std::chrono::milliseconds{60000});
    static nlohmann::json image(const std::string &name);
    static nlohmann::json any(nlohmann::json conditions);
    static nlohmann::json all(nlohmann::json conditions);
    static nlohmann::json absent(nlohmann::json condition);
    static nlohmann::json business(const std::string &field, nlohmann::json value,
                                   const std::string &comparison = "eq");
    void route(const std::string &name, nlohmann::json next);
    void wait(const std::string &name, int milliseconds, nlohmann::json next);
    void observe(const std::string &name, const nlohmann::json &condition, nlohmann::json next);
    void observe_ocr(const std::string &name, const std::vector<std::string> &expected,
                     nlohmann::json roi, nlohmann::json next);
    void click(const std::string &name, const nlohmann::json &scene, const nlohmann::json &target,
               const nlohmann::json &post, nlohmann::json next, nlohmann::json offset = {0, 0});
    void back(const std::string &name, const nlohmann::json &scene, const nlohmann::json &post,
              nlohmann::json next);
    void fixed_click(const std::string &name, const nlohmann::json &scene,
                     const nlohmann::json &post, nlohmann::json position, nlohmann::json next);
    void swipe(const std::string &name, const nlohmann::json &scene,
               const nlohmann::json &post, nlohmann::json coordinates, nlohmann::json next, int duration = 400);
    // 内联子图的终点只能进入调用者指定后继。
    std::string append(const std::string &prefix, const CompiledWorkflow &child,
                       nlohmann::json next, const nlohmann::json &normal_exits = nlohmann::json::object());
    // 原生子任务独立持有命中预算；定义一次、有限调用，不复制整份战斗图。
    std::string define_child(const std::string &prefix, const CompiledWorkflow &child,
                             const std::vector<std::string> &normal_returns = {});
    void call_child(const std::string &name, const std::string &entry, nlohmann::json next);
    void recovery(const std::string &name, const std::string &reason);
    // 已知分类失败后的第五次未知观察才允许生成跳跃等待/转交意图，不发送输入。
    void unknown_leap(const std::string &name, nlohmann::json next,
                      nlohmann::json extra_known = nlohmann::json::array());
    void confirm(const std::string &name, const std::string &operation, const std::string &event,
                 const nlohmann::json &condition, nlohmann::json next,
                 nlohmann::json expected_step = nullptr);
    void hit_limit(const std::string &name, int limit);
    void event_scope(const std::string &name, nlohmann::json rules);
    void failure_route(const std::string &name, nlohmann::json next);
    void delay_after(const std::string &name, int milliseconds);
    void postcondition_budget(const std::string &name, int milliseconds);
    void allowed_area(const std::string &name, nlohmann::json area);
    // 显式启用本作用域的普通插入出口；不改子图、不吞掉错误或伪造业务完成。
    // 调用者必须把 BlockedExit 绑定到重新观察入口；独立运行则报告需要外层处理。
    void interrupt_on(nlohmann::json condition, std::string reason);
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
    std::map<std::string, std::string> uncertain_actions_;
    void compile_interruption();
    void add(const std::string &name, nlohmann::json node);
    nlohmann::json request(const nlohmann::json &condition) const;
    void action(const std::string &name, const nlohmann::json &scene, const nlohmann::json &target,
                const nlohmann::json &post, nlohmann::json command, nlohmann::json next,
                nlohmann::json offset);
};
} // namespace wvd::games::tasks
