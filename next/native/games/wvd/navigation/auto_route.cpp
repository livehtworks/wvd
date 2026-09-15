#include "auto_route.hpp"

namespace wvd::games::navigation {
using J = nlohmann::json;
using C = tasks::PipelineCompiler;
tasks::CompiledWorkflow auto_route(const std::string &target) {
    if (target != "chest_auto" && target != "mark_auto" && target != "dungFlag" && target != "stay")
        throw std::runtime_error("AUTO_ROUTE_TARGET_INVALID");
    C graph("navigation.auto_route");
    const auto map = C::image("mapFlag");
    const auto dungeon = C::image("dungFlag");
    const J combat{{"mode", "combat_active"}};
    const auto chest = C::any({C::image("chestFlag"), C::image("chestOpening"), C::image("whowillopenit")});
    const auto encounter = C::any({combat, chest, C::image("RiseAgain")});
    const auto outside = C::all({C::any({C::image("Inn"), C::image("EdgeOfTown"), C::image("returnText"),
                                        C::image("returntoTown"), C::image("openworldmap"), C::image("worldmapflag")}),
                                 C::absent(map), C::absent(encounter)});
    const auto moving = C::all({dungeon, C::absent(map), C::absent(encounter), C::absent(outside)});
    const auto no_target = C::any({C::image("NoChestCanBeFound"), C::image("theRouteToTheDestinationCannotBeFound")});
    const J post{{"mode", "auto_route_post"}};
    graph.observe("Encounter", encounter, {"EncounterExit"});
    graph.recovery("EncounterExit", "navigation.auto_encounter_requires_dispatch");
    graph.recovery("StoppedExit", "navigation.auto_stopped_requires_dispatch");
    if (target == "stay") {
        graph.route("Entry", {"Encounter", "Outside", "Wait"});
        graph.observe("Wait", moving, {"Encounter", "Outside", "StoppedExit"});
        graph.delay_after("Wait", 2000);
        // stay 永远不是完成一个任务点，只有显式插入出口供外层继续等待。
        graph.observe("Outside", outside, {"Terminal"});
    } else {
        auto button = C::image(target);
        button["roi"] = {720, 250, 150, 180};
        auto available = button;
        if (target == "chest_auto") {
            auto minus = C::image("chest_auto_minus");
            minus.update({{"roi", {811, 340, 41, 30}},
                          {"preprocess", {{"operation", "subtract"}, {"rgb", {90, 90, 90}}}}});
            available = C::all({button, minus});
        }
        graph.route("Entry", {"Encounter", "Retreated", "Done", "CloseMap", "Ready", "Expand"});
        graph.observe("Retreated", outside, {"Terminal"});
        graph.observe("Done", C::all({no_target, C::absent(encounter)}), {"Terminal"});
        graph.back("CloseMap", C::all({map, C::absent(encounter)}), post, {"Entry"});
        graph.observe("Ready", C::all({moving, button}), {"Choose", "Unavailable"});
        graph.fixed_click("Expand", C::all({moving, C::absent(button)}), post, {762, 346}, {"Encounter", "Ready"});
        graph.hit_limit("Expand", 1);
        graph.click("Choose", C::all({moving, available}), button, post,
                    {"Encounter", "Retreated", "Done", "Resume", "Moving"});
        graph.hit_limit("Choose", 1);
        if (target == "chest_auto") {
            graph.click("Unavailable", C::all({moving, button, C::absent(available)}), button, post,
                        {"Encounter", "UnavailableExit"});
            graph.recovery("UnavailableExit", "navigation.auto_button_unavailable");
        } else
            graph.observe("Unavailable", C::all({moving, C::absent(button)}), {"StoppedExit"});
        graph.click("Resume", moving, C::image("resume"), post, {"Encounter", "Retreated", "Done", "Moving"});
        graph.hit_limit("Resume", 1);
        graph.observe("Moving", moving, {"Encounter", "Retreated", "Done", "Stopped", "Moving"});
        graph.hit_limit("Moving", 100);
        graph.observe("Stopped", C::all({moving, J{{"mode", "movement_stopped"}}}), {"StoppedExit"});
    }
    graph.interrupt_on({{"mode", "blocking_screen"}, {"parallel_basic", true}}, "navigation.common_screen_requires_dispatch");
    return graph.finish();
}
}
