#include "workflow_validator.hpp"
#include "document_parameters.hpp"
#include "event_policy.hpp"
#include "resource_locale.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

namespace wvd::authoring {
namespace {
using J = nlohmann::json;
[[noreturn]] void fail(const std::string &code, const std::string &identity = {}) {
    throw std::runtime_error(identity.empty() ? code : code + ":" + identity);
}

bool identifier(const std::string &value) {
    if (value.empty() || value.size() > 64 ||
        !std::isalnum(static_cast<unsigned char>(value.front())))
        return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '_' || c == '-';
    });
}

void exact_object(const J &value, const std::set<std::string> &required,
                  const std::set<std::string> &optional, const std::string &code,
                  const std::string &identity = {}) {
    if (!value.is_object())
        fail(code, identity);
    for (const auto &key : required)
        if (!value.contains(key))
            fail(code, identity + (identity.empty() ? "" : ":") + key);
    for (const auto &[key, child] : value.items()) {
        (void)child;
        if (!required.contains(key) && !optional.contains(key))
            fail(code, identity + (identity.empty() ? "" : ":") + key);
    }
}

std::int64_t integer(const J &value, std::int64_t low, std::int64_t high,
                     const std::string &code, const std::string &identity) {
    if (!value.is_number_integer())
        fail(code, identity);
    if (value.is_number_unsigned()) {
        const auto number = value.get<std::uint64_t>();
        if (high < 0 || number > static_cast<std::uint64_t>(high) ||
            (low > 0 && number < static_cast<std::uint64_t>(low)))
            fail(code, identity);
        return static_cast<std::int64_t>(number);
    }
    const auto number = value.get<std::int64_t>();
    if (number < low || number > high)
        fail(code, identity);
    return number;
}

void bounded_text(const J &value, std::size_t low, std::size_t high,
                  const std::string &code, const std::string &identity) {
    if (!value.is_string())
        fail(code, identity);
    const auto size = value.get_ref<const std::string &>().size();
    if (size < low || size > high)
        fail(code, identity);
}

void validate_roi(const J &value, const std::string &identity) {
    if (!value.is_array() || value.size() != 4)
        fail("AUTHOR_ROI_INVALID", identity);
    const auto x = integer(value[0], 0, 899, "AUTHOR_ROI_INVALID", identity);
    const auto y = integer(value[1], 0, 1599, "AUTHOR_ROI_INVALID", identity);
    const auto width = integer(value[2], 1, 900, "AUTHOR_ROI_INVALID", identity);
    const auto height = integer(value[3], 1, 1600, "AUTHOR_ROI_INVALID", identity);
    if (x + width > 900 || y + height > 1600)
        fail("AUTHOR_ROI_INVALID", identity);
}

void validate_point(const J &value, const std::string &identity) {
    if (!value.is_array() || value.size() != 2)
        fail("AUTHOR_POINT_INVALID", identity);
    integer(value[0], 1, 898, "AUTHOR_POINT_INVALID", identity);
    integer(value[1], 1, 1598, "AUTHOR_POINT_INVALID", identity);
}

void validate_image(const J &value, const std::string &identity) {
    bounded_text(value, 1, 160, "AUTHOR_IMAGE_INVALID", identity);
    const auto &path = value.get_ref<const std::string &>();
    if (path.front() == '/' || path.find(':') != std::string::npos ||
        path.find('\\') != std::string::npos || path.find("..") != std::string::npos)
        fail("AUTHOR_IMAGE_INVALID", identity);
}

void validate_bounded_json(const J &value, const std::string &identity,
                           unsigned depth = 0, std::size_t *count = nullptr) {
    std::size_t local_count{};
    if (!count)
        count = &local_count;
    if (++*count > 4096 || depth > 8)
        fail("AUTHOR_JSON_VALUE_LIMIT", identity);
    if (value.is_string() && value.get_ref<const std::string &>().size() > 4096)
        fail("AUTHOR_JSON_VALUE_LIMIT", identity);
    if (value.is_array())
        for (const auto &child : value)
            validate_bounded_json(child, identity, depth + 1, count);
    if (value.is_object()) {
        if (value.size() > 256)
            fail("AUTHOR_JSON_VALUE_LIMIT", identity);
        for (const auto &[key, child] : value.items()) {
            if (key.empty() || key.size() > 128)
                fail("AUTHOR_JSON_VALUE_LIMIT", identity);
            validate_bounded_json(child, identity, depth + 1, count);
        }
    }
}

