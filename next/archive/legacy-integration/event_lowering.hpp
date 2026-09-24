#pragma once
#include <json.hpp>
#include <algorithm>
#include <set>
#include <stdexcept>
#include <string>

namespace wvd::games::tasks {
// 事件候选只做识别；动作回调重新确认新帧，处理后由 Maa 返回父 next。
inline nlohmann::json lower_event_candidates(const nlohmann::json &source, nlohmann::json &scopes) {
    using J = nlohmann::json;
    if (!source.is_object() || !scopes.is_object())
        throw std::runtime_error("EVENT_LOWERING_INVALID");
    auto result = source;
    std::size_t ordinal{};
    for (auto &[owner, rules] : scopes.items()) {
        if (!source.contains(owner) || !rules.is_array())
            throw std::runtime_error("EVENT_SCOPE_NODE_MISSING:" + owner);
        std::stable_sort(rules.begin(), rules.end(), [](const J &a, const J &b) {
            return a.at("priority").get<int>() > b.at("priority").get<int>();
        });
        for (auto &rule : rules) {
            if (!rule.contains("reset_hit_counts")) continue;
            std::set<std::string> reset;
            for (const auto &name : rule.at("reset_hit_counts")) {
                const auto base = name.get<std::string>();
                if (!source.contains(base)) throw std::runtime_error("EVENT_RESET_NODE_MISSING:" + base);
                reset.insert(base);
                const auto observer = "__wvd_observe__" + base;
                if (source.contains(observer)) reset.insert(observer);
            }
            rule["reset_hit_counts"] = reset;
        }
        auto &parent = result.at(owner);
        const auto action = parent.value("custom_action", "");
        const auto route_owner = action == "GuardedAction" ? "__wvd_observe__" + owner : owner;
        J route_candidates = J::array();
        for (const auto &rule : rules) {
            if (!rule.contains("resume") || rule.at("resume").value("mode", "") != "replan") continue;
            if (!result.contains(route_owner) || !result.at(route_owner).contains("next"))
                throw std::runtime_error("EVENT_REPLAN_ROUTE_MISSING:" + owner);
            const auto target = rule.at("resume").at("node_id").get<std::string>();
            if (!result.contains(target)) throw std::runtime_error("EVENT_REPLAN_TARGET_MISSING:" + target);
            const auto candidate = "__wvd_event_route_" + std::to_string(ordinal++);
            if (result.contains(candidate)) throw std::runtime_error("EVENT_NODE_COLLISION");
            const J route{{"source_node", owner}, {"event_id", rule.at("id")}, {"target", target}};
            result[candidate] = {{"recognition", "Custom"}, {"custom_recognition", "EventRoute"},
                {"custom_recognition_param", route}, {"roi", {0, 0, 900, 1600}},
                {"action", "Custom"}, {"custom_action", "ConsumeEventRoute"},
                {"custom_action_param", route}, {"next", J::array({target})},
                {"pre_delay", 0}, {"post_delay", 0}, {"rate_limit", 50},
                {"timeout", parent.value("timeout", 60000)}};
            route_candidates.push_back(candidate);
        }
        if (!route_candidates.empty()) {
            auto &route_parent = result.at(route_owner);
            const auto original_next = route_parent.at("next");
            route_parent["next"] = route_candidates;
            for (const auto &target : original_next) route_parent["next"].push_back(target);
        }
        if (!parent.contains("next") || parent.at("next").empty() ||
            action == "GuardedAction" || action == "AwaitTransition")
            continue;
        J overlays = J::array(), encounters = J::array();
        for (const auto &rule : rules) {
            const auto candidate = "__wvd_event_" + std::to_string(ordinal++);
            if (result.contains(candidate)) throw std::runtime_error("EVENT_NODE_COLLISION");
            result[candidate] = {{"recognition", "Custom"}, {"custom_recognition", "WvdVision"},
                {"custom_recognition_param", rule.at("detect")}, {"roi", {0, 0, 900, 1600}},
                {"action", "Custom"}, {"custom_action", "DispatchEvent"},
                {"custom_action_param", {{"source_node", owner}, {"event_id", rule.at("id")},
                                         {"class", rule.at("class")}}},
                {"next", J::array()}, {"on_error", parent.value("on_error", J::array())},
                {"pre_delay", 0}, {"post_delay", 0}, {"rate_limit", 50},
                {"timeout", parent.value("timeout", 60000)}};
            (rule.at("class") == "overlay" ? overlays : encounters).push_back("[JumpBack]" + candidate);
        }
        const auto original = source.at(owner).at("next");
        J ordered = route_candidates;
        for (const auto &candidate : overlays) ordered.push_back(candidate);
        bool encounters_inserted = false;
        for (const auto &target : original) {
            if (!target.is_string() || !source.contains(target.get<std::string>()))
                throw std::runtime_error("EVENT_PARENT_EDGE_INVALID:" + owner);
            if (!encounters_inserted && source.at(target.get<std::string>()).value("recognition", "DirectHit") == "DirectHit") {
                for (const auto &candidate : encounters) ordered.push_back(candidate);
                encounters_inserted = true;
            }
            ordered.push_back(target);
        }
        if (!encounters_inserted)
            for (const auto &candidate : encounters) ordered.push_back(candidate);
        parent["next"] = std::move(ordered);
    }
    if (result.size() > 9216) throw std::runtime_error("EVENT_EXPANSION_LIMIT");
    return result;
}
} // namespace wvd::games::tasks
