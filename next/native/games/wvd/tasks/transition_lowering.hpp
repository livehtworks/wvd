#pragma once
#include <json.hpp>
#include <algorithm>
#include <chrono>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace wvd::games::tasks {
// 发布期将输入节点拆成「提交一次」和「独立观察」两个 Maa 节点。
// 这是现有 Pipeline 的编译步骤，不创建第二套执行器；作者文档/旧任务数据不被改写。
inline nlohmann::json lower_input_transitions(const nlohmann::json &source,
                                             std::chrono::milliseconds session_budget) {
    using J = nlohmann::json;
    constexpr std::size_t source_node_limit = 4608; // 沿用既有编译图上限。
    if (!source.is_object() || source.empty() || source.size() > source_node_limit ||
        session_budget.count() < 1 || session_budget.count() > 24LL * 60 * 60 * 1000)
        throw std::runtime_error("TRANSITION_SOURCE_INVALID");
    J result = source;
    std::map<std::string, std::string> observers;
    for (const auto &[name, original] : source.items()) {
        if (original.value("custom_action", "") != "GuardedAction") continue;
        const auto &parameters = original.at("custom_action_param");
        if (parameters.contains("input_contract") || !parameters.contains("postcondition") ||
            !original.contains("next") || !original.at("next").is_array() || original.at("next").empty())
            throw std::runtime_error("TRANSITION_SOURCE_CONTRACT_INVALID:" + name);
        const auto observer = "__wvd_observe__" + name;
        if (source.contains(observer) || result.contains(observer))
            throw std::runtime_error("TRANSITION_NODE_COLLISION:" + observer);
        const auto delay = original.value("post_delay", std::int64_t{0});
        // 旧默认 3000ms 不再成为隐式网络期限。只有显式业务预算才覆盖会话预算；
        // 会话监督器仍有总截止时间，不能因每个观察节点重启预算而无限运行。
        const auto explicit_budget = parameters.value("transition_timeout_ms", std::int64_t{0});
        const auto budget = explicit_budget > 0 ? explicit_budget : session_budget.count();
        if (explicit_budget < 0 || delay < 0 || delay > 10000 || budget <= delay ||
            budget > session_budget.count())
            throw std::runtime_error("TRANSITION_TIMING_INVALID:" + name);
        J wait{{"recognition", "DirectHit"}, {"action", "Custom"},
               {"custom_action", "AwaitTransition"},
               {"custom_action_param", {{"input_contract", 2}, {"source_node", name},
                    {"postcondition", parameters.at("postcondition")},
                    {"observation_budget_ms", budget}, {"initial_delay_ms", delay},
                    {"poll_interval_ms", std::clamp(original.value("rate_limit", 50), 10, 10000)}}},
               {"pre_delay", 0}, {"post_delay", 0}, {"rate_limit", 50},
               {"timeout", original.value("timeout", budget)},
               {"max_hit", original.value("max_hit", 1)}, {"next", original.at("next")}};
        if (original.contains("on_error")) wait["on_error"] = original.at("on_error");
        auto &input = result.at(name);
        input["custom_action_param"]["input_contract"] = 2;
        input["post_delay"] = 0; // 旧 Sleep 位于输入之后、观察之前，不再后置到确认之后。
        input["next"] = J::array({observer});
        result[observer] = std::move(wait);
        observers.emplace(name, observer);
    }
    // 作用域前缀已由原编译器处理。补入派生观察节点，避免子流程第二次执行时 max_hit 用尽。
    for (auto &[name, node] : result.items()) {
        (void)name;
        if (node.value("custom_action", "") != "RunChild") continue;
        auto &reset = node.at("custom_action_param").at("reset_hit_counts");
        std::set<std::string> names;
        for (const auto &item : reset) {
            const auto original = item.get<std::string>();
            if (!source.contains(original)) throw std::runtime_error("TRANSITION_CHILD_SCOPE_INVALID");
            names.insert(original);
            if (const auto found = observers.find(original); found != observers.end()) names.insert(found->second);
        }
        reset = names;
    }
    if (result.size() != source.size() + observers.size() || result.size() > source_node_limit * 2)
        throw std::runtime_error("TRANSITION_EXPANSION_INVALID");
    // 每个新节点都只能由其唯一输入源进入；错误出口仍是原图的错误出口。
    for (const auto &[name, node] : result.items()) {
        (void)name;
        for (const auto *edge : {"next", "on_error"})
            for (const auto &target : node.value(edge, J::array()))
                if (!target.is_string() || !result.contains(target.get<std::string>()))
                    throw std::runtime_error("TRANSITION_EDGE_INVALID");
        if (node.value("custom_action", "") == "RunChild" &&
            !result.contains(node.at("custom_action_param").at("entry").get<std::string>()))
            throw std::runtime_error("TRANSITION_CHILD_ENTRY_INVALID");
    }
    return result;
}
} // namespace wvd::games::tasks
