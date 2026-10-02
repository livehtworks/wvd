#include "auto_route.hpp"
#include "games/wvd/vision/harken_probes.hpp"

namespace wvd::games::navigation {
using J = nlohmann::json;
using C = tasks::PipelineCompiler;
tasks::CompiledWorkflow auto_route(const std::string &target) {
    if (target != "chest_auto" && target != "mark_auto" && target != "dungFlag" && target != "stay")
        throw std::runtime_error("AUTO_ROUTE_TARGET_INVALID");
    C graph("navigation.auto_route");
    graph.check_policy("navigation", {"wvd-network-retry", "wvd-pause", "wvd-download", "wvd-story", "wvd-blessing", "wvd-karma", "wvd-dialogue", "wvd-special-dialogue", "wvd-sandman"});
    const auto map = C::image("mapFlag");
    const J combat{{"mode", "combat_active"}};
    const auto chest = C::any({C::image("chestFlag"), C::image("chestOpening"), C::image("whowillopenit")});
    const auto encounter = C::any({combat, chest, C::image("RiseAgain")});
    const auto outside = C::all({C::any({C::image("Inn"), C::image("EdgeOfTown"), C::image("returnText"),
                                        C::image("returntoTown"), C::image("openworldmap"), C::image("worldmapflag")}),
                                 C::absent(map), C::absent(encounter)});
    const auto harken = C::any({vision::harken_buff_menu(), vision::harken_floor_menu(),
                                vision::outskirts_return_button()});
    const auto retreated = target == "dungFlag" ? C::any({outside, harken}) : outside;
    const J moving{{"mode", "auto_route_moving"}};
    const auto no_target = C::any({C::image("NoChestCanBeFound"), C::image("theRouteToTheDestinationCannotBeFound")});
    const J post = target == "dungFlag" ? C::any({J{{"mode", "auto_route_post"}}, harken})
                                          : J{{"mode", "auto_route_post"}};
    graph.observe("Encounter", encounter, {"EncounterExit"});
    graph.handoff("EncounterExit", "encounter");
    graph.handoff("StoppedExit", "stopped");
    if (target == "stay") {
        graph.route("Entry", {"Encounter", "Outside", "Wait"});
        graph.observe("Wait", moving, {"Encounter", "Outside", "StoppedExit"});
        graph.delay_after("Wait", 2000);
        // stay 永远不是完成一个任务点，只有显式插入出口供外层继续等待。
        graph.observe("Outside", outside, {"Terminal"});
    } else {
        auto button = C::image(target);
        button["roi"] = {680, 220, 220, 240};
        auto available = button;
        if (target == "chest_auto") {
            auto minus = C::image("chest_auto_minus");
            minus.update({{"roi", {760, 280, 140, 140}},
                          {"preprocess", {{"operation", "subtract"}, {"rgb", {90, 90, 90}}}}});
            available = C::all({button, minus});
        }
        graph.route("Entry", {"Encounter", "Retreated", "Done", "CloseMap", "Ready", "Expand"});
        graph.observe("Retreated", retreated, {"Terminal"});
        graph.observe("Done", C::all({no_target, C::absent(encounter)}), {"Terminal"});
        graph.back("CloseMap", C::all({map, C::absent(encounter)}), post, {"Entry"});
        graph.observe("Ready", C::all({moving, button}), {"Choose", "Unavailable"});
        graph.fixed_click("Expand", C::all({moving, C::absent(button)}),
            C::any({encounter, retreated, no_target, C::all({moving, button})}), {762, 346}, {"Encounter", "Retreated", "Done", "Ready"});
        graph.retry_menu_input("Expand", C::all({moving, C::absent(button)}), 3000);
        graph.hit_limit("Expand", 1);
        graph.click("Choose", C::all({moving, available}), button, post,
                    {"Encounter", "Retreated", "Done", "Resume", "Moving"});
        graph.hit_limit("Choose", 1);
        if (target == "chest_auto") {
            graph.click("Unavailable", C::all({moving, button, C::absent(available)}), button, post,
                        {"Encounter", "UnavailableExit"});
            graph.handoff("UnavailableExit", "unavailable");
        } else
            graph.observe("Unavailable", C::all({moving, C::absent(button)}), {"StoppedExit"});
        graph.public_step("Resume", "navigation-resume", J::object(), {"Encounter", "Retreated", "Done", "Moving"}, J::object(), C::all({moving, C::image("resume")}));
        graph.hit_limit("Resume", 1);
        graph.poll("Moving", 250, {"Encounter", "Retreated", "Done", "Stopped", "Moving"}, moving,
            J{{"mode", "region_changed"}, {"channel", "navigation"}, {"roi", {650, 25, 225, 225}}});
        graph.failure_route("Moving", {"StalledExit"});
        graph.recovery("StalledExit", "navigation.input_no_progress");
        graph.observe("Stopped", C::all({moving, J{{"mode", "movement_stopped"}}}), {"StoppedExit"});
    }
    graph.interrupt_on(C::absent(J{{"mode", "input_clear"}}), "navigation.common_screen_requires_dispatch", "blocked");
    return graph.finish();
}
}
