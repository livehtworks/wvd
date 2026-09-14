#include "turn.hpp"
#include "auto_combat.hpp"
#include "games/wvd/state.hpp"
#include <algorithm>
#include <map>

namespace wvd::games::combat {
using J = nlohmann::json;
using C = tasks::PipelineCompiler;
namespace {
J request(J parameters) {
    return {{"id", "combat.turn"}, {"revision", "1"}, {"type", "custom"}, {"binding", "WvdVision"},
             {"roi", {0, 0, 900, 1600}}, {"parameters", std::move(parameters)}};
}
bool combat_action(maafw::Context &context, const J &p, const J &) {
    if (context.cancelled())
        return false;
    const auto frame = context.capture();
    auto confirmation = context.recognize(frame, maafw::parse_recognition_request(p.at("confirmation")));
    if (confirmation.outcome == contracts::RecognitionOutcome::Error)
        throw std::runtime_error(confirmation.error_code);
    if (confirmation.outcome != contracts::RecognitionOutcome::Hit || !confirmation.action_eligible)
        throw std::runtime_error("COMBAT_CONFIRMATION_MISSING");
    const auto operation = p.at("operation").get<std::string>();
    std::vector<PortraitScore> scores;
    if (operation == "prepare") {
        for (const auto &candidate : p.at("portraits")) {
            if (context.cancelled())
                return false;
            auto observed = context.recognize(frame, maafw::parse_recognition_request(
                request({{"mode", "portrait"}, {"image", candidate.at("image")}})));
            if (observed.outcome == contracts::RecognitionOutcome::Error)
                throw std::runtime_error(observed.error_code);
            scores.push_back({candidate.at("role"), observed.evidence.at("evidence").at("best_score")});
        }
    } else if (operation != "success" && operation != "auto_confirmed")
        throw std::runtime_error("COMBAT_OPERATION_UNKNOWN");
    J receipt;
    const auto accepted = context.with_business_state([&](contracts::BusinessRunState &base) {
        if (context.cancelled())
            return false;
        auto &state = dynamic_cast<WvdRunState &>(base);
        if (operation == "prepare")
            state.prepare_skill(scores, p.at("catalog"));
        else
            state.finish_prepared_skill(p.at("index"), operation == "success"
                ? SkillOutcome::Succeeded : SkillOutcome::AutoFallbackConfirmed);
        receipt = {{"operation", operation}, {"frame_id", frame.identity.frame_id},
                    {"generation", frame.identity.generation}, {"consumed", operation != "prepare"}};
        return true;
    });
    if (accepted)
        context.business_event("combat", receipt);
    return accepted;
}
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
J support_position(const std::string &name) {
    // 固定旧实现的友方目标键未做 gettext，不能擅自翻译配置值。
    static const std::map<std::string, J> positions{
        {"左上角色", {200, 1200}}, {"中上角色", {450, 1200}}, {"右上角色", {700, 1200}},
        {"左下角色", {200, 1400}}, {"中下角色", {450, 1400}}, {"右下角色", {700, 1400}}};
    const auto found = positions.find(name);
    return found == positions.end() ? J(nullptr) : found->second;
}
}
void register_combat(runtime::BehaviorRegistry &registry) {
    registry.add_action({"wvd.combat", "1"}, combat_action);
}
contracts::BehaviorBinding combat_binding() { return {"WvdCombat", {"wvd.combat", "1"}, J::object()}; }

tasks::CompiledWorkflow take_turn(const J &profile, const std::set<std::string> &available_images) {
    C graph("combat.turn");
    J catalog = J::array(), portraits = J::array();
    std::set<std::string> declared;
    for (const auto &group : profile.at("STRATEGY"))
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
    if (catalog.size() > 128)
        throw std::runtime_error("COMBAT_SKILL_CATALOG_INVALID");
    const J battle{{"mode", "combat_active"}};
    const auto detail = C::image("spellskill/skillDetail");
    const auto ok = C::image("OK");
    const auto close = roi_image("close", {250, 1420, 420, 150});
    const auto popup = C::any({detail, ok, close});
    const auto ended = C::any({C::image("dungFlag"), C::image("chestFlag"), C::image("RiseAgain")});
    const auto menu = C::all({battle, roi_image("flee", {720, 1120, 180, 130}), C::absent(popup)});
    const auto enabled = roi_image("spellskill/CombatAutoEnable", {780, 1030, 120, 160});
    const auto disabled = roi_image("spellskill/CombatAutoDisable", {780, 1030, 120, 160});
    const auto clear = C::all({battle, C::absent(popup)});
    const auto speed = C::any({C::image("combatSpd"), C::image("combatSpd_DHI")});
    const auto actor = J{{"mode", "prepared_actor"}, {"portraits", portraits}};
    const auto support = roi_image("supportSkillCheck", {677, 1475, 189, 80});
    const auto errors = C::any({C::image("notenoughsp"), C::image("notenoughmp")});
    const auto finished = C::all({C::any({clear, ended}), C::absent(errors), C::absent(popup)});
    const J auto_exits{{"BattleEndedExit", {"Terminal"}}, {"BlockedExit", {"BlockedExit"}}};
    const auto full_auto = graph.append("FullAuto", enable_auto(), {"Terminal"}, auto_exits);
    const auto char_auto = graph.append("CharAuto", enable_auto(), {"DisableCharAuto", "AutoEnded"}, auto_exits);
    graph.fixed_click("DisableCharAuto", C::all({clear, enabled}), C::any({C::all({clear, disabled}), ended}),
                      {850, 1100}, {"AutoEnded"});
    graph.observe("AutoEnded", C::any({C::all({clear, disabled}), ended}), {"Terminal"});
    graph.route("Entry", {"Ended", "Speed", "SpeedAlt", "Automatic", "Prepare", "UnexpectedPopup"});
    for (const auto &[node, image] : {std::pair{"Speed", "combatSpd"}, std::pair{"SpeedAlt", "combatSpd_DHI"}}) {
        graph.click(node, clear, C::image(image), C::any({C::all({battle, C::absent(speed)}), ended}),
                    {"Ended", "Automatic", "Prepare", "UnexpectedPopup"});
        graph.hit_limit(node, 1);
    }
    graph.observe("Ended", ended, {"Terminal"});
    graph.observe("Automatic", C::all({battle, C::business("/strategy/automatic", true)}), {full_auto});
    graph.observe("UnexpectedPopup", C::all({battle, popup}), {char_auto});
    J choices = {"NoSelection"};
    for (std::size_t index = 0; index < catalog.size(); ++index)
        choices.push_back("Select" + std::to_string(index));
    graph.combat_step("Prepare", menu, {{"operation", "prepare"}, {"portraits", portraits}, {"catalog", catalog}}, choices);
    graph.observe("NoSelection", C::business("/has_prepared_skill", false), {char_auto});
    for (std::size_t index = 0; index < catalog.size(); ++index) {
        const auto &skill = catalog[index];
        const auto prefix = "Skill" + std::to_string(index);
        const auto skill_name = skill.at("skill_var").get<std::string>();
        if (skill_name == "防御" || skill_name == "defend") {
            const auto advanced = C::all({finished, C::any({ended, C::absent(actor)})});
            graph.observe("Select" + std::to_string(index), C::business("/prepared_skill_index", index), {prefix + "Defend"});
            graph.fixed_click(prefix + "Defend", C::all({menu, actor}), C::any({clear, ended}), {513, 1200},
                              {prefix + "Success", prefix + "DefendConfirm"});
            graph.fixed_click(prefix + "DefendConfirm", C::all({menu, actor}), advanced, {513, 1200}, {prefix + "Success"});
            graph.combat_step(prefix + "Success", advanced, {{"operation", "success"}, {"index", index}}, {"Terminal"});
            continue;
        }
        const auto automatic = graph.append(prefix + "Auto", enable_auto(), {prefix + "Disable", prefix + "AutoConfirmed"}, auto_exits);
        graph.fixed_click(prefix + "Disable", C::all({clear, enabled}), C::any({C::all({clear, disabled}), ended}),
                          {850, 1100}, {prefix + "AutoConfirmed"});
        graph.combat_step(prefix + "AutoConfirmed", C::any({C::all({clear, disabled}), ended}),
                          {{"operation", "auto_confirmed"}, {"index", index}}, {"Terminal"});
        graph.combat_step(prefix + "Success", finished, {{"operation", "success"}, {"index", index}}, {"Terminal"});
        const auto position = slot(skill_name);
        const int level = skill.at("skill_lvl");
        if (level < 1 || level > 9)
            throw std::runtime_error("WVD_SKILL_LEVEL_INVALID");
        graph.observe("Select" + std::to_string(index), C::business("/prepared_skill_index", index), {prefix + "Open0"});
        const auto casting = C::all({battle, actor, detail});
        // 正常等级失败可有界重试一次 1 级；第二次资源不足保留为恢复出口。
        const int attempts = level == 1 ? 1 : 2;
        for (int attempt = 0; attempt < attempts; ++attempt) {
            const auto s = prefix + "Try" + std::to_string(attempt);
            const int use_level = attempt ? 1 : level;
            const auto lv1 = J{{"mode", "skill_level"}, {"level", 1}};
            const auto wanted = J{{"mode", "skill_level"}, {"level", use_level}};
            const J target_choices{s + "Support", s + "Confirm", s + "Enemy0", s + "Missing"};
            graph.route(prefix + "Open" + std::to_string(attempt), {s + "Open0"});
            for (int opening = 0; opening < 3; ++opening) {
                const auto open = s + "Open" + std::to_string(opening);
                graph.fixed_click(open, C::all({menu, actor}), C::any({casting, menu, errors, ended}), position,
                                  {s + "Detail", s + "ResourceError", opening == 2 ? s + "OpenFailed" : s + "Open" + std::to_string(opening + 1)});
                graph.hit_limit(open, 1);
                graph.delay_after(open, 600);
            }
            graph.observe(s + "Detail", casting, {s + "Level", s + "Level1", s + "DefaultLevel"});
            graph.observe(s + "OpenFailed", C::all({menu, actor}), {automatic});
            graph.click(s + "Level", C::all({casting, lv1}), wanted, casting, target_choices);
            graph.click(s + "Level1", C::all({casting, lv1, C::absent(wanted)}), lv1, casting, target_choices);
            graph.observe(s + "DefaultLevel", C::all({casting, C::absent(lv1)}), target_choices);
            const auto recipient = support_position(skill.value("target_var", ""));
            if (!recipient.is_null())
                graph.fixed_click(s + "Support", C::all({casting, support}), C::any({casting, finished, errors}), recipient,
                                  {prefix + "Success", s + "Confirm", s + "ResourceError"});
            else
                graph.observe(s + "Support", C::all({casting, support}), {s + "Confirm", automatic});
            graph.click(s + "Confirm", casting, ok, C::any({casting, finished, errors}),
                        {prefix + "Success", s + "ResourceError", s + "StillDetail"});
            const auto target = J{{"mode", "skill_target"}, {"portraits", portraits}};
            const auto enemy = C::all({casting, C::absent(ok), C::absent(support)});
            const std::vector<J> offsets{{-80,80}, {0,80}, {80,80}, {-120,140}, {-40,140}, {40,140}, {120,140},
                                        {-110,210}, {-35,210}, {35,210}, {110,210}, {-70,275}, {0,275}, {70,275}};
            for (std::size_t point = 0; point < offsets.size(); ++point) {
                const auto name = s + "Enemy" + std::to_string(point);
                J next{prefix + "Success", s + "ResourceError"};
                next.push_back(point + 1 < offsets.size() ? s + "Enemy" + std::to_string(point + 1) : s + "StillDetail");
                next.push_back(s + "Missing");
                graph.click(name, enemy, target, C::any({casting, finished, errors}), next, offsets[point]);
                graph.allowed_area(name, {1, 260, 898, 641});
                graph.hit_limit(name, 1);
                graph.delay_after(name, 200);
            }
            graph.observe(s + "Missing", C::all({enemy, C::absent(target)}), {automatic});
            graph.observe(s + "StillDetail", casting, {automatic});
            if (attempt + 1 < attempts)
                graph.back(s + "ResourceError", errors, menu, {prefix + "Open" + std::to_string(attempt + 1)});
            else {
                graph.observe(s + "ResourceError", errors, {prefix + "ResourceExit"});
                graph.recovery(prefix + "ResourceExit", "combat.resource_insufficient_at_level_one");
            }
        }
    }
    graph.interrupt_on({{"mode", "blocking_screen"}}, "combat.common_screen_requires_dispatch");
    return graph.finish();
}
}
