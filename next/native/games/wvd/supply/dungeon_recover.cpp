#include "dungeon_recover.hpp"

namespace wvd::games::supply {
tasks::CompiledWorkflow recover_in_dungeon() {
    using C = tasks::PipelineCompiler;
    using J = nlohmann::json;
    C graph("supply.dungeon_recover");
    const J combat{{"mode", "combat_active"}};
    const auto chest = C::any({C::image("chestFlag"), C::image("whowillopenit"), C::image("chestOpening")});
    const auto interrupted = C::any({combat, chest, C::image("RiseAgain")});
    const auto trait = C::image("trait"), recover = C::image("recover");
    const auto dungeon = C::all({C::image("dungFlag"), C::absent(C::image("mapFlag")),
                                 C::absent(trait), C::absent(recover), C::absent(interrupted)});
    const auto panel = C::all({C::any({trait, recover}), C::absent(interrupted)});
    const auto context = C::any({dungeon, panel});
    const auto post = C::any({context, interrupted});
    const auto needed = C::business("/healing_required", true);
    graph.route("Entry", {"Encounter", "Unneeded", "Requested"});
    graph.observe("Encounter", interrupted, {"EncounterExit"});
    graph.recovery("EncounterExit", "supply.recover_interrupted");
    graph.observe("Unneeded", C::all({dungeon, C::absent(needed)}), {"Terminal"});
    // 业务条件只能选路，不提供视觉许可。确认动作重新截图证明场景，
    // confirm_event 在同一状态所有者中再次检查需求，不能跳过需求直接声明完成。
    graph.observe("Requested", C::all({context, needed}), {"Begin"});
    graph.confirm("Begin", "heal.request", "healing_requested", context,
                   {"Encounter", "Ready", "Story", "Trait", "Open0"});
    auto story = C::image("story");
    story["roi"] = {676, 800, 220, 108};
    // 原版最多轮流尝试 31 次前排角色；每次都重新证明仍在地下城，而非盲点面板下的坐标。
    for (int i = 0; i <= 30; ++i) {
        const auto suffix = std::to_string(i);
        graph.fixed_click("Open" + suffix, dungeon, post, {36 + (i % 3) * 286, 1425},
            {"Encounter", "Ready", "Story", "Trait", i < 30 ? "Open" + std::to_string(i + 1) : "OpenFailed"});
        graph.hit_limit("Open" + suffix, 1);
        graph.delay_after("Open" + suffix, 2000);
    }
    graph.recovery("OpenFailed", "supply.character_panel_not_opened");
    graph.observe("Ready", C::all({panel, recover}), {"Recover"});
    graph.fixed_click("Story", C::all({panel, trait, story, C::absent(recover)}), post,
        {725, 850}, {"Encounter", "Ready", "SeekRecover"});
    graph.fixed_click("Trait", C::all({panel, trait, C::absent(story), C::absent(recover)}), post,
        {830, 850}, {"Encounter", "Ready", "SeekRecover"});
    graph.fixed_click("SeekRecover", C::all({panel, trait, C::absent(recover)}), post,
        {833, 843}, {"Encounter", "Ready", "SeekRecover"});
    graph.delay_after("SeekRecover", 1000);
    graph.fixed_click("Recover", C::all({panel, recover}), post, {600, 1200},
        {"Encounter", "Recovered", "Back0"});
    graph.hit_limit("Recover", 1);
    graph.delay_after("Recover", 1000);
    // 返回只在仍有角色面板证据时执行；已回地下城则立即停，最多五次返回。
    for (int i = 0; i < 5; ++i) {
        graph.back("Back" + std::to_string(i), panel, post,
            {"Encounter", "Recovered", i < 4 ? "Back" + std::to_string(i + 1) : "ReturnFailed"});
        graph.hit_limit("Back" + std::to_string(i), 1);
        graph.delay_after("Back" + std::to_string(i), 300);
    }
    graph.recovery("ReturnFailed", "supply.recover_panel_not_closed");
    graph.confirm("Recovered", "heal.complete", "healing_completed", dungeon, {"Terminal"});
    graph.interrupt_on({{"mode", "blocking_screen"}}, "supply.common_screen_requires_dispatch");
    return graph.finish();
}
}