void validate_condition(const J &condition, const std::string &node_id,
                        unsigned depth = 0, std::size_t *count = nullptr) {
    std::size_t local_count{};
    if (!count)
        count = &local_count;
    if (++*count > 256 || depth > 8)
        fail("AUTHOR_CONDITION_LIMIT", node_id);
    if (!condition.is_object() || !condition.contains("mode") ||
        !condition.at("mode").is_string())
        fail("AUTHOR_CONDITION_INVALID", node_id);
    const auto mode = condition.at("mode").is_string()
                          ? condition.at("mode").get<std::string>()
                          : std::string{};
    if (mode == "semantic") {
        exact_object(condition, {"mode", "id"}, {}, "AUTHOR_SEMANTIC_INVALID", node_id);
        if (!authoring::public_id(condition.at("id").get<std::string>()))
            fail("AUTHOR_SEMANTIC_INVALID", node_id);
        return;
    }
    if (mode == "location") {
        exact_object(condition, {"mode", "id"}, {}, "AUTHOR_LOCATION_INVALID", node_id);
        integer(condition.at("id"), 0, 65535, "AUTHOR_LOCATION_INVALID", node_id);
        return;
    }
    if (mode == "bright_mask") {
        exact_object(condition, {"mode", "image"}, {"threshold", "roi", "min_brightness"},
                     "AUTHOR_CONDITION_INVALID", node_id);
        validate_image(condition.at("image"), node_id);
        if (condition.contains("roi")) validate_roi(condition.at("roi"), node_id);
        if (condition.contains("min_brightness"))
            integer(condition.at("min_brightness"), 0, 255, "AUTHOR_MASK_INVALID", node_id);
        if (condition.contains("threshold")) {
            if (!condition.at("threshold").is_number()) fail("AUTHOR_THRESHOLD_INVALID", node_id);
            const auto n = condition.at("threshold").get<double>();
            if (!std::isfinite(n) || n < 0 || n > 1) fail("AUTHOR_THRESHOLD_INVALID", node_id);
        }
        return;
    }
    if (mode == "template") {
        exact_object(condition, {"mode", "image"}, {"threshold", "roi", "grayscale"},
                     "AUTHOR_CONDITION_INVALID", node_id);
        validate_image(condition.at("image"), node_id);
        if (condition.contains("grayscale") && !condition.at("grayscale").is_boolean())
            fail("AUTHOR_CONDITION_INVALID", node_id);
        if (condition.contains("threshold")) {
            if (!condition.at("threshold").is_number())
                fail("AUTHOR_THRESHOLD_INVALID", node_id);
            const auto threshold = condition.at("threshold").get<double>();
            if (!std::isfinite(threshold) || threshold < 0 || threshold > 1)
                fail("AUTHOR_THRESHOLD_INVALID", node_id);
        }
        if (condition.contains("roi"))
            validate_roi(condition.at("roi"), node_id);
        return;
    }
    if (mode == "ocr") {
        exact_object(condition, {"mode", "expected"}, {"roi"},
                     "AUTHOR_CONDITION_INVALID", node_id);
        const auto &expected = condition.at("expected");
        if (!expected.is_array() || expected.empty() || expected.size() > 32)
            fail("AUTHOR_OCR_EXPECTED_INVALID", node_id);
        for (const auto &text : expected)
            bounded_text(text, 1, 128, "AUTHOR_OCR_EXPECTED_INVALID", node_id);
        if (condition.contains("roi"))
            validate_roi(condition.at("roi"), node_id);
        return;
    }
    if (mode == "all" || mode == "any" || mode == "not") {
        exact_object(condition, {"mode", "conditions"}, {},
                     "AUTHOR_CONDITION_INVALID", node_id);
        const auto &children = condition.at("conditions");
        if (!children.is_array() || children.empty() || children.size() > 32 ||
            (mode == "not" && children.size() != 1))
            fail("AUTHOR_CONDITION_INVALID", node_id);
        for (const auto &child : children)
            validate_condition(child, node_id, depth + 1, count);
        return;
    }
    static const std::set<std::string> builtins{
        "auto_route_moving", "auto_route_post", "blocking_screen", "boot_post",
        "boot_ready", "combat_active", "dark_light_clear", "dark_light_post",
        "default_dialogue", "dialogue_post", "fast_forward_off", "fishing_bait_empty",
        "fishing_reward", "fishing_unknown", "map_route_post", "mining_reward",
        "movement_stopped", "next_low_confidence", "party_death", "party_defeat",
        "pause", "pause_negative", "special_dialogue", "special_dialogue_post",
        "target_marker", "task_stop"};
    if (!builtins.contains(mode) || condition.size() != 1)
        fail("AUTHOR_CONDITION_UNSUPPORTED", node_id + ":" + mode);
}

