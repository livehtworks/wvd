#pragma once

#include "document_parameters.hpp"
#include <functional>
#include <set>

namespace wvd::authoring {
// 作者契约只保存声明和值；运行时的作用域令牌不从 JSON 读取。
inline void validate_event_resume(const Json &resume, const std::string &id) {
    if (!resume.is_object() || !resume.contains("mode")) contract_error("EVENT_RESUME_INVALID", id);
    for (const auto &[key, value] : resume.items()) {
        (void)value;
        if (key != "mode" && key != "node_id" && key != "guard")
            contract_error("EVENT_RESUME_FIELD", id + ":" + key);
    }
    const auto mode = resume.at("mode").get<std::string>();
    if (mode == "reobserve") {
        if (resume.size() != 1) contract_error("EVENT_RESUME_INVALID", id);
    } else if (mode == "replan") {
        if (resume.size() != 3 || !resume.at("node_id").is_string() ||
            !public_id(resume.at("node_id").get<std::string>()) || !resume.at("guard").is_object())
            contract_error("EVENT_RESUME_INVALID", id);
    } else contract_error("EVENT_RESUME_INVALID", id);
}

inline void validate_event_policy(const Json &document) {
    const auto rules = document.at("execution").value("events", Json::object());
    if (!rules.is_object() || rules.size() > 32) contract_error("EVENT_RULES_INVALID");
    for (const auto &[id, rule] : rules.items()) {
        if (!public_id(id) || id.size() > 64 || !rule.is_object())
            contract_error("EVENT_RULE_INVALID", id);
        for (const auto &[key, value] : rule.items()) {
            (void)value;
            if (key != "enabled" && key != "class" && key != "priority" && key != "detect" &&
                key != "handler" && key != "resume" && key != "allow_nested" &&
                key != "disposition" && key != "reason")
                contract_error("EVENT_RULE_FIELD", id + ":" + key);
        }
        if (!rule.contains("enabled") || !rule.at("enabled").is_boolean() ||
            !rule.contains("class") || !rule.at("class").is_string() ||
            (rule.at("class") != "overlay" && rule.at("class") != "encounter") ||
            !rule.contains("priority") || !rule.at("priority").is_number_integer() ||
            rule.at("priority").get<int>() < 0 || rule.at("priority").get<int>() > 1000 ||
            !rule.contains("detect") || !rule.at("detect").is_object())
            contract_error("EVENT_RULE_INVALID", id);
        const auto disposition = rule.value("disposition", std::string("handled"));
        if (disposition == "handled") {
            if (!rule.contains("handler") || rule.contains("reason")) contract_error("EVENT_HANDLER_REQUIRED", id);
            validate_call(rule.at("handler"));
            if (rule.contains("resume")) validate_event_resume(rule.at("resume"), id);
        } else if (disposition == "external_blocked") {
            if (rule.contains("handler") || rule.contains("resume") || !rule.contains("reason") ||
                !rule.at("reason").is_string() || rule.at("reason").get<std::string>().empty())
                contract_error("EVENT_BLOCKED_INVALID", id);
        } else contract_error("EVENT_DISPOSITION_INVALID", id);
        const auto nested = rule.value("allow_nested", Json::array());
        if (!nested.is_array() || nested.size() > 16) contract_error("EVENT_NESTED_INVALID", id);
        std::set<std::string> unique;
        for (const auto &child : nested) {
            if (!child.is_string() || !public_id(child.get<std::string>()) ||
                !unique.insert(child.get<std::string>()).second || child == id)
                contract_error("EVENT_NESTED_INVALID", id);
        }
    }
    for (const auto &node : document.at("nodes")) {
        const auto id = node.at("id").get<std::string>();
        const auto overrides = node.value("event_overrides", Json::object());
        const auto resumes = node.value("resume", Json::object());
        if (!overrides.is_object() || !resumes.is_object()) contract_error("EVENT_NODE_POLICY_INVALID", id);
        for (const auto &[event_id, override] : overrides.items()) {
            if (!public_id(event_id) || !override.is_object() || override.empty())
                contract_error("EVENT_OVERRIDE_INVALID", id + ":" + event_id);
            for (const auto &[key, value] : override.items()) {
                if (key == "enabled") {
                    if (!value.is_boolean()) contract_error("EVENT_OVERRIDE_INVALID", id + ":" + event_id);
                } else if (key == "arguments") {
                    (void)arguments(value);
                } else contract_error("EVENT_OVERRIDE_FIELD", id + ":" + key);
            }
        }
        for (const auto &[event_id, resume] : resumes.items()) {
            if (!public_id(event_id)) contract_error("EVENT_RESUME_UNKNOWN", id + ":" + event_id);
            validate_event_resume(resume, id + ":" + event_id);
        }
    }
}

inline Json effective_event_policy(const Json &inherited, const Json &document, const Json &node) {
    Json result = inherited;
    const auto declared = document.at("execution").value("events", Json::object());
    const auto overrides = node.value("event_overrides", Json::object());
    const auto resumes = node.value("resume", Json::object());
    for (const auto &[id, rule] : declared.items())
        result[id] = rule;
    for (const auto &[id, value] : overrides.items()) {
        if (!result.contains(id)) contract_error("EVENT_OVERRIDE_UNKNOWN", id);
        for (const auto &[key, override] : value.items()) {
            if (key == "arguments") result[id]["handler"]["arguments"] = override;
            else result[id][key] = override;
        }
    }
    for (const auto &[id, resume] : resumes.items())
        if (!result.contains(id)) contract_error("EVENT_RESUME_UNKNOWN", id);
    for (const auto &[id, resume] : resumes.items())
        result[id]["resume"] = resume;
    return result;
}

struct NestedEventPolicy {
    Json rules = Json::object();
    std::set<std::string> direct;
};

inline NestedEventPolicy nested_event_policy(const Json &effective, const std::string &event_id) {
    NestedEventPolicy result;
    std::set<std::string> visiting{event_id};
    const auto include = [&](auto &&self, const std::string &key) -> void {
        if (!visiting.insert(key).second)
            contract_error("EVENT_NESTED_CYCLE", event_id + ":" + key);
        if (!effective.contains(key) || !effective.at(key).value("enabled", false))
            contract_error("EVENT_NESTED_UNKNOWN", event_id + ":" + key);
        if (!result.rules.contains(key)) {
            result.rules[key] = effective.at(key);
            for (const auto &child : effective.at(key).value("allow_nested", Json::array()))
                self(self, child.get<std::string>());
        }
        visiting.erase(key);
    };
    for (const auto &child : effective.at(event_id).value("allow_nested", Json::array())) {
        const auto key = child.get<std::string>();
        result.direct.insert(key);
        include(include, key);
    }
    return result;
}
} // namespace wvd::authoring
