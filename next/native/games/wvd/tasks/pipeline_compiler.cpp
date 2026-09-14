#include "pipeline_compiler.hpp"
#include <functional>
#include <set>
#include <stdexcept>

namespace wvd::games::tasks {
using J = nlohmann::json;
namespace {
void require(bool value, const char *code) {
    if (!value)
        throw std::runtime_error(code);
}
void collect_images(const J &value, std::set<std::string> &images) {
    if (value.is_object()) {
        // 专用识别器内部加载的资源也必须进入发布清单，不能等运行才发现缺图。
        const auto mode = value.value("mode", "");
        if (mode == "reached")
            for (int i = 0; i < 4; ++i)
                images.insert("cursor_" + std::to_string(i) + ".png");
        if (mode == "combat_active")
            for (const auto *name : {"combatActive", "combatActive_2", "combatActive_3", "combatActive_4"})
                images.insert(std::string(name) + ".png");
        if (mode == "movement_stopped")
            for (const auto *name : {"dungFlag", "mapFlag"})
                images.insert(std::string(name) + ".png");
        if (mode == "harken_stair" && value.contains("stair"))
            images.insert(value.at("stair").get<std::string>() + ".png");
        for (const auto &[key, child] : value.items()) {
            if (key == "image") {
                const auto path = child.get<std::string>();
                require(!path.empty() && path.front() != '/' &&
                            path.find(':') == std::string::npos &&
                            path.find('\\') == std::string::npos &&
                            path.find("..") == std::string::npos,
                        "COMPILE_IMAGE_PATH_INVALID");
                images.insert(path.ends_with(".png") ? path : path + ".png");
            } else
                collect_images(child, images);
        }
    } else if (value.is_array())
        for (const auto &child : value)
            collect_images(child, images);
}
std::set<std::string> collect_actions(const J &nodes) {
    std::set<std::string> actions;
    for (const auto &node : nodes) {
        if (node.value("custom_action", "") != "GuardedAction")
            continue;
        const auto &parameters = node.at("custom_action_param");
        const auto command = parameters.at("command").at("kind").get<std::string>();
        require(command == "Click" || command == "ClickKey" || command == "Swipe",
                "COMPILE_ACTION_PERMISSION_INVALID");
        for (auto name : {"scene_recognition", "target_recognition", "postcondition"}) {
            const auto &request = parameters.at(name);
            require(request.at("type") == "custom" && request.at("binding") == "WvdVision",
                    "COMPILE_RECOGNITION_UNKNOWN");
        }
        actions.insert(command);
    }
    return actions;
}
} // namespace
void CompiledWorkflow::validate() const {
    require(!kind.empty() && nodes.is_object() && nodes.contains(entry) && nodes.contains(terminal),
            "COMPILE_ENTRY_INVALID");
    require(nodes.size() <= 4096, "COMPILE_NODE_LIMIT");
    std::set<std::string> reached;
    std::function<void(const std::string &)> visit = [&](const std::string &name) {
        require(nodes.contains(name), "COMPILE_NEXT_UNKNOWN");
        if (!reached.insert(name).second)
            return;
        const auto &node = nodes.at(name);
        require(node.value("max_hit", 0) >= 1 && node.value("max_hit", 0) <= 256,
                "COMPILE_UNBOUNDED_NODE");
        const auto action = node.value("action", "DoNothing");
        require(action == "DoNothing" ||
                    (action == "Custom" && (node.value("custom_action", "") == "GuardedAction" ||
                                            node.value("custom_action", "") == "RootTerminal" ||
                                            node.value("custom_action", "") == "WvdConfirm" ||
                                            node.value("custom_action", "") == "BusinessCheckpoint" ||
                                            node.value("custom_action", "") == "RequireRecovery")),
                "COMPILE_UNGUARDED_ACTION");
        require(node.value("recognition", "DirectHit") == "DirectHit" ||
                    (node.value("recognition", "") == "Custom" &&
                     node.value("custom_recognition", "") == "WvdVision"),
                "COMPILE_RECOGNITION_UNKNOWN");
        if (node.value("custom_action", "") == "RootTerminal")
            require(name == terminal && node.value("next", J::array()).empty(),
                    "COMPILE_TERMINAL_INVALID");
        for (const auto &next : node.value("next", J::array()))
            visit(next.get<std::string>());
        for (const auto &next : node.value("on_error", J::array()))
            visit(next.get<std::string>());
    };
    visit(entry);
    require(reached.contains(terminal), "COMPILE_TERMINAL_UNREACHABLE");
    require(reached.size() == nodes.size(), "COMPILE_ORPHAN_NODE");
    require(nodes.at(terminal).value("custom_action", "") == "RootTerminal",
            "COMPILE_TERMINAL_INVALID");
    if (!checkpoint.empty())
        require(nodes.contains(checkpoint) && nodes.at(checkpoint).value("custom_action", "") == "BusinessCheckpoint",
                "COMPILE_CHECKPOINT_INVALID");
    std::set<std::string> actual_images;
    collect_images(nodes, actual_images);
    require(std::vector<std::string>(actual_images.begin(), actual_images.end()) == images,
            "COMPILE_RESOURCE_INDEX_STALE");
    const auto actions = collect_actions(nodes);
    require(std::vector<std::string>(actions.begin(), actions.end()) == required_actions,
            "COMPILE_PERMISSION_INDEX_STALE");
}
PipelineCompiler::PipelineCompiler(std::string kind) { workflow_.kind = std::move(kind); }
J PipelineCompiler::image(const std::string &name) {
    // 保留旧普通模板默认 0.8，不以降低阈值代替导航界面确认。
    return {{"mode", "template"}, {"image", name}, {"threshold", 0.8}};
}
J PipelineCompiler::any(J c) { return {{"mode", "any"}, {"conditions", std::move(c)}}; }
J PipelineCompiler::all(J c) { return {{"mode", "all"}, {"conditions", std::move(c)}}; }
J PipelineCompiler::absent(J c) {
    return {{"mode", "not"}, {"conditions", J::array({std::move(c)})}};
}
J PipelineCompiler::business(const std::string &field, J value, const std::string &comparison) {
    return {{"mode", "business"}, {"field", field}, {"value", std::move(value)}, {"comparison", comparison}};
}
J PipelineCompiler::request(const J &condition) const {
    return {{"id", workflow_.kind},   {"revision", "1"},          {"type", "custom"},
            {"binding", "WvdVision"}, {"roi", {0, 0, 900, 1600}}, {"parameters", condition}};
}
void PipelineCompiler::add(const std::string &name, J node) {
    require(!workflow_.nodes.contains(name), "COMPILE_NODE_DUPLICATE");
    node.update({{"pre_delay", 0},
                 {"post_delay", 0},
                 {"rate_limit", 50},
                 {"timeout", 3000},
                 {"max_hit", 5}});
    if (name != "Terminal" && name != "RecoveryRequired")
        node["on_error"] = {"RecoveryRequired"};
    workflow_.nodes[name] = std::move(node);
}
void PipelineCompiler::route(const std::string &name, J next) {
    add(name, {{"action", "DoNothing"}, {"next", std::move(next)}});
}
void PipelineCompiler::observe(const std::string &name, const J &condition, J next) {
    add(name, {{"recognition", "Custom"},
               {"custom_recognition", "WvdVision"},
               {"custom_recognition_param", condition},
               {"roi", {0, 0, 900, 1600}},
               {"action", "DoNothing"},
               {"next", std::move(next)}});
}
void PipelineCompiler::action(const std::string &name, const J &scene, const J &target,
                              const J &post, J command, J next, J offset) {
    J params{{"scene", "wvd"},
             {"scene_recognition", request(scene)},
             {"target_recognition", request(target)},
             {"postcondition", request(post)},
             {"command", std::move(command)},
             {"allowed_area", {1, 1, 898, 1598}},
             {"postcondition_timeout_ms", 3000},
             {"use_target_center", !offset.is_null()}};
    if (!offset.is_null())
        params.update({{"target_offset", offset}, {"clip_target_to_area", true}});
    add(name, {{"recognition", "Custom"},
               {"custom_recognition", "WvdVision"},
               {"custom_recognition_param", all({scene, target})},
               {"roi", {0, 0, 900, 1600}},
               {"action", "Custom"},
               {"custom_action", "GuardedAction"},
               {"custom_action_param", std::move(params)},
               {"next", std::move(next)}});
}
void PipelineCompiler::click(const std::string &name, const J &scene, const J &target,
                             const J &post, J next, J offset) {
    action(name, scene, target, post, {{"kind", "Click"}}, std::move(next), std::move(offset));
}
void PipelineCompiler::back(const std::string &name, const J &scene, const J &post, J next) {
    action(name, scene, scene, post, {{"kind", "ClickKey"}, {"key", 4}}, std::move(next), nullptr);
}
void PipelineCompiler::fixed_click(const std::string &name, const J &scene, const J &post,
                                   J position, J next) {
    require(position.is_array() && position.size() == 2 && position[0].is_number_integer() &&
                position[1].is_number_integer(),
            "COMPILE_POSITION_INVALID");
    require(position[0] >= 1 && position[0] <= 898 && position[1] >= 1 && position[1] <= 1598,
            "COMPILE_POSITION_INVALID");
    action(name, scene, scene, post, {{"kind", "Click"}, {"x", position[0]}, {"y", position[1]}},
           std::move(next), nullptr);
}
void PipelineCompiler::hit_limit(const std::string &name, int limit) {
    require(workflow_.nodes.contains(name) && limit >= 1 && limit <= 256,
            "COMPILE_HIT_LIMIT_INVALID");
    workflow_.nodes[name]["max_hit"] = limit;
}
void PipelineCompiler::delay_after(const std::string &name, int milliseconds) {
    require(workflow_.nodes.contains(name) && milliseconds >= 0 && milliseconds <= 10000,
            "COMPILE_DELAY_INVALID");
    workflow_.nodes[name]["post_delay"] = milliseconds;
}
void PipelineCompiler::postcondition_budget(const std::string &name, int milliseconds) {
    require(workflow_.nodes.contains(name) && milliseconds >= 1 && milliseconds <= 60000 &&
                workflow_.nodes.at(name).value("custom_action", "") == "GuardedAction",
            "COMPILE_POSTCONDITION_BUDGET_INVALID");
    workflow_.nodes[name]["custom_action_param"]["postcondition_timeout_ms"] = milliseconds;
}
void PipelineCompiler::swipe(const std::string &name, const J &scene, const J &post,
                             J coordinates, J next) {
    require(coordinates.is_array() && coordinates.size() == 4, "COMPILE_SWIPE_INVALID");
    for (std::size_t i = 0; i < 4; ++i)
        require(coordinates[i].is_number_integer() && coordinates[i] >= 1 &&
                    coordinates[i] <= (i % 2 == 0 ? 898 : 1598), "COMPILE_SWIPE_INVALID");
    action(name, scene, scene, post,
           {{"kind", "Swipe"}, {"x", coordinates[0]}, {"y", coordinates[1]},
            {"x2", coordinates[2]}, {"y2", coordinates[3]}, {"duration", 400}},
           std::move(next), nullptr);
}
std::string PipelineCompiler::append(const std::string &prefix, const CompiledWorkflow &child,
                                    J next, const J &normal_exits) {
    require(!prefix.empty() && prefix.find_first_not_of(
                "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") == std::string::npos,
            "COMPILE_PREFIX_INVALID");
    child.validate();
    require(normal_exits.is_object(), "COMPILE_EXIT_BINDINGS_INVALID");
    for (const auto &[name, successors] : normal_exits.items())
        require(child.nodes.contains(name) && child.nodes.at(name).value("custom_action", "") == "RequireRecovery" &&
                    successors.is_array() && !successors.empty(), "COMPILE_EXIT_BINDING_INVALID");
    for (const auto &[name, node] : child.nodes.items()) {
        (void)node;
        require(!workflow_.nodes.contains(prefix + "_" + name), "COMPILE_NODE_DUPLICATE");
    }
    for (const auto &[name, original] : child.nodes.items()) {
        auto node = original;
        if (node.value("custom_action", "") == "WvdConfirm") {
            auto &operation = node["custom_action_param"]["operation"];
            operation = prefix + ":" + operation.get<std::string>();
            require(operation.get<std::string>().size() <= 128, "COMPILE_BUSINESS_EVENT_INVALID");
        }
        for (const auto *key : {"next", "on_error"})
            if (node.contains(key))
                for (auto &edge : node[key])
                    edge = prefix + "_" + edge.get<std::string>();
        if (name == child.terminal) {
            node.erase("custom_action");
            node.erase("custom_action_param");
            node["action"] = "DoNothing";
            node["next"] = next;
            node["on_error"] = {"RecoveryRequired"};
        }
        if (normal_exits.contains(name)) {
            // 只有调用者显式绑定的普通插入出口改为外层后继；真正恢复出口仍保持 RequireRecovery。
            node.erase("custom_action");
            node.erase("custom_action_param");
            node["action"] = "DoNothing";
            node["next"] = normal_exits.at(name);
            node["on_error"] = {"RecoveryRequired"};
        }
        workflow_.nodes[prefix + "_" + name] = std::move(node);
    }
    return prefix + "_" + child.entry;
}
void PipelineCompiler::recovery(const std::string &name, const std::string &reason) {
    require(!reason.empty(), "COMPILE_RECOVERY_REASON_EMPTY");
    add(name, {{"action", "Custom"}, {"custom_action", "RequireRecovery"},
               {"custom_action_param", {{"reason", reason}}}});
}
void PipelineCompiler::confirm(const std::string &name, const std::string &operation,
                               const std::string &event, const J &condition, J next, J step) {
    const std::set<std::string> events{"target_completed", "dungeon_entered", "combat_observed",
                                      "chest_observed", "dungeon_resumed", "dungeon_completed", "resurrected"};
    require(events.contains(event) && !operation.empty() && operation.size() <= 128,
            "COMPILE_BUSINESS_EVENT_INVALID");
    require(step.is_null() || (step.is_number_integer() && step >= 0 && step <= 4096),
            "COMPILE_TASK_STEP_INVALID");
    require(event != "target_completed" || !step.is_null(), "COMPILE_TASK_STEP_REQUIRED");
    J parameters{{"event", event}, {"operation", operation}, {"confirmation", request(condition)}};
    if (!step.is_null())
        parameters["expected_step"] = step;
    add(name, {{"recognition", "Custom"}, {"custom_recognition", "WvdVision"},
               {"custom_recognition_param", condition}, {"roi", {0, 0, 900, 1600}},
               {"action", "Custom"}, {"custom_action", "WvdConfirm"},
               {"custom_action_param", parameters}, {"next", std::move(next)}});
}
CompiledWorkflow PipelineCompiler::finish() {
    bool business = false;
    for (const auto &node : workflow_.nodes)
        business = business || node.value("custom_action", "") == "WvdConfirm";
    if (business) {
        workflow_.checkpoint = "Checkpoint";
        for (auto &node : workflow_.nodes)
            for (const auto *key : {"next", "on_error"})
                if (node.contains(key))
                    for (auto &edge : node[key])
                        if (edge == "Terminal")
                            edge = workflow_.checkpoint;
        add(workflow_.checkpoint, {{"action", "Custom"}, {"custom_action", "BusinessCheckpoint"},
                                   {"next", {"Terminal"}}});
    }
    add("Terminal", {{"action", "Custom"}, {"custom_action", "RootTerminal"}});
    add("RecoveryRequired",
        {{"action", "Custom"},
         {"custom_action", "RequireRecovery"},
         {"custom_action_param", {{"reason", workflow_.kind + ".budget_exhausted"}}}});
    std::set<std::string> images;
    collect_images(workflow_.nodes, images);
    workflow_.images.assign(images.begin(), images.end());
    const auto actions = collect_actions(workflow_.nodes);
    workflow_.required_actions.assign(actions.begin(), actions.end());
    workflow_.validate();
    return std::move(workflow_);
}
} // namespace wvd::games::tasks