void validate_action_parameters(const J &parameters, const std::string &node_id) {
    if (!parameters.is_object() || !parameters.contains("operation") ||
        !parameters.at("operation").is_string())
        fail("AUTHOR_ACTION_PARAMETERS_INVALID", node_id);
    const auto operation = parameters.at("operation").get<std::string>();
    const std::set<std::string> common_optional{
        "allowed_area", "postcondition_timeout_ms", "delay_after_ms"};
    auto validate_common = [&] {
        if (parameters.contains("allowed_area"))
            validate_roi(parameters.at("allowed_area"), node_id);
        if (parameters.contains("postcondition_timeout_ms"))
            integer(parameters.at("postcondition_timeout_ms"), 1, 60000,
                    "AUTHOR_ACTION_BUDGET_INVALID", node_id);
        if (parameters.contains("delay_after_ms"))
            integer(parameters.at("delay_after_ms"), 0, 10000,
                    "AUTHOR_ACTION_DELAY_INVALID", node_id);
    };
    if (operation == "click") {
        exact_object(parameters, {"operation", "scene", "target", "postcondition"},
                     {"offset", "allowed_area", "postcondition_timeout_ms", "delay_after_ms"},
                     "AUTHOR_ACTION_PARAMETERS_INVALID", node_id);
        validate_condition(parameters.at("scene"), node_id);
        validate_condition(parameters.at("target"), node_id);
        validate_condition(parameters.at("postcondition"), node_id);
        if (parameters.contains("offset")) {
            const auto &offset = parameters.at("offset");
            if (!offset.is_array() || offset.size() != 2)
                fail("AUTHOR_OFFSET_INVALID", node_id);
            integer(offset[0], -900, 900, "AUTHOR_OFFSET_INVALID", node_id);
            integer(offset[1], -1600, 1600, "AUTHOR_OFFSET_INVALID", node_id);
        }
    } else if (operation == "fixed_click") {
        exact_object(parameters, {"operation", "scene", "postcondition", "position"},
                     common_optional, "AUTHOR_ACTION_PARAMETERS_INVALID", node_id);
        validate_condition(parameters.at("scene"), node_id);
        validate_condition(parameters.at("postcondition"), node_id);
        validate_point(parameters.at("position"), node_id);
    } else if (operation == "back") {
        exact_object(parameters, {"operation", "scene", "postcondition"},
                     common_optional, "AUTHOR_ACTION_PARAMETERS_INVALID", node_id);
        validate_condition(parameters.at("scene"), node_id);
        validate_condition(parameters.at("postcondition"), node_id);
    } else if (operation == "swipe") {
        exact_object(parameters,
                     {"operation", "scene", "postcondition", "coordinates", "duration_ms"},
                     common_optional, "AUTHOR_ACTION_PARAMETERS_INVALID", node_id);
        validate_condition(parameters.at("scene"), node_id);
        validate_condition(parameters.at("postcondition"), node_id);
        const auto &coordinates = parameters.at("coordinates");
        if (!coordinates.is_array() || coordinates.size() != 4)
            fail("AUTHOR_SWIPE_INVALID", node_id);
        for (std::size_t i = 0; i < coordinates.size(); ++i)
            integer(coordinates[i], 1, i % 2 == 0 ? 898 : 1598,
                    "AUTHOR_SWIPE_INVALID", node_id);
        integer(parameters.at("duration_ms"), 1, 60000,
                "AUTHOR_SWIPE_INVALID", node_id);
    } else {
        fail("AUTHOR_ACTION_UNSUPPORTED", node_id + ":" + operation);
    }
    validate_common();
}

