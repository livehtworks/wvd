#include "chest.hpp"
#include <algorithm>
#include <array>
#include <random>

namespace wvd::games::chest {
using C = tasks::PipelineCompiler;
using J = nlohmann::json;
tasks::CompiledWorkflow open_chest(int preferred_character, bool quick, std::uint32_t seed) {
    if (preferred_character < 0 || preferred_character > 6)
        throw std::runtime_error("CHEST_CHARACTER_INVALID");
    C graph("chest.open", std::chrono::seconds{240});
    const auto flag = C::image("chestFlag"), choose = C::image("whowillopenit"), opening = C::image("chestOpening");
    const J combat{{"mode", "combat_active"}};
    const auto revive = C::image("RiseAgain"), ambush = C::image("ambush");
    const auto interrupted = C::any({combat, revive, ambush});
    const auto chest = C::all({C::any({flag, choose, opening}), C::absent(interrupted)});
    const auto dungeon = C::all({C::image("dungFlag"), C::absent(interrupted), C::absent(chest)});
    const auto post = C::any({chest, dungeon, interrupted});
    graph.route("Entry", {"Begin"});
    graph.confirm("Begin", "chest.begin", "chest_observed", chest, quick ? J{"QuickOpen", "Dispatch"} : J{"Dispatch"});
    graph.route("Dispatch", {"Combat", "Revive", "Ambush", "Done", "Open", "Select0", "Disarm"});
    graph.observe("Combat", combat, {"CombatExit"});
    graph.recovery("CombatExit", "chest.combat_requires_dispatch");
    graph.observe("Revive", revive, {"ReviveExit"});
    graph.recovery("ReviveExit", "chest.resurrection_required");
    graph.observe("Ambush", ambush, {"AmbushExit"});
    graph.recovery("AmbushExit", "chest.ambush_requires_dispatch");
    graph.confirm("Done", "chest.completed", "dungeon_resumed", dungeon, {"Terminal"});
    if (quick) {
        const J exits{"Combat", "Revive", "Ambush", "Done"};
        auto after = [&](const std::string &next) {
            auto choices = exits;
            choices.push_back(next);
            return choices;
        };
        graph.click("QuickOpen", chest, flag, post, after("QuickRoute0"));
        graph.hit_limit("QuickOpen", 1);
        graph.delay_after("QuickOpen", 1000);
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
    graph.click("Open", chest, flag, post, {"Dispatch"});
    std::array<int, 6> order{0, 1, 2, 3, 4, 5};
    std::mt19937 random(seed);
    std::shuffle(order.begin(), order.end(), random);
    if (preferred_character)
        std::rotate(order.begin(), std::find(order.begin(), order.end(), preferred_character - 1), order.end());
    for (std::size_t i = 0; i < order.size(); ++i) {
        const int x = 258 + (order[i] % 3) * 258, y = 1161 + (order[i] / 3) * 184;
        auto fear = C::image("chestfear");
        fear["roi"] = {x - 125, y - 82, 250, 164};
        const auto name = std::to_string(i);
        graph.observe("Select" + name, C::all({chest, choose}),
                      {"Combat", "Revive", "Ambush", "Done", "Choose" + name, "Fear" + name});
        graph.fixed_click("Choose" + name, C::all({chest, choose, C::absent(fear)}), post,
                          {x, y}, {"Combat", "Revive", "Ambush", "Done", "Disarm"});
        graph.delay_after("Choose" + name, 1500);
        // 恐惧角色没有选择输入；全部不可用明确失败，不访问空随机列表。
        graph.observe("Fear" + name, C::all({chest, choose, fear}),
                      i + 1 < order.size() ? J{"Select" + std::to_string(i + 1)} : J{"NoCharacter"});
    }
    graph.recovery("NoCharacter", "chest.no_available_character");
    // 连点只表示多次独立意图。每个点之前重识别，战斗一出现就撤销后续拆陷阱。
    graph.fixed_click("Disarm", C::all({chest, C::any({choose, opening})}), post,
                      {515, 934}, {"Combat", "Revive", "Ambush", "Done", "Disarm", "Dispatch"});
    graph.delay_after("Disarm", 300);
    graph.hit_limit("Disarm", 8);
    graph.stop_if_interrupted_after("Disarm", "chest.disarm_outcome_unconfirmed");
    graph.interrupt_on({{"mode", "blocking_screen"}}, "chest.common_screen_requires_dispatch");
    return graph.finish();
}
}
