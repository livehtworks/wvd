#include "author_workflow.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

namespace wvd::games::tasks {
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
        if (low < 0 || number > static_cast<std::uint64_t>(high))
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
    if (mode == "template") {
        exact_object(condition, {"mode", "image"}, {"threshold", "roi"},
                     "AUTHOR_CONDITION_INVALID", node_id);
        validate_image(condition.at("image"), node_id);
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
        exact_object(parameters, {"binding", "condition", "arguments"},
                     {"delay_after_ms"}, "AUTHOR_BUSINESS_PARAMETERS_INVALID", node_id);
        validate_condition(parameters.at("condition"), node_id);
        if (!parameters.at("arguments").is_object())
            fail("AUTHOR_BUSINESS_ARGUMENTS_INVALID", node_id);
        const auto &arguments = parameters.at("arguments");
        validate_bounded_json(arguments, node_id);
        if (arguments.dump().size() > 65536)
            fail("AUTHOR_BUSINESS_ARGUMENTS_INVALID", node_id);
        if (!arguments.contains("operation") || !arguments.at("operation").is_string())
            fail("AUTHOR_BUSINESS_ARGUMENTS_INVALID", node_id);
        const auto operation = arguments.at("operation").get<std::string>();
        if (operation == "prepare") {
            exact_object(arguments, {"operation", "portraits", "catalog"}, {},
                         "AUTHOR_BUSINESS_ARGUMENTS_INVALID", node_id);
            if (!arguments.at("portraits").is_array() || arguments.at("portraits").size() > 128 ||
                !arguments.at("catalog").is_array() || arguments.at("catalog").size() > 128)
                fail("AUTHOR_BUSINESS_ARGUMENTS_INVALID", node_id);
            for (const auto &portrait : arguments.at("portraits")) {
                exact_object(portrait, {"image", "role"}, {},
                             "AUTHOR_BUSINESS_ARGUMENTS_INVALID", node_id);
                validate_image(portrait.at("image"), node_id);
                bounded_text(portrait.at("role"), 1, 128,
                             "AUTHOR_BUSINESS_ARGUMENTS_INVALID", node_id);
            }
        } else if (operation == "success" || operation == "auto_confirmed") {
            exact_object(arguments, {"operation", "index"}, {},
                         "AUTHOR_BUSINESS_ARGUMENTS_INVALID", node_id);
            integer(arguments.at("index"), 0, 127,
                    "AUTHOR_BUSINESS_ARGUMENTS_INVALID", node_id);
        } else {
            fail("AUTHOR_BUSINESS_ARGUMENTS_INVALID", node_id + ":" + operation);
        }
    } else if (binding == "chest") {
        exact_object(parameters, {"binding", "condition", "preferred", "seed"},
                     {"delay_after_ms"}, "AUTHOR_BUSINESS_PARAMETERS_INVALID", node_id);
        validate_condition(parameters.at("condition"), node_id);
        integer(parameters.at("preferred"), 0, 6,
                "AUTHOR_BUSINESS_ARGUMENTS_INVALID", node_id);
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

struct ValidatedGraph {
    std::map<std::string, const J *> nodes;
    std::map<std::string, std::vector<const J *>> success;
    std::map<std::string, std::vector<const J *>> failure;
    std::string success_end;
};

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
        exact_object(node, {"id", "type", "name", "parameters"}, {"repeat_limit"},
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
        if (type == "recognition") {
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

    exact_object(document.at("execution"), {"time_limit_ms"}, {},
                 "AUTHOR_EXECUTION_FIELDS_INVALID");
    integer(document.at("execution").at("time_limit_ms"), 1, 1800000,
            "AUTHOR_EXECUTION_BUDGET_INVALID", "execution");
    return graph;
}

std::string pipeline_name(const std::string &node_id, const std::string &entry,
                          const std::string &success_end) {
    if (node_id == entry)
        return "Entry";
    if (node_id == success_end)
        return "Terminal";
    return "Author_" + node_id;
}

J successors(const std::vector<const J *> &edges, const std::string &entry,
             const std::string &success_end) {
    J result = J::array();
    for (const auto *edge : edges)
        result.push_back(pipeline_name(edge->at("to").get<std::string>(), entry, success_end));
    return result;
}
} // namespace

void validate_author_workflow(const J &document) {
    (void)validate_graph(document);
    if (document.dump().size() > 1048576)
        fail("AUTHOR_DOCUMENT_TOO_LARGE");
}

AuthorWorkflowCompilation compile_author_workflow(const J &document,
                                                  AuthorBusinessResolver resolver) {
    validate_author_workflow(document);
    const auto graph = validate_graph(document);
    const auto entry = document.at("entry").get<std::string>();
    const auto flow_id = document.at("flow").at("id").get<std::string>();
    const auto budget = std::chrono::milliseconds{
        document.at("execution").at("time_limit_ms").get<std::int64_t>()};
    PipelineCompiler compiler("author." + flow_id, budget);
    AuthorWorkflowCompilation result;

    for (const auto &[id, node] : graph.nodes) {
        const auto runtime_name = pipeline_name(id, entry, graph.success_end);
        result.node_to_pipeline[id] = {runtime_name};
        if (!result.pipeline_to_node.emplace(runtime_name, id).second)
            fail("AUTHOR_PIPELINE_MAPPING_DUPLICATE", id + ":" + runtime_name);
        if (id == graph.success_end)
            continue;
        const auto next = successors(graph.success.at(id), entry, graph.success_end);
        const auto &parameters = node->at("parameters");
        const auto type = node->at("type").get<std::string>();
        try {
            if (type == "recognition") {
                const auto &condition = parameters.at("condition");
                if (condition.value("mode", "") == "ocr")
                    compiler.observe_ocr(runtime_name,
                                         condition.at("expected").get<std::vector<std::string>>(),
                                         condition.value("roi", J::array({0, 0, 900, 1600})), next);
                else
                    compiler.observe(runtime_name, condition, next);
            } else if (type == "action") {
                const auto operation = parameters.at("operation").get<std::string>();
                if (operation == "click")
                    compiler.click(runtime_name, parameters.at("scene"), parameters.at("target"),
                                   parameters.at("postcondition"), next,
                                   parameters.value("offset", J::array({0, 0})));
                else if (operation == "fixed_click")
                    compiler.fixed_click(runtime_name, parameters.at("scene"),
                                         parameters.at("postcondition"),
                                         parameters.at("position"), next);
                else if (operation == "back")
                    compiler.back(runtime_name, parameters.at("scene"),
                                  parameters.at("postcondition"), next);
                else if (operation == "swipe")
                    compiler.swipe(runtime_name, parameters.at("scene"),
                                   parameters.at("postcondition"),
                                   parameters.at("coordinates"), next,
                                   static_cast<int>(parameters.at("duration_ms").get<std::int64_t>()));
                else
                    fail("AUTHOR_ACTION_UNSUPPORTED", id + ":" + operation);
                if (parameters.contains("allowed_area"))
                    compiler.allowed_area(runtime_name, parameters.at("allowed_area"));
                if (parameters.contains("postcondition_timeout_ms"))
                    compiler.postcondition_budget(
                        runtime_name,
                        static_cast<int>(parameters.at("postcondition_timeout_ms").get<std::int64_t>()));
            } else if (type == "wait") {
                // 等待由可取消的原生动作持有，不能交给 Maa post_delay 阻塞停止链。
                compiler.wait(runtime_name,
                              static_cast<int>(parameters.at("duration_ms").get<std::int64_t>()),
                              next);
            } else if (type == "business") {
                const auto binding = parameters.at("binding").get<std::string>();
                if (binding == "confirm")
                    compiler.confirm(
                        runtime_name, parameters.at("operation_id").get<std::string>(),
                        parameters.at("event").get<std::string>(), parameters.at("condition"), next,
                        parameters.contains("expected_step") ? parameters.at("expected_step") : J(nullptr));
                else if (binding == "combat")
                    compiler.combat_step(runtime_name, parameters.at("condition"),
                                         parameters.at("arguments"), next);
                else if (binding == "chest")
                    compiler.chest_selection(
                        runtime_name, parameters.at("condition"),
                        static_cast<int>(parameters.at("preferred").get<std::int64_t>()),
                        static_cast<unsigned>(parameters.at("seed").get<std::uint64_t>()), next);
                else if (binding == "task_stage") {
                    if (!resolver)
                        fail("AUTHOR_BUSINESS_CONTEXT_REQUIRED", id);
                    const auto prefix = runtime_name + "_Stage";
                    const auto child = resolver(parameters);
                    const auto child_entry = compiler.define_child(prefix, child);
                    compiler.call_child(runtime_name, child_entry, next);
                }
                else
                    fail("AUTHOR_BUSINESS_UNSUPPORTED", id + ":" + binding);
            } else if (type == "end") {
                compiler.recovery(runtime_name, parameters.at("reason").get<std::string>());
            } else {
                fail("AUTHOR_NODE_TYPE_INVALID", id + ":" + type);
            }
            if (node->contains("repeat_limit"))
                compiler.hit_limit(runtime_name,
                                   static_cast<int>(node->at("repeat_limit").get<std::int64_t>()));
            else
                compiler.hit_limit(runtime_name, 1);
            if (type != "wait" && parameters.contains("delay_after_ms"))
                compiler.delay_after(runtime_name,
                                     static_cast<int>(parameters.at("delay_after_ms").get<std::int64_t>()));
            if (!graph.failure.at(id).empty())
                compiler.failure_route(runtime_name,
                                       successors(graph.failure.at(id), entry, graph.success_end));
        } catch (const std::runtime_error &error) {
            const std::string message = error.what();
            if (message.starts_with("AUTHOR_"))
                throw;
            fail("AUTHOR_NODE_COMPILE_FAILED", id + ":" + message);
        }
    }
    try {
        result.workflow = compiler.finish();
    } catch (const std::runtime_error &error) {
        fail("AUTHOR_COMPILE_FAILED", error.what());
    }
    // 编译器可能增加业务检查点和统一恢复节点；二者属于既有运行模型而非作者节点。
    for (const auto &[id, source] : graph.nodes) {
        (void)source;
        const auto prefix = pipeline_name(id, entry, graph.success_end) + "_Stage_";
        for (const auto &[name, node] : result.workflow.nodes.items()) {
            (void)node;
            if (name.starts_with(prefix)) {
                result.node_to_pipeline[id].push_back(name);
                result.pipeline_to_node[name] = id;
            }
        }
    }
    result.workflow.validate();
    return result;
}

} // namespace wvd::games::tasks