void validate_business_parameters(const J &parameters, const std::string &node_id) {
    if (!parameters.is_object() || !parameters.contains("binding") ||
        !parameters.at("binding").is_string())
        fail("AUTHOR_BUSINESS_PARAMETERS_INVALID", node_id);
    const auto binding = parameters.at("binding").get<std::string>();
    if (binding == "confirm") {
        exact_object(parameters, {"binding", "operation_id", "event", "condition"},
                     {"expected_step", "delay_after_ms"},
                     "AUTHOR_BUSINESS_PARAMETERS_INVALID", node_id);
        bounded_text(parameters.at("operation_id"), 1, 128,
                     "AUTHOR_BUSINESS_OPERATION_INVALID", node_id);
        bounded_text(parameters.at("event"), 1, 96,
                     "AUTHOR_BUSINESS_EVENT_INVALID", node_id);
        validate_condition(parameters.at("condition"), node_id);
        if (parameters.contains("expected_step"))
            integer(parameters.at("expected_step"), 0, 4096,
                    "AUTHOR_BUSINESS_STEP_INVALID", node_id);
    } else if (binding == "combat") {
        // 作者节点表示完整遭遇战；角色轮次和策略参数只从本次冻结配置读取。
        exact_object(parameters, {"binding"}, {"delay_after_ms"},
                     "AUTHOR_BUSINESS_PARAMETERS_INVALID", node_id);
    } else if (binding == "chest") {
        exact_object(parameters, {"binding", "preferred"},
                     {"quick", "seed", "delay_after_ms"},
                     "AUTHOR_BUSINESS_PARAMETERS_INVALID", node_id);
        integer(parameters.at("preferred"), 0, 6,
                "AUTHOR_BUSINESS_ARGUMENTS_INVALID", node_id);
        if (parameters.contains("quick") && !parameters.at("quick").is_boolean())
            fail("AUTHOR_BUSINESS_ARGUMENTS_INVALID", node_id);
        if (parameters.contains("seed"))
            integer(parameters.at("seed"), 0, std::numeric_limits<unsigned>::max(),
                    "AUTHOR_BUSINESS_ARGUMENTS_INVALID", node_id);
    } else if (binding == "task_stage") {
        exact_object(parameters, {"binding", "task_id", "stage"}, {},
                     "AUTHOR_BUSINESS_PARAMETERS_INVALID", node_id);
        bounded_text(parameters.at("task_id"), 1, 64,
                     "AUTHOR_BUSINESS_ARGUMENTS_INVALID", node_id);
        bounded_text(parameters.at("stage"), 1, 32,
                     "AUTHOR_BUSINESS_ARGUMENTS_INVALID", node_id);
        const auto stage = parameters.at("stage").get<std::string>();
        if (stage != "prepare" && stage != "enter" && stage != "traverse")
            fail("AUTHOR_BUSINESS_ARGUMENTS_INVALID", node_id);
    } else {
        fail("AUTHOR_BUSINESS_UNSUPPORTED", node_id + ":" + binding);
    }
    if (parameters.contains("delay_after_ms"))
        integer(parameters.at("delay_after_ms"), 0, 10000,
                "AUTHOR_ACTION_DELAY_INVALID", node_id);
}

} // namespace

