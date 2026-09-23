#include "global_prompt.hpp"
#include "games/wvd/vision/harken_probes.hpp"

namespace wvd::games::recovery {
tasks::CompiledWorkflow dismiss_global_prompt(GlobalPrompt prompt) {
    using C = tasks::PipelineCompiler;
    using J = nlohmann::json;
    const bool blessing = prompt == GlobalPrompt::Blessing;
    const std::string name = blessing ? "blessing" : "sandman_recover";
    C graph("recovery.global_prompt." + name, std::chrono::seconds{90});
    const auto marker = C::image(name);
    const J known{{"mode", "boot_post"}};
    const auto changed = C::all({known, C::absent(marker)});
    graph.route("Entry", blessing ? J{"HarkenChoice", "Choose0"} : J{"Choose0"});
    if (blessing) {
        // 祝福名称随机，三处信息图标与底部放弃项共同证明当前是四选一页面。
        graph.fixed_click("HarkenChoice", vision::harken_buff_menu(), vision::harken_floor_menu(),
                          {450, 987}, {"HarkenReturned"});
        graph.postcondition_budget("HarkenChoice", 15000);
        graph.observe("HarkenReturned", vision::harken_floor_menu(), {"Terminal"});
    }
    // 旧祝福二次确认优先关闭，不能继续点背景中的祝福按钮。
    // 每次输入都重新定位；六次无效后退出，不把下一页当成连点目标。
    for (unsigned i = 0; i < 6; ++i) {
        const auto suffix = std::to_string(i);
        const auto retry = i < 5 ? "Choose" + std::to_string(i + 1) : "Unchanged";
        graph.route("Choose" + suffix, blessing ? J{"Close" + suffix, "Select" + suffix} : J{"Select" + suffix});
        if (blessing) {
            const auto close = C::image("combatClose");
            graph.click("Close" + suffix, C::all({marker, close}), close, known, {"Changed", retry});
            graph.delay_after("Close" + suffix, 2000);
            graph.postcondition_budget("Close" + suffix, 10000);
        }
        const auto scene = blessing ? C::all({marker, C::absent(C::image("combatClose"))}) : marker;
        graph.click("Select" + suffix, scene, marker, known, {"Changed", retry});
        graph.delay_after("Select" + suffix, 2000);
        graph.postcondition_budget("Select" + suffix, 10000);
    }
    graph.observe("Changed", changed, {"Terminal"});
    graph.observe("Unchanged", marker, {"UnchangedExit"});
    graph.recovery("UnchangedExit", "global." + name + ".unchanged");
    return graph.finish();
}
}
