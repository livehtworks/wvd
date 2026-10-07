#include "turn.hpp"
#include "level_selection_steps.hpp"
#include "auto_combat.hpp"
#include "strategy.hpp"
#include <algorithm>
#include <map>

namespace wvd::games::combat {
using J = nlohmann::json;
using C = tasks::PipelineCompiler;
namespace {
J roi_image(const char *name, J roi) {
    auto value = C::image(name);
    value["roi"] = std::move(roi);
    return value;
}
J slot(const std::string &name) {
    static const std::map<std::string, J> positions{
        {"左上技能", {266, 965}}, {"Top-Left Skill", {266, 965}},
        {"右上技能", {640, 965}}, {"Top-Right Skill", {640, 965}},
        {"左下技能", {266, 1054}}, {"Bottom-Left Skill", {266, 1054}},
        {"右下技能", {640, 1054}}, {"Bottom-Right Skill", {640, 1054}}};
    auto found = positions.find(name);
    if (found == positions.end())
        throw std::runtime_error("COMBAT_SKILL_SLOT_UNKNOWN");
    return found->second;
}
int support_slot(const std::string &name) {
    // 固定旧实现的友方目标键未做 gettext，不能擅自翻译配置值。
    static const std::map<std::string, int> positions{
        {"左上角色", 0}, {"中上角色", 1}, {"右上角色", 2},
        {"左下角色", 3}, {"中下角色", 4}, {"右下角色", 5}};
    const auto found = positions.find(name);
    if (found == positions.end()) throw std::runtime_error("PROFILE_SKILL_TARGET_INVALID");
    return found->second;
}
}

tasks::CompiledWorkflow take_turn(const J &profile, const std::set<std::string> &available_images) {
    C graph("combat.turn");
    graph.check_policy("combat", {"wvd-network-retry", "wvd-pause", "wvd-download", "wvd-party-death", "wvd-party-defeat"});
    J catalog = J::array(), portraits = J::array();
    std::set<std::string> declared;
    const auto reachable = reachable_strategy_groups(profile);
    std::set<std::string> selected_groups;
    for (const auto &group : profile.at("STRATEGY")) {
        const auto name = group.value("group_name", "");
        if (!reachable.contains(name) || !selected_groups.insert(name).second) continue;
        for (const auto &skill : group.value("skill_settings", J::array())) {
            if (std::find(catalog.begin(), catalog.end(), skill) == catalog.end())
                catalog.push_back(skill);
            const auto role = skill.value("role_var", "");
            if (role.empty())
                continue;
            for (const auto &name : {role, role + "_sp", role + "_alt"}) {
                const auto image = "spellskill/char/" + name;
                if (available_images.contains(image + ".png") && declared.insert(image).second)
                    portraits.push_back({{"image", image}, {"role", name}});
            }
        }
    }
    if (catalog.size() > 128)
        throw std::runtime_error("COMBAT_SKILL_CATALOG_INVALID");
    const J battle{{"mode", "combat_active"}};
    const auto detail = C::image("combat_skill_detail");
    const auto ok = C::image("combat_skill_confirm");
    const auto close = roi_image("close", {0, 600, 900, 1000});
    const auto popup = C::any({detail, ok, close});
    const auto ended = C::any({C::image("dungFlag"), C::image("chestFlag"), C::image("RiseAgain")});
    const auto menu = C::all({battle, roi_image("flee", {660, 1080, 240, 220}), C::absent(popup)});
    const auto disabled = roi_image("spellskill/CombatAutoDisable", {740, 940, 160, 280});
    const auto enabled = roi_image("spellskill/CombatAutoEnable", {740, 940, 160, 280});
    const auto clear = C::all({battle, C::absent(popup)});
    const auto speed_off_zh = roi_image("combat_speed_off_zh_hant", {0, 930, 120, 210});
    const auto speed_on_zh = roi_image("combat_speed_on_zh_hant", {0, 930, 120, 210});
    const auto speed = C::any({C::image("combatSpd"), C::image("combatSpd_DHI"), speed_off_zh});
    const auto actor = J{{"mode", "prepared_actor"}, {"portraits", portraits}};
    const J support{{"mode", "support_selection"}};
    const J no_support{{"mode", "support_selection"}, {"expect", "absent"}};
    const auto errors = C::any({C::image("notenoughsp"), C::image("notenoughmp")});
    const auto finished = C::all({C::any({clear, ended}), C::absent(errors), C::absent(popup)});
    const J auto_exits{{"BattleEndedExit", {"Terminal"}}, {"BlockedExit", {"BlockedExit"}}};
    const auto full_auto = graph.append("FullAuto", enable_auto(), {"Terminal"}, auto_exits);
    const J single_auto_exits{{"BlockedExit", {"BlockedExit"}}};
    const auto char_auto = graph.append("CharAuto", single_actor_auto(), {"Terminal"}, single_auto_exits);
    graph.route("Entry", {"Ended", "AutoOff", "SpeedZh", "Speed", "SpeedAlt", "Automatic", "Prepare", "UnexpectedPopup"});
    const auto unintended_auto = C::all({clear, enabled, C::absent(disabled), C::business("/strategy/automatic", false)});
    graph.click("AutoOff", unintended_auto, enabled, C::any({disabled, ended}), {"Entry"});
    graph.retry_menu_input("AutoOff", unintended_auto, 1000);
    graph.click("SpeedZh", clear, speed_off_zh,
                C::any({C::all({battle, speed_on_zh}), ended}),
                {"Ended", "Automatic", "Prepare", "UnexpectedPopup"});
    graph.hit_limit("SpeedZh", 1);
    graph.retry_menu_input("SpeedZh", C::all({clear, speed_off_zh}), 3000);
    for (const auto &[node, image] : {std::pair{"Speed", "combatSpd"}, std::pair{"SpeedAlt", "combatSpd_DHI"}}) {
        graph.click(node, clear, C::image(image), C::any({C::all({battle, C::absent(speed)}), ended}),
                    {"Ended", "Automatic", "Prepare", "UnexpectedPopup"});
        graph.hit_limit(node, 1);
        graph.retry_menu_input(node, C::all({clear, C::image(image)}), 3000);
    }
    graph.observe("Ended", ended, {"Terminal"});
    graph.observe("ActorChanged", C::all({battle, C::absent(actor)}), {"Terminal"});
    graph.observe("Automatic", C::all({battle, C::business("/strategy/automatic", true)}), {full_auto});
    // Generic gold buttons can be network Retry, not a skill confirmation.
    // Keep broad popup exclusion for input safety, but require skill-specific
    // detail evidence before classifying an unowned skill dialog.
    const auto unowned_detail = C::all({battle, detail});
    graph.observe("UnexpectedPopup", unowned_detail, {"UnownedDetail", "Entry"});
    graph.recovery("UnownedDetail", "combat.unowned_skill_detail", unowned_detail, {"Entry"});
    const auto recognized_actor = C::business("/combat_actor_recognized", true);
    J choices = {"UnknownActor", "NoSelection"};
    for (std::size_t index = 0; index < catalog.size(); ++index)
        choices.push_back("Select" + std::to_string(index));
    graph.combat_step("Prepare", menu, {{"operation", "prepare"}, {"portraits", portraits}, {"catalog", catalog}}, choices);
    // A transient portrait miss is not permission to replace a configured skill with Auto.
    // Use Prepare's identity evidence, then capture again under the existing phase deadline.
    graph.observe("UnknownActor", C::all({C::business("/has_prepared_skill", false),
                  C::absent(recognized_actor)}), {"Entry"});
    graph.delay_after("UnknownActor", 250);
    graph.observe("NoSelection", C::all({C::business("/has_prepared_skill", false),
                  recognized_actor}), {char_auto});
    for (std::size_t index = 0; index < catalog.size(); ++index) {
        const auto &skill = catalog[index];
        const auto prefix = "Skill" + std::to_string(index);
        const auto skill_name = skill.at("skill_var").get<std::string>();
        if (skill_name == "防御" || skill_name == "defend") {
            const auto advanced = C::all({finished, C::any({ended, C::absent(actor)})});
            graph.observe_business("Select" + std::to_string(index), C::business("/prepared_skill_index", index), {prefix + "Defend"});
            graph.fixed_click(prefix + "Defend", C::all({menu, actor}), C::any({clear, ended}), {513, 1200},
                              {prefix + "Success", prefix + "DefendConfirm"});
            graph.fixed_click(prefix + "DefendConfirm", C::all({menu, actor}), advanced, {513, 1200}, {prefix + "Success"});
            graph.stop_if_interrupted_after(prefix + "Defend", "combat.skill_outcome_unconfirmed");
            graph.stop_if_interrupted_after(prefix + "DefendConfirm", "combat.skill_outcome_unconfirmed");
            graph.combat_step(prefix + "Success", advanced, {{"operation", "success"}, {"index", index}}, {"Terminal"});
            continue;
        }
        graph.public_step(prefix + "Success", "combat-confirm-result", J::object(), {prefix + "RecordSuccess"}, J::object(), finished);
        graph.combat_step(prefix + "RecordSuccess", C::business("/prepared_skill_index", index),
            {{"operation", "success"}, {"index", index}}, {"Terminal"});
        // Failed opening is not a cast or permission to enable Auto. The public
        // handoff and this input both require fresh proof of the same actor/menu.
        const auto defended = C::all({finished, C::any({ended, C::absent(actor)})});
        graph.fixed_click(prefix + "UnavailableDefend", C::all({menu, actor}), defended,
            {513, 1200}, {prefix + "DefendFallbackDone"});
        graph.retry_menu_input(prefix + "UnavailableDefend", C::all({menu, actor}), 3000);
        graph.stop_if_interrupted_after(prefix + "UnavailableDefend", "combat.defend_outcome_unconfirmed");
        graph.combat_step(prefix + "DefendFallbackDone", defended,
            {{"operation", "defend_fallback_confirmed"}, {"index", index}}, {"Terminal"});
        const auto position = slot(skill_name);
        const int level = skill.at("skill_lvl");
        if (level < 1 || level > 9)
            throw std::runtime_error("WVD_SKILL_LEVEL_INVALID");
        if (level > 1) graph.recovery(prefix + "LevelUnconfirmed", "combat.skill_level_unconfirmed");
        graph.observe_business("Select" + std::to_string(index), C::business("/prepared_skill_index", index), {prefix + "Open0"});
        const auto casting = C::all({battle, actor, detail});
        // A confirmed resource error retries level one, then uses manual defend.
        const int attempts = level == 1 ? 1 : 2;
        for (int attempt = 0; attempt < attempts; ++attempt) {
            const auto s = prefix + "Try" + std::to_string(attempt);
            const int use_level = attempt ? 1 : level;
            const J target_choices{s + "Support", s + "Confirm", s + "Enemy0",
                "ActorChanged", "Ended", s + "ResourceError", s + "DetailClosed"};
            const int slot_index = (position[1].get<int>() > 1000 ? 2 : 0) + (position[0].get<int>() > 450 ? 1 : 0);
            const auto unavailable = C::all({menu, actor, J{{"mode", "combat_skill_disabled"}, {"slot", slot_index}}});
            graph.route(prefix + "Open" + std::to_string(attempt), {s + "Disabled", s + "Open0"});
            graph.observe(s + "Disabled", unavailable, {s + "DisabledAgain", s + "Open0"});
            graph.delay_after(s + "Disabled", 250);
            graph.observe(s + "DisabledAgain", unavailable, {prefix + "UnavailableDefend"});
            // 详情打开由同一次公共调用等待并补点，原菜单不能充当成功回执。
            graph.public_step(s + "Open0", "combat-open-detail", {{"x", position[0]}, {"y", position[1]}},
                              {s + "Detail", s + "ResourceError", "Ended", "ActorChanged"},
                              {{"UnavailableExit", {prefix + "UnavailableDefend"}}}, C::all({menu, actor}));
            const auto levels = append_level_selection_steps(graph, s, use_level, casting,
                target_choices, J{prefix + "LevelUnconfirmed"});
            graph.observe(s + "Detail", casting, levels);
            // Level selection returns before a target is clicked. A closed popup
            // on the same actor may be reopened; a changed actor must be reselected.
            graph.observe(s + "DetailClosed", C::all({menu, actor, C::absent(detail)}), {prefix + "Open" + std::to_string(attempt)});
            const J recipient{{"mode", "support_selection"}, {"slot", support_slot(skill.value("target_var", "左上角色"))}};
            graph.click(s + "Support", C::all({casting, support}), recipient, C::any({casting, finished, errors}),
                              {prefix + "Success", s + "Confirm", s + "ResourceError"});
            graph.stop_if_interrupted_after(s + "Support", "combat.skill_outcome_unconfirmed");
            // 确认施放不是选目标：详情仍在不能结清输入，否则下一分支会立即取消技能。
            // 等待原业务期限内的新帧结果，异常处理返回后也继续核对同一次输入。
            graph.click(s + "Confirm", casting, ok, C::any({finished, errors}),
                        {prefix + "Success", s + "ResourceError", s + "StillDetail"});
            graph.retry_menu_input(s + "Confirm", casting, 3000);
            graph.failure_route(s + "Confirm", {s + "StillDetail"});
            graph.stop_if_interrupted_after(s + "Confirm", "combat.skill_outcome_unconfirmed");
            const auto enemy = C::all({casting, C::absent(ok), no_support});
            // Each public step captures and guards anew; never batch points across actor changes.
            for (int point = 0; point < 24; ++point) {
                const auto name = s + "Enemy" + std::to_string(point);
                J next{prefix + "Success", s + "ResourceError"};
                next.push_back("ActorChanged");
                next.push_back(s + "Support");
                next.push_back(s + "Confirm");
                next.push_back(point + 1 < 24 ? s + "Enemy" + std::to_string(point + 1) : s + "StillDetail");
                graph.public_step(name, "combat-select-target", {{"candidate_index", point}}, next, J::object(), enemy);
            }
            // Keep the selected skill while retrying under the existing no-progress deadline.
            graph.observe(s + "StillDetail", casting, target_choices);
            graph.delay_after(s + "StillDetail", 1000);
            if (attempt + 1 < attempts)
                graph.back(s + "ResourceError", errors, menu, {prefix + "Open" + std::to_string(attempt + 1)});
            else {
                graph.back(s + "ResourceError", errors, menu, {prefix + "UnavailableDefend"});
            }
        }
    }
    graph.interrupt_on(C::absent(J{{"mode", "input_clear"}}), "combat.common_screen_requires_dispatch");
    return graph.finish();
}
}
