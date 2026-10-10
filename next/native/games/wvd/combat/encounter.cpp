#include "encounter.hpp"
#include "enemy_rules.hpp"
#include "games/wvd/vision/chest_probes.hpp"
#include "games/wvd/vision/combat_phase_probes.hpp"

namespace wvd::games::combat {
tasks::CompiledWorkflow fight_encounter(const nlohmann::json &profile,
                                         const std::set<std::string> &available_images,
                                         EncounterEnd end) {
    using C = tasks::PipelineCompiler;
    using J = nlohmann::json;
    C graph("combat.encounter");
    graph.check_policy("combat", {"wvd-network-retry", "wvd-pause", "wvd-download", "wvd-party-death", "wvd-party-defeat"});
    const J battle{{"mode", "combat_active"}};
    const J progress{{"mode", "region_changed"}, {"channel", "combat"}, {"roi", {15, 40, 145, 800}}};
    auto baseline = progress, stalled = progress;
    baseline["reset"] = true;
    stalled["mode"] = "region_stalled";
    graph.observe("StartProgress", baseline, {"Turn0"});
    graph.observe("NoProgress", C::all({battle, stalled}), {"AutoTimeout"});
    const auto dungeon = vision::combat_phase("dungeon");
    const auto chest = vision::combat_phase("chest");
    const bool repel = end == EncounterEnd::RepelPrompt;
    const auto special = profile.at("TASK_POINT_STRATEGY").value("special_combat", J::object());
    const bool detect_skull = !repel && special.value("skull", false);
    const bool detect_portrait = !repel && special.value("portrait", false);
    if (detect_skull || detect_portrait) {
        auto flee = C::image("flee");
        flee["roi"] = {660, 1080, 240, 220};
        const auto ready = C::all({battle, flee});
        J detectors = J::array();
        J prior = J::array(), branches = J::array();
        const auto rules = enemy_rules(profile);
        if (detect_portrait) for (std::size_t i = 0; i < rules.size(); ++i) {
            auto portrait = C::image(rules[i].image);
            portrait.update({{"roi", {20, 45, 160, 855}}, {"grayscale", true}, {"threshold", 0.88}});
            const auto name = "Enemy" + std::to_string(i);
            auto condition = C::all({ready, portrait});
            if (!prior.empty()) condition = C::all({condition, C::absent(C::any(prior))});
            graph.confirm(name, "combat.begin", "combat_special_observed", condition, {"StartProgress"}, nullptr, rules[i].id);
            branches.push_back(name);
            prior.push_back(portrait);
            detectors.push_back(portrait);
        }
        if (detect_skull) {
            auto skull = C::image("combat_special_skull");
            skull.update({{"roi", {160, 100, 740, 800}}, {"grayscale", true}, {"threshold", 0.84}});
            detectors.push_back(skull);
        }
        if (detect_portrait && rules.empty()) {
            auto portrait = C::image(special.at("portrait_image").get<std::string>());
            portrait.update({{"roi", {20, 45, 160, 855}}, {"grayscale", true}, {"threshold", 0.88}});
            detectors.push_back(portrait);
        }
        const auto match = C::any(detectors);
        for (const auto *branch : {"Special", "Ordinary", "Dungeon", "Chest", "Revive", "WaitingForMenu"}) branches.push_back(branch);
        graph.route("Entry", branches);
        auto fallback = C::all({ready, match});
        if (!prior.empty()) fallback = C::all({fallback, C::absent(C::any(prior))});
        graph.confirm("Special", "combat.begin", "combat_special_observed", fallback, {"StartProgress"});
        graph.confirm("Ordinary", "combat.begin", "combat_observed",
                      C::all({ready, C::absent(match)}), {"StartProgress"});
        graph.poll("WaitingForMenu", 500,
            branches,
            C::all({battle, C::absent(flee)}),
            J{{"mode", "region_changed"}, {"channel", "combat"}, {"roi", {15, 40, 145, 800}}});
        graph.failure_route("WaitingForMenu", {"AutoTimeout"});
    } else {
        graph.route("Entry", {"Observed", "Dungeon", "Chest", "Revive"});
        graph.confirm("Observed", "combat.begin", repel ? "repel_battle_observed" : "combat_observed", battle, {"StartProgress"});
    }
    // 击退敌势力在战后对话结束一场战斗；不扩大普通遭遇的成功条件。
    graph.confirm("Dungeon", "combat.resume", repel ? "repel_battle_completed" : "dungeon_resumed",
        repel ? C::all({C::image("icanstillgo"), C::absent(battle)}) : dungeon, {"Terminal"});
    // A known chest page ends this screen owner; no intervening dungeon frame is required.
    graph.observe("Chest", chest, repel ? J{"UnexpectedEnd"} : J{"ChestExit"});
    if (!repel) graph.handoff("ChestExit", "chest");
    if (repel) graph.recovery("UnexpectedEnd", "quest.repel_unexpected_encounter_end");
    graph.observe("Revive", vision::combat_phase("revival"), {"ReviveExit"});
    graph.handoff("ReviveExit", "revive");
    graph.recovery("AutoTimeout", "combat.auto_progress_timeout");
    auto enabled = C::image("spellskill/CombatAutoEnable"), disabled = C::image("spellskill/CombatAutoDisable");
    enabled["roi"] = disabled["roi"] = {740, 940, 160, 280};
    const auto clear = vision::combat_phase("clear");
    const auto full_auto = C::all({clear, enabled, C::business("/strategy/automatic", true)});
    const auto turn = graph.define_child("Actor", take_turn(profile, available_images), {"BlockedExit"});
    // 子调用每次重新识别当前角色；回到真实终点检查后再处理下一角色。
    // 自动战斗仅在行动条持续无变化时超时，不能以战斗总时长/角色数判失败。
    {
        const std::string name = "Turn0", after = "Turn0";
        graph.call_child(name + "Action", turn, {"Dungeon", "Chest", "Revive", name + "Auto", after},
                         J::object(), battle);
        graph.observe(name + "Auto", full_auto, {name + "Poll"});
        // Auto 就绪不是又一个角色行动。等待可暂时缺图，但不能由此发送新输入；
        // 只有明确退出/关闭 Auto 才继续，独立等待预算耗尽给出可追溯原因。
        // 明确是等待：没有结果时先允许当前作用域分派，再安排下一帧。
        // full_auto 只证明正常自动执行，不能当作“战斗已结束”或输入许可。
        graph.poll(name + "Poll", 250,
            {"Dungeon", "Chest", "Revive", name + "AutoOff", name + "Poll"}, full_auto,
            J{{"mode", "region_changed"}, {"channel", "combat"}, {"roi", {15, 40, 145, 800}}});
        graph.failure_route(name + "Poll", {"AutoTimeout"});
        graph.observe(name + "AutoOff", C::all({clear, disabled, C::absent(enabled)}), {after});
        graph.route(name, {"Dungeon", "Chest", "Revive", "NoProgress", name + "Action"});
    }
    graph.interrupt_on(C::absent(J{{"mode", "input_clear"}}), "combat.common_screen_requires_dispatch", "blocked");
    return graph.finish();
}
}
