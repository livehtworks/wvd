#include "wall_bypass.hpp"
#include <array>

namespace wvd::games::navigation {
tasks::CompiledWorkflow bypass_wall_after_restart() {
    using C = tasks::PipelineCompiler;
    using J = nlohmann::json;
    C graph("navigation.wall_bypass", std::chrono::seconds{60});
    const auto other = C::any({J{{"mode", "blocking_screen"}}, J{{"mode", "combat_active"}},
        C::image("chestFlag"), C::image("whowillopenit"), C::image("RiseAgain"), C::image("mapFlag"),
        C::image("Inn"), C::image("trait"), C::image("recover")});
    const auto dungeon = C::all({C::image("dungFlag"), C::absent(other)});
    const auto known = C::any({dungeon, other});
    graph.route("Entry", {"Interrupted", "Done", "Turn", "Left", "Right"});
    graph.observe("Interrupted", other, {"InterruptedExit"});
    graph.recovery("InterruptedExit", "navigation.wall_bypass_requires_dispatch");
    graph.observe("Done", C::business("/wall_bypass_step", 3), {"Terminal"});
    const std::array<std::string, 3> names{"Turn", "Left", "Right"};
    const std::array<std::string, 3> events{"wall_turn_completed", "wall_left_completed", "wall_right_completed"};
    // 只保存已发送并得到新帧确认的阶段，不保存坐标/旧识别许可。
    // 中途进入战斗或弹窗交回外层；再次进入地下城从未完成阶段继续。
    for (std::size_t i = 0; i < names.size(); ++i) {
        const auto &name = names[i];
        graph.observe(name, C::business("/wall_bypass_step", i), {name + "Input"});
        if (i == 0)
            graph.swipe(name + "Input", dungeon, known, {300, 950, 600, 950}, {name + "Confirmed"});
        else
            graph.fixed_click(name + "Input", dungeon, known, {i == 1 ? 27 : 853, 950}, {name + "Confirmed"});
        if (i < 2)
            graph.delay_after(name + "Input", 1000);
        graph.confirm(name + "Confirmed", "wall." + name, events[i], known, {"Entry"});
    }
    return graph.finish();
}
}