ValidatedGraph validate_graph(const J &document) {
    exact_object(document, {"schema", "flow", "entry", "nodes", "edges", "layout", "execution"},
                 {"revision"}, "AUTHOR_DOCUMENT_FIELDS_INVALID");
    if (document.at("schema") != 1)
        fail("AUTHOR_SCHEMA_INVALID");
    exact_object(document.at("flow"), {"id", "name", "description"}, {},
                 "AUTHOR_FLOW_FIELDS_INVALID");
    bounded_text(document.at("flow").at("id"), 1, 64, "AUTHOR_FLOW_ID_INVALID", "flow");
    if (!identifier(document.at("flow").at("id").get<std::string>()))
        fail("AUTHOR_FLOW_ID_INVALID", document.at("flow").at("id").get<std::string>());
    bounded_text(document.at("flow").at("name"), 1, 128, "AUTHOR_FLOW_NAME_INVALID", "flow");
    bounded_text(document.at("flow").at("description"), 0, 2048,
                 "AUTHOR_FLOW_DESCRIPTION_INVALID", "flow");
    bounded_text(document.at("entry"), 1, 64, "AUTHOR_ENTRY_INVALID", "entry");
    if (document.contains("revision")) {
        bounded_text(document.at("revision"), 64, 64, "AUTHOR_REVISION_INVALID", "revision");
        const auto &revision = document.at("revision").get_ref<const std::string &>();
        if (!std::all_of(revision.begin(), revision.end(), [](unsigned char c) {
                return std::isdigit(c) || (c >= 'a' && c <= 'f');
            }))
            fail("AUTHOR_REVISION_INVALID", "revision");
    }
    const auto &nodes = document.at("nodes");
    const auto &edges = document.at("edges");
    if (!nodes.is_array() || nodes.empty() || nodes.size() > 1024)
        fail("AUTHOR_NODE_COUNT_INVALID");
    if (!edges.is_array() || edges.size() > 4096)
        fail("AUTHOR_EDGE_COUNT_INVALID");

    ValidatedGraph graph;
    std::set<std::string> confirmation_operations;
    for (const auto &node : nodes) {
        exact_object(node, {"id", "type", "name", "parameters"}, {"repeat_limit", "event_overrides", "resume"},
                     "AUTHOR_NODE_FIELDS_INVALID");
        bounded_text(node.at("id"), 1, 64, "AUTHOR_NODE_ID_INVALID", "node");
        const auto id = node.at("id").get<std::string>();
        if (!identifier(id))
            fail("AUTHOR_NODE_ID_INVALID", id);
        if (!graph.nodes.emplace(id, &node).second)
            fail("AUTHOR_NODE_DUPLICATE", id);
        bounded_text(node.at("name"), 1, 128, "AUTHOR_NODE_NAME_INVALID", id);
        if (!node.at("type").is_string())
            fail("AUTHOR_NODE_TYPE_INVALID", id);
        if (node.contains("repeat_limit"))
            integer(node.at("repeat_limit"), 1, 256, "AUTHOR_NODE_REPEAT_INVALID", id);
        const auto type = node.at("type").get<std::string>();
        if (type == "route") {
            exact_object(node.at("parameters"), {}, {}, "AUTHOR_ROUTE_PARAMETERS_INVALID", id);
        } else if (type == "recognition") {
            exact_object(node.at("parameters"), {"condition"}, {"delay_after_ms"},
                         "AUTHOR_RECOGNITION_PARAMETERS_INVALID", id);
            validate_condition(node.at("parameters").at("condition"), id);
            if (node.at("parameters").contains("delay_after_ms"))
                integer(node.at("parameters").at("delay_after_ms"), 0, 10000,
                        "AUTHOR_ACTION_DELAY_INVALID", id);
        } else if (type == "action") {
            validate_action_parameters(node.at("parameters"), id);
        } else if (type == "wait") {
            exact_object(node.at("parameters"), {"duration_ms"}, {},
                         "AUTHOR_WAIT_PARAMETERS_INVALID", id);
            integer(node.at("parameters").at("duration_ms"), 1, 10000,
                    "AUTHOR_WAIT_DURATION_INVALID", id);
        } else if (type == "call") {
            authoring::validate_call(node.at("parameters"));
        } else if (type == "slot") {
            const auto &p = node.at("parameters");
            exact_object(p, {"name"}, {"calls"}, "AUTHOR_SLOT_INVALID", id);
            if (!authoring::public_id(p.at("name").get<std::string>())) fail("AUTHOR_SLOT_INVALID", id);
            const auto calls = p.value("calls", J::array());
            if (!calls.is_array() || calls.size() > 32) fail("AUTHOR_SLOT_INVALID", id);
            for (const auto &call : calls) authoring::validate_call(call);
        } else if (type == "business") {
            validate_business_parameters(node.at("parameters"), id);
            if (node.at("parameters").at("binding") == "confirm") {
                const auto operation = node.at("parameters").at("operation_id").get<std::string>();
                if (!confirmation_operations.insert(operation).second)
                    fail("AUTHOR_BUSINESS_OPERATION_DUPLICATE", id + ":" + operation);
            }
        } else if (type == "end") {
            exact_object(node.at("parameters"), {"outcome"}, {"reason"},
                         "AUTHOR_END_PARAMETERS_INVALID", id);
            if (!node.at("parameters").at("outcome").is_string())
                fail("AUTHOR_END_OUTCOME_INVALID", id);
            const auto outcome = node.at("parameters").at("outcome").get<std::string>();
            if (outcome == "success") {
                if (node.at("parameters").contains("reason") || !graph.success_end.empty())
                    fail("AUTHOR_SUCCESS_END_INVALID", id);
                graph.success_end = id;
            } else if (outcome == "failure") {
                if (!node.at("parameters").contains("reason"))
                    fail("AUTHOR_FAILURE_END_INVALID", id);
                bounded_text(node.at("parameters").at("reason"), 1, 256,
                             "AUTHOR_FAILURE_END_INVALID", id);
            } else {
                fail("AUTHOR_END_OUTCOME_INVALID", id);
            }
        } else {
            fail("AUTHOR_NODE_TYPE_INVALID", id + ":" + type);
        }
        if (node.at("parameters").dump().size() > 65536)
            fail("AUTHOR_NODE_PARAMETERS_TOO_LARGE", id);
    }
    if (graph.success_end.empty())
        fail("AUTHOR_SUCCESS_END_MISSING");
    const auto entry = document.at("entry").get<std::string>();
    if (!graph.nodes.contains(entry))
        fail("AUTHOR_ENTRY_UNKNOWN", entry);
    if (graph.nodes.at(entry)->at("type") == "end")
        fail("AUTHOR_ENTRY_END_INVALID", entry);

    std::set<std::string> edge_ids;
    std::map<std::pair<std::string, std::string>, std::map<int, const J *>> ordered;
    for (const auto &edge : edges) {
        exact_object(edge, {"id", "from", "to", "outcome", "order"}, {},
                     "AUTHOR_EDGE_FIELDS_INVALID");
        for (const auto *key : {"id", "from", "to", "outcome"})
            if (!edge.at(key).is_string())
                fail("AUTHOR_EDGE_FIELDS_INVALID", key);
        const auto edge_id = edge.at("id").get<std::string>();
        if (!identifier(edge_id))
            fail("AUTHOR_EDGE_ID_INVALID", edge_id);
        if (!edge_ids.insert(edge_id).second)
            fail("AUTHOR_EDGE_DUPLICATE", edge_id);
        const auto from = edge.at("from").get<std::string>();
        const auto to = edge.at("to").get<std::string>();
        if (!graph.nodes.contains(from))
            fail("AUTHOR_EDGE_DANGLING", edge_id + ":from:" + from);
        if (!graph.nodes.contains(to))
            fail("AUTHOR_EDGE_DANGLING", edge_id + ":to:" + to);
        if (graph.nodes.at(from)->at("type") == "end")
            fail("AUTHOR_EDGE_FROM_END", edge_id + ":" + from);
        const auto outcome = edge.at("outcome").get<std::string>();
        if (outcome != "success" && outcome != "failure")
            fail("AUTHOR_EDGE_OUTCOME_INVALID", edge_id);
        const auto order = static_cast<int>(integer(edge.at("order"), 0, 4095,
                                                    "AUTHOR_EDGE_ORDER_INVALID", edge_id));
        const auto [position, inserted] = ordered[{from, outcome}].emplace(order, &edge);
        if (!inserted) {
            (void)position;
            fail("AUTHOR_EDGE_ORDER_DUPLICATE", edge_id + ":" + from + ":" + outcome);
        }
    }
    for (const auto &[group, sequence] : ordered) {
        int expected{};
        for (const auto &[order, edge] : sequence) {
            if (order != expected++)
                fail("AUTHOR_EDGE_ORDER_GAP", edge->at("id").get<std::string>() + ":" + group.first);
            (group.second == "success" ? graph.success[group.first] : graph.failure[group.first])
                .push_back(edge);
        }
    }
    for (const auto &[id, node] : graph.nodes) {
        // 后续可达性检查和编译都按每个节点读取成功/失败出口。
        // 即使作者没有配置失败边，也必须保留一个空集合，不能让 map::at 抛异常。
        (void)graph.success[id];
        (void)graph.failure[id];
        const auto type = node->at("type").get<std::string>();
        if (type != "end" && graph.success[id].empty())
            fail("AUTHOR_NODE_SUCCESS_EDGE_MISSING", id);
        if (type == "wait" && !graph.failure[id].empty())
            fail("AUTHOR_WAIT_FAILURE_EDGE_UNSUPPORTED",
                 graph.failure[id].front()->at("id").get<std::string>() + ":" + id);
        if (type == "end" && (!graph.success[id].empty() || !graph.failure[id].empty()))
            fail("AUTHOR_EDGE_FROM_END", id);
    }

    std::set<std::string> reached;
    std::function<void(const std::string &)> visit = [&](const std::string &id) {
        if (!reached.insert(id).second)
            return;
        for (const auto *outcome : {&graph.success, &graph.failure})
            for (const auto *edge : outcome->at(id))
                visit(edge->at("to").get<std::string>());
    };
    visit(entry);
    for (const auto &[id, node] : graph.nodes) {
        (void)node;
        if (!reached.contains(id))
            fail("AUTHOR_NODE_UNREACHABLE", id);
    }

    std::map<std::string, int> state;
    std::vector<std::string> stack;
    std::function<void(const std::string &)> cycle = [&](const std::string &id) {
        state[id] = 1;
        stack.push_back(id);
        for (const auto *outcome : {&graph.success, &graph.failure})
            for (const auto *edge : outcome->at(id)) {
                const auto target = edge->at("to").get<std::string>();
                if (state[target] == 0)
                    cycle(target);
                else if (state[target] == 1) {
                    const auto first = std::find(stack.begin(), stack.end(), target);
                    for (auto current = first; current != stack.end(); ++current)
                        if (!graph.nodes.at(*current)->contains("repeat_limit"))
                            fail("AUTHOR_LOOP_UNBOUNDED", *current + ":" +
                                 edge->at("id").get<std::string>());
                }
            }
        stack.pop_back();
        state[id] = 2;
    };
    cycle(entry);

    exact_object(document.at("layout"), {"nodes", "viewport"}, {},
                 "AUTHOR_LAYOUT_FIELDS_INVALID");
    const auto &layout_nodes = document.at("layout").at("nodes");
    if (!layout_nodes.is_array() || layout_nodes.size() != graph.nodes.size())
        fail("AUTHOR_LAYOUT_NODE_COUNT_INVALID");
    std::set<std::string> positioned;
    for (const auto &position : layout_nodes) {
        exact_object(position, {"node_id", "x", "y"}, {}, "AUTHOR_LAYOUT_NODE_INVALID");
        if (!position.at("node_id").is_string())
            fail("AUTHOR_LAYOUT_NODE_INVALID");
        const auto id = position.at("node_id").get<std::string>();
        if (!graph.nodes.contains(id) || !positioned.insert(id).second)
            fail("AUTHOR_LAYOUT_NODE_INVALID", id);
        for (const auto *axis : {"x", "y"}) {
            if (!position.at(axis).is_number())
                fail("AUTHOR_LAYOUT_COORDINATE_INVALID", id);
            const auto value = position.at(axis).get<double>();
            if (!std::isfinite(value) || std::abs(value) > 1000000)
                fail("AUTHOR_LAYOUT_COORDINATE_INVALID", id);
        }
    }
    const auto &viewport = document.at("layout").at("viewport");
    exact_object(viewport, {"x", "y", "zoom"}, {}, "AUTHOR_VIEWPORT_INVALID");
    for (const auto *key : {"x", "y", "zoom"})
        if (!viewport.at(key).is_number() || !std::isfinite(viewport.at(key).get<double>()))
            fail("AUTHOR_VIEWPORT_INVALID", key);
    if (std::abs(viewport.at("x").get<double>()) > 1000000 ||
        std::abs(viewport.at("y").get<double>()) > 1000000 ||
        viewport.at("zoom").get<double>() < .05 || viewport.at("zoom").get<double>() > 8)
        fail("AUTHOR_VIEWPORT_INVALID");

    exact_object(document.at("execution"), {"time_limit_ms"}, {"resource_locale", "events"},
                 "AUTHOR_EXECUTION_FIELDS_INVALID");
    try {
    authoring::validate_event_policy(document);
    } catch (const nlohmann::json::exception &error) {
        fail("AUTHOR_EVENT_POLICY_JSON_INVALID", error.what());
    }
    const auto event_rules = document.at("execution").value("events", J::object());
    for (const auto &[event_id, rule] : event_rules.items()) {
        try {
        validate_condition(rule.at("detect"), "event:" + event_id);
        if (rule.contains("resume") && rule.at("resume").value("mode", "") == "replan") {
            const auto target = rule.at("resume").at("node_id").get<std::string>();
            if (!graph.nodes.contains(target) || graph.nodes.at(target)->at("type") == "end")
                fail("EVENT_REPLAN_NODE_INVALID", event_id + ":" + target);
            validate_condition(rule.at("resume").at("guard"), "event:" + event_id);
        }
        } catch (const nlohmann::json::exception &error) {
            fail("AUTHOR_EVENT_CONDITION_JSON_INVALID", event_id + ":" + error.what());
        }
    }
    for (const auto &node : document.at("nodes")) {
        const auto resumes = node.value("resume", J::object());
        for (const auto &[event_id, resume] : resumes.items()) {
            try {
            if (resume.value("mode", "") != "replan") continue;
            const auto target = resume.at("node_id").get<std::string>();
            if (!graph.nodes.contains(target) || graph.nodes.at(target)->at("type") == "end")
                fail("EVENT_REPLAN_NODE_INVALID", event_id + ":" + target);
            validate_condition(resume.at("guard"), node.at("id").get<std::string>());
            } catch (const nlohmann::json::exception &error) {
                fail("AUTHOR_EVENT_NODE_RESUME_JSON_INVALID", node.at("id").get<std::string>() + ":" + event_id + ":" + error.what());
            }
        }
    }
    if (document.at("execution").contains("resource_locale")) {
        const auto locale = document.at("execution").at("resource_locale").get<std::string>();
        authoring::validate_resource_locale(locale);
    }
    integer(document.at("execution").at("time_limit_ms"), 1, 1800000,
            "AUTHOR_EXECUTION_BUDGET_INVALID", "execution");
    return graph;
}

void validate_author_workflow(const J &document) {
    (void)validate_graph(instantiate_document(document));
    if (document.dump().size() > 1048576)
        fail("AUTHOR_DOCUMENT_TOO_LARGE");
}
} // namespace wvd::authoring
