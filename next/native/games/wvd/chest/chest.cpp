#include "chest.hpp"

namespace wvd::games::chest {
using C = tasks::PipelineCompiler;
using J = nlohmann::json;
tasks::CompiledWorkflow open_chest(int preferred_character, bool quick, std::uint32_t seed) {
    if (preferred_character < 0 || preferred_character > 6)
        throw std::runtime_error("CHEST_CHARACTER_INVALID");
    // 实际受控识别链 33 次输入已耗时 240 秒；完整 quick + 普通重试不能
    // 继承旧短预算。总时间仍有界，父 Session 另持总预算，帧 TTL 不变。
    C graph("chest.open", std::chrono::seconds{quick ? 900 : 600});
    const auto flag = C::image("chestFlag"), choose = C::image("whowillopenit"), opening = C::image("chestOpening");
    auto reward = C::image("chest_reward_advance");
    reward["roi"] = {750, 1400, 150, 150};
    const J combat{{"mode", "combat_active"}};
    const auto revive = C::image("RiseAgain"), ambush = C::image("ambush");
    const auto interrupted = C::any({combat, revive, ambush});
    const auto chest = C::all({C::any({flag, choose, opening}), C::absent(interrupted)});
    const auto dungeon = C::all({C::image("dungFlag"), C::absent(interrupted), C::absent(chest), C::absent(reward)});
    const auto post = C::any({reward, chest, dungeon, interrupted});
    // 打开动作必须看到下一阶段；原宝箱按钮仍在不能算成功，避免网络慢时重复点击。
    const auto opened = C::any({choose, opening, reward, dungeon, interrupted});
    graph.route("Entry", {"Reward", "Begin"});
    graph.confirm("Begin", "chest.begin", "chest_observed", chest, quick ? J{"QuickOpen", "Dispatch"} : J{"Dispatch"});
    graph.route("Dispatch", {"Reward", "Combat", "Revive", "Ambush", "Done", "Round0"});
    graph.observe("Reward", reward, {"RewardAdvance"});
    graph.hit_limit("Reward", 20);
    graph.fixed_click("RewardAdvance", reward, post, {832, 1491}, {"AfterReward"});
    graph.hit_limit("RewardAdvance", 20);
    graph.delay_after("RewardAdvance", 900);
    // 奖励可能连续多页，也可能直接返回迷宫或自动走到下一个宝箱。
    graph.route("AfterReward", {"Reward", "Combat", "Revive", "Ambush", "Done", "DoneAtNextChest"});
    graph.hit_limit("AfterReward", 20);
    graph.confirm("DoneAtNextChest", "chest.completed", "dungeon_resumed", flag, {"Terminal"});
    graph.observe("Combat", combat, {"CombatExit"});
    graph.recovery("CombatExit", "chest.combat_requires_dispatch");
    graph.observe("Revive", revive, {"ReviveExit"});
    graph.recovery("ReviveExit", "chest.resurrection_required");
    graph.observe("Ambush", ambush, {"AmbushExit"});
    graph.recovery("AmbushExit", "chest.ambush_requires_dispatch");
    graph.confirm("Done", "chest.completed", "dungeon_resumed", dungeon, {"Terminal"});
    const J exits{"Reward", "Combat", "Revive", "Ambush", "Done"};
    auto after = [&](const std::string &next) {
        auto choices = exits;
        choices.push_back(next);
        return choices;
    };
    if (quick) {
        graph.click("QuickOpen", chest, flag, opened, after("QuickRoute0"));
        graph.hit_limit("QuickOpen", 1);
        graph.delay_after("QuickOpen", 1000);
        graph.postcondition_budget("QuickOpen", 60000);
        // 旧 quick 在 WHO=0 时以 -1 计算，最终位置为第六人；保留该实际坐标语义。
        const int role = preferred_character ? preferred_character - 1 : 5;
        const J position{258 + (role % 3) * 258, 1161 + (role / 3) * 184};
        for (unsigned i = 0; i < 3; ++i) {
            const auto suffix = std::to_string(i);
            auto choices = exits;
            choices.push_back("QuickRole" + suffix);
            choices.push_back("QuickDisarm0");
            graph.route("QuickRoute" + suffix, choices);
            graph.fixed_click("QuickRole" + suffix, C::all({chest, choose}), post, position,
                after(i < 2 ? "QuickRoute" + std::to_string(i + 1) : "QuickDisarm0"));
            graph.hit_limit("QuickRole" + suffix, 1);
            graph.delay_after("QuickRole" + suffix, i < 2 ? 200 : 1000);
        }
        // 每次输入都是新帧意图，不把旧版的循环变成一串不可撤销的底层输入。
        auto disarm = [&](const std::string &name, const std::string &next) {
            graph.fixed_click(name, C::all({chest, C::any({choose, opening})}), post,
                {515, 934}, after(next));
            graph.hit_limit(name, 1);
            graph.delay_after(name, 200);
            graph.stop_if_interrupted_after(name, "chest.disarm_outcome_unconfirmed");
        };
        for (unsigned i = 0; i < 30; ++i)
            disarm("QuickDisarm" + std::to_string(i), i < 29 ? "QuickDisarm" + std::to_string(i + 1) : "QuickDismiss0");
        for (unsigned i = 0; i < 3; ++i) {
            const auto suffix = std::to_string(i);
            graph.fixed_click("QuickDismiss" + suffix, chest, post, {1, 1}, after("QuickFallback" + suffix));
            graph.hit_limit("QuickDismiss" + suffix, 1);
            disarm("QuickFallback" + suffix, i < 2 ? "QuickDismiss" + std::to_string(i + 1) : "Dispatch");
        }
    }
    graph.recovery("NoCharacter", "chest.no_available_character");
    graph.recovery("RetryExit", "chest.retry_pending");
    // 旧 while 每轮重新识别选人/开启状态。一次子调用最多六轮，返回外层后
    // 候选池仍由 Run 持有；父 Session 的总预算不因正常返回而重置。
    for (unsigned round = 0; round < 6; ++round) {
        const auto prefix = "Round" + std::to_string(round);
        const auto next_round = round < 5 ? "Round" + std::to_string(round + 1) : "RetryExit";
        auto choices = exits;
        choices.insert(choices.end(), {prefix + "Open", prefix + "Prepare", prefix + "Disarm0"});
        graph.route(prefix, choices);
        graph.click(prefix + "Open", chest, flag, opened, {prefix});
        graph.hit_limit(prefix + "Open", 3);
        graph.delay_after(prefix + "Open", 1000);
        graph.postcondition_budget(prefix + "Open", 60000);
        J selected{prefix + "Unavailable"};
        for (unsigned role = 0; role < 6; ++role)
            selected.push_back(prefix + "Role" + std::to_string(role));
        graph.chest_selection(prefix + "Prepare", C::all({chest, choose}), preferred_character, seed, selected);
        graph.observe(prefix + "Unavailable", C::business("/chest_has_character", false), {"NoCharacter"});
        for (unsigned role = 0; role < 6; ++role) {
            const auto name = prefix + "Role" + std::to_string(role);
            const int x = 258 + (role % 3) * 258, y = 1161 + (role / 3) * 184;
            auto fear = C::image("chestfear");
            fear["roi"] = {x - 125, y - 82, 250, 164};
            graph.observe(name, C::business("/chest_character", role), {name + "Choose", name + "Fear"});
            graph.fixed_click(name + "Choose", C::all({chest, choose, C::absent(fear)}), post,
                              {x, y}, {prefix + "Attempted"});
            graph.delay_after(name + "Choose", 1500);
            graph.observe(name + "Fear", C::all({chest, choose, fear}), {next_round});
        }
        graph.confirm(prefix + "Attempted", "chest.character", "chest_character_attempted", post,
                      after(prefix + "Disarm0"));
        for (unsigned attempt = 0; attempt < 8; ++attempt) {
            const auto name = prefix + "Disarm" + std::to_string(attempt);
            graph.fixed_click(name, C::all({chest, C::any({choose, opening})}), post, {515, 934},
                after(attempt < 7 ? prefix + "Disarm" + std::to_string(attempt + 1) : next_round));
            graph.hit_limit(name, 1);
            graph.delay_after(name, 300);
            graph.stop_if_interrupted_after(name, "chest.disarm_outcome_unconfirmed");
        }
    }
    graph.interrupt_on(C::all({J{{"mode", "blocking_screen"}}, C::absent(reward), C::absent(interrupted)}),
                       "chest.common_screen_requires_dispatch");
    return graph.finish();
}
}
