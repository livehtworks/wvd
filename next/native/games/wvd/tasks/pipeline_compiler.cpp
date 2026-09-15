#include "pipeline_compiler.hpp"
#include "games/wvd/vision/boot_probes.hpp"
#include "games/wvd/vision/navigation_probes.hpp"
#include "games/wvd/vision/dialogue_probes.hpp"
#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>

namespace wvd::games::tasks {
using J = nlohmann::json;
namespace {
void require(bool value, const char *code) {
    if (!value)
        throw std::runtime_error(code);
}
void collect_images(const J &value, std::set<std::string> &images, std::set<std::string> &expanded_modes) {
    if (value.is_object()) {
        // 专用识别器内部加载的资源也必须进入发布清单，不能等运行才发现缺图。
        const auto mode = value.value("mode", "");
        // 常量隐式依赖在本次收集内只展开一次；显式 image/动态参数仍逐项收集。
        // 不缓存整份图或跨编译共享结果，validate 仍独立重算完整资源集合。
        const bool expand = expanded_modes.insert(mode).second;
        if (expand && mode == "fishing_bait_empty") {
            images.insert("fishing/nobait.png");
            images.insert("fishing/8bait.png");
        }
        if (expand && mode == "fishing_reward") {
            for (auto name : {"CloseFishInfo", "size_small", "size_average", "size_large", "鲈鱼", "雅罗", "鲶鱼", "鳟鱼", "鳗鱼", "三文鱼", "杂鱼"})
                images.insert(std::string("fishing/") + name + ".png");
            collect_images(J{{"mode", "blocking_screen"}}, images, expanded_modes);
        }
        if (expand && mode == "fishing_bobber") images.insert("fishing/bobber.png");
        if (expand && mode == "fishing_unknown") {
            for (auto name : {"fishing/cast", "fishing/striking", "fishing/CloseFishInfo", "dungFlag"})
                images.insert(std::string(name) + ".png");
            collect_images(J{{"mode", "blocking_screen"}}, images, expanded_modes);
        }
        if (expand && mode == "mining_reward") {
            for (auto name : {"receive", "org_fine", "org_high", "org_mid", "org_low", "org_refine", "org_alter",
                              "org_sliver", "org_ouro", "org_lesser_full", "org_full"})
                images.insert(std::string("FFXI/") + name + ".png");
            collect_images(J{{"mode", "blocking_screen"}}, images, expanded_modes);
        }
        if (expand && mode == "dark_light_clear") {
            for (auto name : {"darklight", "darklight_lightIt", "dungFlag", "mapFlag", "trait", "recover",
                              "chestFlag", "whowillopenit", "chestOpening", "RiseAgain"})
                images.insert(std::string(name) + ".png");
            collect_images(J{{"mode", "combat_active"}}, images, expanded_modes);
            collect_images(J{{"mode", "blocking_screen"}}, images, expanded_modes);
        }
        if (expand && mode == "dark_light_post") {
            images.insert("darklight_lightIt.png");
            collect_images(J{{"mode", "boot_ready"}}, images, expanded_modes);
            collect_images(J{{"mode", "blocking_screen"}}, images, expanded_modes);
        }
        if (expand && (mode == "unknown_frozen" || mode == "unknown_exhausted")) {
            collect_images(J{{"mode", "boot_ready"}}, images, expanded_modes);
            collect_images(J{{"mode", "blocking_screen"}}, images, expanded_modes);
            for (const auto *name : {"trait", "recover", "spellskill/skillDetail"})
                images.insert(std::string(name) + ".png");
        }
        if (expand && (mode == "boot_ready" || mode == "boot_post"))
            collect_images(vision::boot_probes(mode == "boot_post"), images, expanded_modes);
        if (expand && mode == "blocking_screen")
            collect_images(vision::blocking_probes(), images, expanded_modes);
        if (expand && mode == "dialogue_post") {
            collect_images(J{{"mode", "default_dialogue"}}, images, expanded_modes);
            collect_images(vision::boot_probes(true), images, expanded_modes);
        }
        if (expand && mode == "default_dialogue") {
            collect_images(vision::default_dialogue_probes(), images, expanded_modes);
            collect_images(vision::default_dialogue_normal_probes(), images, expanded_modes);
            collect_images(vision::blocking_probes(false), images, expanded_modes);
            collect_images(J{{"mode", "party_death"}}, images, expanded_modes);
        }
        if (expand && mode == "auto_route_moving")
            collect_images(vision::auto_route_moving_probes(), images, expanded_modes);
        if (expand && mode == "map_route_post")
            collect_images(vision::map_route_post_probes(), images, expanded_modes);
        if (expand && mode == "auto_route_post") {
            images.insert("mapFlag.png");
            images.insert("dungFlag.png");
            collect_images(vision::auto_route_probes(), images, expanded_modes);
            collect_images(vision::auto_route_outside_probes(), images, expanded_modes);
        }
        if (expand && (mode == "party_death" || mode == "party_defeat")) {
            images.insert("someonedead.png");
            if (mode == "party_defeat")
                images.insert("multipeopledead.png");
            collect_images(vision::boot_probes(false), images, expanded_modes);
            collect_images(J{{"mode", "pause"}}, images, expanded_modes);
            collect_images(vision::blocking_probes(false), images, expanded_modes);
        }
        if (expand && mode == "party_death_post") {
            collect_images(J{{"mode", "party_death"}}, images, expanded_modes);
            collect_images(J{{"mode", "party_defeat"}}, images, expanded_modes);
            images.insert("RiseAgain.png");
            collect_images(vision::boot_probes(true), images, expanded_modes);
        }
        if (mode == "pause" || mode == "pause_negative")
            for (const auto *name : {"trait", "recover", "spellskill/skillDetail", "close"})
                images.insert(std::string(name) + ".png");
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
        if (mode == "skill_level")
            for (const auto *prefix : {"lv", "s_lv"})
                images.insert(std::string("spellskill/skillLvl/") + prefix + std::to_string(value.at("level").get<int>()) + ".png");
        if (mode == "skill_target")
            for (const auto *name : {"next", "combatTarget", "spellskill/skillDetail", "OK", "supportSkillCheck"})
                images.insert(std::string(name) + ".png");
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
                collect_images(child, images, expanded_modes);
        }
    } else if (value.is_array())
        for (const auto &child : value)
            collect_images(child, images, expanded_modes);
}
void collect_images(const J &value, std::set<std::string> &images) {
    std::set<std::string> expanded_modes;
    collect_images(value, images, expanded_modes);
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
    require(time_limit > std::chrono::milliseconds::zero() && time_limit <= std::chrono::minutes{30},
            "COMPILE_SESSION_BUDGET_INVALID");
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
                                            node.value("custom_action", "") == "RunChild" ||
                                            node.value("custom_action", "") == "WvdConfirm" ||
                                            node.value("custom_action", "") == "WvdCombat" ||
                                            node.value("custom_action", "") == "WvdChest" ||
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
        if (node.value("custom_action", "") == "RunChild") {
            const auto &p = node.at("custom_action_param");
            require(p.is_object() && p.size() == 3 && p.contains("entry") && p.at("entry").is_string() &&
                        p.contains("clone") && p.at("clone") == false && p.contains("reset_hit_counts") &&
                        p.at("reset_hit_counts").is_array(), "COMPILE_CHILD_PARAMETERS_INVALID");
            visit(p.at("entry").get<std::string>());
        }
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
    // next/on_error 不能越过子任务边界；只有 RunChild 可进入子任务，只有根图可到根终点。
    // 每个作用域独立检查，普通图循环由 max_hit/Session 预算约束；调用递归则直接拒绝。
    std::map<std::string, std::string> owners;
    std::map<std::string, unsigned> scope_heights;
    std::set<std::string> checked_scopes, active_scopes;
    std::function<void(const std::string &)> check_scope = [&](const std::string &scope) {
        require(!active_scopes.contains(scope), "COMPILE_CHILD_RECURSIVE");
        if (checked_scopes.contains(scope))
            return;
        require(active_scopes.size() < 8, "COMPILE_CHILD_DEPTH_LIMIT");
        active_scopes.insert(scope);
        std::set<std::string> local;
        bool has_terminal = false;
        unsigned height = 1;
        std::function<void(const std::string &)> walk = [&](const std::string &name) {
            if (!local.insert(name).second)
                return;
            const auto &node = nodes.at(name);
            const auto action = node.value("custom_action", "");
            // RequireRecovery 只关闭整个 Session，没有正常返回边，可以共享失败出口。
            if (action == "RequireRecovery")
                return;
            const auto [owner, inserted] = owners.emplace(name, scope);
            require(inserted || owner->second == scope, "COMPILE_CHILD_BOUNDARY_CROSSED");
            if (action == "RootTerminal")
                require(scope == entry, "COMPILE_CHILD_ROOT_TERMINAL");
            if (action == "RunChild") {
                const auto target = node.at("custom_action_param").at("entry").get<std::string>();
                check_scope(target);
                height = std::max(height, 1 + scope_heights.at(target));
            }
            const auto next = node.value("next", J::array());
            if (next.empty() && (action == "RootTerminal" || node.value("action", "DoNothing") == "DoNothing"))
                has_terminal = true;
            for (const auto *key : {"next", "on_error"})
                for (const auto &edge : node.value(key, J::array()))
                    walk(edge.get<std::string>());
        };
        walk(scope);
        require(has_terminal, "COMPILE_CHILD_TERMINAL_MISSING");
        // 已从浅层验证过的共享子图，也要计入当前调用链的最长深度。
        require(height <= 8, "COMPILE_CHILD_DEPTH_LIMIT");
        scope_heights.emplace(scope, height);
        active_scopes.erase(scope);
        checked_scopes.insert(scope);
    };
    check_scope(entry);
    for (const auto &node : nodes) {
        if (node.value("custom_action", "") != "RunChild")
            continue;
        const auto &p = node.at("custom_action_param");
        std::vector<std::string> expected;
        for (const auto &[name, scope] : owners)
            if (scope == p.at("entry").get<std::string>())
                expected.push_back(name);
        require(p.at("reset_hit_counts") == expected, "COMPILE_CHILD_RESET_SCOPE_INVALID");
    }
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
PipelineCompiler::PipelineCompiler(std::string kind, std::chrono::milliseconds time_limit) {
    workflow_.kind = std::move(kind);
    workflow_.time_limit = time_limit;
}
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
    local_nodes_.push_back(name);
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
void PipelineCompiler::allowed_area(const std::string &name, J area) {
    require(workflow_.nodes.contains(name) && workflow_.nodes.at(name).value("custom_action", "") == "GuardedAction",
            "COMPILE_ACTION_AREA_INVALID");
    require(area.is_array() && area.size() == 4, "COMPILE_ACTION_AREA_INVALID");
    for (const auto &part : area)
        require(part.is_number_integer(), "COMPILE_ACTION_AREA_INVALID");
    require(area[0] >= 0 && area[1] >= 0 && area[2] > 0 && area[3] > 0 &&
                area[0].get<int>() <= 900 - area[2].get<int>() &&
                area[1].get<int>() <= 1600 - area[3].get<int>(), "COMPILE_ACTION_AREA_INVALID");
    workflow_.nodes[name]["custom_action_param"]["allowed_area"] = std::move(area);
}
void PipelineCompiler::interrupt_on(J condition, std::string reason) {
    require(interruption_.is_null() && condition.is_object() && !reason.empty(), "COMPILE_INTERRUPTION_INVALID");
    interruption_ = std::move(condition);
    interruption_reason_ = std::move(reason);
}
void PipelineCompiler::stop_if_interrupted_after(const std::string &name, std::string reason) {
    require(workflow_.nodes.contains(name) &&
                std::find(local_nodes_.begin(), local_nodes_.end(), name) != local_nodes_.end() &&
                workflow_.nodes.at(name).value("custom_action", "") == "GuardedAction" && !reason.empty(),
            "COMPILE_UNCERTAIN_ACTION_INVALID");
    require(uncertain_actions_.emplace(name, std::move(reason)).second, "COMPILE_UNCERTAIN_ACTION_DUPLICATE");
}
void PipelineCompiler::compile_interruption() {
    if (interruption_.is_null()) {
        require(uncertain_actions_.empty(), "COMPILE_INTERRUPTION_REQUIRED");
        return;
    }
    require(!workflow_.nodes.contains("Interrupt") && !workflow_.nodes.contains("BlockedExit"),
            "COMPILE_INTERRUPTION_NAME_CONFLICT");
    const auto clear = absent(interruption_);
    // 仅改此构建器声明的节点。append/define_child 的节点已有自己的出口和作用域，
    // 不能跨进去重写，更不能把 Common 处理器也拦在自己要关闭的弹窗之前。
    for (const auto &name : local_nodes_) {
        auto &node = workflow_.nodes[name];
        const auto action = node.value("custom_action", "");
        if (action == "RequireRecovery")
            continue;
        if (node.value("recognition", "") == "Custom")
            node["custom_recognition_param"] = all({clear, node.at("custom_recognition_param")});
        if (action == "GuardedAction") {
            auto &p = node["custom_action_param"];
            p["scene_recognition"]["parameters"] = all({clear, p.at("scene_recognition").at("parameters")});
            // 固定点位/返回键原本就以同一条件证明场景和目标，包装后仍须保持一致。
            // 否则同一帧会先查完整 guarded scene，再把未包装的原场景重查一遍。
            if (!p.at("use_target_center").get<bool>())
                p["target_recognition"] = p.at("scene_recognition");
            // 弹窗只是动作后的普通插入，不是业务成功。后继先返回外层；下面所有
            // WvdConfirm/WvdCombat 的新帧确认也排除弹窗，不能因此消费技能或任务点。
            p["postcondition"]["parameters"] = any({interruption_, p.at("postcondition").at("parameters")});
        } else if (action == "WvdConfirm" || action == "WvdCombat" || action == "WvdChest") {
            auto &confirmation = node["custom_action_param"]["confirmation"]["parameters"];
            confirmation = all({clear, confirmation});
        }
        if (node.contains("next") && !node.at("next").empty()) {
            // 已点出技能/恢复等动作时，不把覆盖层当成可重新入场的普通中断。
            // 专属出口保留动作事件和未消费状态；调用者不会将它绑定为正常子返回。
            node["next"].insert(node["next"].begin(),
                uncertain_actions_.contains(name) ? name + "OutcomeBlocked" : "Interrupt");
        }
        // on_error 不作正常返回：原生输入拒绝、识别 Error 和预算失败仍须失败/恢复。
    }
    for (const auto &[name, reason] : uncertain_actions_) {
        observe(name + "OutcomeBlocked", interruption_, {name + "OutcomeUnconfirmed"});
        hit_limit(name + "OutcomeBlocked", 128);
        recovery(name + "OutcomeUnconfirmed", reason);
    }
    observe("Interrupt", interruption_, {"BlockedExit"});
    hit_limit("Interrupt", 128);
    recovery("BlockedExit", interruption_reason_);
}
void PipelineCompiler::combat_step(const std::string &name, const J &condition, J parameters, J next) {
    require(parameters.is_object(), "COMPILE_COMBAT_PARAMETERS_INVALID");
    parameters["confirmation"] = request(condition);
    add(name, {{"recognition", "Custom"}, {"custom_recognition", "WvdVision"},
               {"custom_recognition_param", condition}, {"roi", {0, 0, 900, 1600}},
               {"action", "Custom"}, {"custom_action", "WvdCombat"},
               {"custom_action_param", std::move(parameters)}, {"next", std::move(next)}});
}
void PipelineCompiler::chest_selection(const std::string &name, const J &condition,
                                      int preferred, unsigned seed, J next) {
    require(preferred >= 0 && preferred <= 6, "CHEST_CHARACTER_INVALID");
    add(name, {{"recognition", "Custom"}, {"custom_recognition", "WvdVision"},
        {"custom_recognition_param", condition}, {"roi", {0, 0, 900, 1600}},
        {"action", "Custom"}, {"custom_action", "WvdChest"},
        {"custom_action_param", {{"confirmation", request(condition)}, {"preferred", preferred},
                                 {"seed", seed}, {"image", "chestfear"}}}, {"next", std::move(next)}});
}
void PipelineCompiler::swipe(const std::string &name, const J &scene, const J &post,
                             J coordinates, J next, int duration) {
    require(duration >= 1 && duration <= 60000, "COMPILE_SWIPE_DURATION_INVALID");
    require(coordinates.is_array() && coordinates.size() == 4, "COMPILE_SWIPE_INVALID");
    for (std::size_t i = 0; i < 4; ++i)
        require(coordinates[i].is_number_integer() && coordinates[i] >= 1 &&
                    coordinates[i] <= (i % 2 == 0 ? 898 : 1598), "COMPILE_SWIPE_INVALID");
    action(name, scene, scene, post,
           {{"kind", "Swipe"}, {"x", coordinates[0]}, {"y", coordinates[1]},
            {"x2", coordinates[2]}, {"y2", coordinates[3]}, {"duration", duration}},
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
        if (node.value("custom_action", "") == "RunChild") {
            auto &entry = node["custom_action_param"]["entry"];
            entry = prefix + "_" + entry.get<std::string>();
            for (auto &reset_name : node["custom_action_param"]["reset_hit_counts"])
                reset_name = prefix + "_" + reset_name.get<std::string>();
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
std::string PipelineCompiler::define_child(const std::string &prefix, const CompiledWorkflow &child,
                                          const std::vector<std::string> &normal_returns) {
    J returns = J::object();
    for (const auto &name : normal_returns) {
        // 普通插入出口只能显式列出，不能把通用错误出口整体改成成功。
        require(name != "RecoveryRequired" && !returns.contains(name), "COMPILE_CHILD_RETURN_INVALID");
        returns[name] = {prefix + "_" + child.terminal};
    }
    return append(prefix, child, J::array(), returns);
}
void PipelineCompiler::call_child(const std::string &name, const std::string &entry, J next) {
    require(!entry.empty(), "COMPILE_CHILD_ENTRY_INVALID");
    std::set<std::string> local;
    std::function<void(const std::string &)> collect = [&](const std::string &n) {
        if (n == "RecoveryRequired")
            return;
        require(workflow_.nodes.contains(n), "COMPILE_CHILD_ENTRY_INVALID");
        const auto &node = workflow_.nodes.at(n);
        if (node.value("custom_action", "") == "RequireRecovery" || !local.insert(n).second)
            return;
        for (const auto *key : {"next", "on_error"})
            for (const auto &edge : node.value(key, J::array()))
                collect(edge.get<std::string>());
    };
    collect(entry);
    add(name, {{"action", "Custom"}, {"custom_action", "RunChild"},
               {"custom_action_param", {{"entry", entry}, {"clone", false}, {"reset_hit_counts", local}}},
               {"next", std::move(next)}});
}
void PipelineCompiler::recovery(const std::string &name, const std::string &reason) {
    require(!reason.empty(), "COMPILE_RECOVERY_REASON_EMPTY");
    add(name, {{"action", "Custom"}, {"custom_action", "RequireRecovery"},
               {"custom_action_param", {{"reason", reason}}}});
}
void PipelineCompiler::failure_route(const std::string &name, J next) {
    require(workflow_.nodes.contains(name) && next.is_array() && !next.empty(), "COMPILE_ERROR_ROUTE_INVALID");
    workflow_.nodes[name]["on_error"] = std::move(next);
}
void PipelineCompiler::confirm(const std::string &name, const std::string &operation,
                               const std::string &event, const J &condition, J next, J step) {
    const std::set<std::string> events{"target_completed", "dungeon_entered", "combat_observed",
                                      "chest_observed", "dungeon_resumed", "dungeon_completed", "revival_observed", "resurrected", "game_restarted",
                                      "healing_requested", "healing_completed", "inn_payment_prepared", "inn_rest_completed", "party_reassembled", "chest_character_attempted",
                                      "party_death_observed", "party_death_cleared", "party_defeat_observed",
                                      "wall_turn_completed", "wall_left_completed", "wall_right_completed",
                                      "karma_observed", "karma_completed", "trap_cycle_started", "trap_cycle_completed",
                                      "giant_cycle_started", "giant_route_completed", "giant_cycle_completed",
                                      "bounty_revealed", "bounty_report_prepared", "bounty_report_completed",
                                      "fishing_reward_prepared", "fishing_reward_completed",
                                      "fishing_wait_started", "fishing_wait_failed",
                                      "fishing_cast_prepared", "fishing_cast_completed",
                                      "fishing_bait_requested", "fishing_supplies_entered", "fishing_transfer_prepared", "fishing_transferred",
                                      "fishing_supplies_finished", "fishing_supplies_returned", "fishing_refilled",
                                      "sleep_visit_started", "sleep_visit_completed",
                                      "scorpion_started", "scorpion_hands_started", "bounty_leap_prepared", "bounty_leap_completed",
                                      "bounty_travel_prepared", "bounty_travel_completed", "bounty_cycle_revealed", "bounty_route_completed",
                                      "bounty_travel_skipped",
                                      "bounty_return_completed", "bounty_cycle_reported", "bounty_cycle_completed",
                                      "manual_started_in_city", "manual_route_completed", "manual_rest_completed", "manual_first_back_prepared", "manual_first_back_completed",
                                      "manual_second_back_prepared", "manual_second_back_completed", "manual_leap_prepared", "manual_leap_completed",
                                      "mining_reward_observed", "mining_reward_dismissed", "mining_refill_requested",
                                      "mining_party_assembled", "mining_refill_completed", "mining_cycle_completed",
                                      "dark_light_entered", "dark_light_completed"};
    require(events.contains(event) && !operation.empty() && operation.size() <= 128,
            "COMPILE_BUSINESS_EVENT_INVALID");
    require(step.is_null() || (step.is_number_integer() && step >= 0 && step <= 4096),
            "COMPILE_TASK_STEP_INVALID");
    require(event != "target_completed" || !step.is_null(), "COMPILE_TASK_STEP_REQUIRED");
    require(event != "mining_reward_observed" || (condition.value("mode", "") == "mining_reward" && step.is_null()),
            "COMPILE_MINING_REWARD_RECOGNITION_REQUIRED");
    require(event != "fishing_reward_prepared" || (condition.value("mode", "") == "fishing_reward" && step.is_null()),
            "COMPILE_FISHING_REWARD_RECOGNITION_REQUIRED");
    J parameters{{"event", event}, {"operation", operation}, {"confirmation", request(condition)}};
    if (!step.is_null())
        parameters["expected_step"] = step;
    add(name, {{"recognition", "Custom"}, {"custom_recognition", "WvdVision"},
               {"custom_recognition_param", condition}, {"roi", {0, 0, 900, 1600}},
               {"action", "Custom"}, {"custom_action", "WvdConfirm"},
               {"custom_action_param", parameters}, {"next", std::move(next)}});
}
CompiledWorkflow PipelineCompiler::finish() {
    compile_interruption();
    bool business = false;
    for (const auto &node : workflow_.nodes)
        business = business || node.value("custom_action", "") == "WvdConfirm" || node.value("custom_action", "") == "WvdCombat" ||
                   node.value("custom_action", "") == "WvdChest";
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
