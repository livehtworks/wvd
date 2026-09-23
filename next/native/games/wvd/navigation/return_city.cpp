#include "return_city.hpp"
#include "auto_route.hpp"
#include "harken_exit.hpp"
#include "games/wvd/vision/harken_probes.hpp"
#include "games/wvd/vision/location_probes.hpp"

namespace wvd::games::navigation {
tasks::CompiledWorkflow return_to_fortress() {
    using C = tasks::PipelineCompiler;
    C graph("navigation.return_to_fortress", std::chrono::seconds{240});
    const auto inn = vision::inn_button();
    const auto harken = C::any({vision::harken_buff_menu(), vision::harken_floor_menu(),
                                vision::outskirts_return_button()});
    const auto normal = C::any({inn, C::image("EdgeOfTown"), C::image("returntotown"),
        C::image("returnText"), C::image("leaveDung"), C::image("blessing"), harken});
    graph.route("Entry", {"Done", "Harken", "Exit0", "Exit1", "Exit2", "Exit3", "Dungeon", "Dismiss"});
    graph.observe("Done", inn, {"Terminal"});
    const auto harken_exit = graph.define_child("HarkenExit", leave_harken());
    graph.observe("Harken", harken, {"LeaveHarken"});
    graph.call_child("LeaveHarken", harken_exit, {"Entry"});
    std::size_t index = 0;
    for (const auto *name : {"returntotown", "returnText", "leaveDung", "blessing"}) {
        const auto node = "Exit" + std::to_string(index++), marker = std::string(name);
        graph.click(node, C::all({C::image(marker), C::absent(inn)}), C::image(marker), normal, {"Entry"});
        graph.delay_after(node, 2000); graph.hit_limit(node, 16);
    }
    const auto return_harken = graph.define_child("ReturnHarken", auto_route("dungFlag"), {"BlockedExit"});
    graph.observe("Dungeon", C::all({C::image("dungFlag"), C::absent(C::image("mapFlag"))}), {"GoHarken"});
    graph.call_child("GoHarken", return_harken, {"Entry"});
    graph.fixed_click("Dismiss", C::all({C::image("EdgeOfTown"), C::absent(inn)}), normal, {1, 1}, {"Entry"});
    graph.delay_after("Dismiss", 2000); graph.hit_limit("Dismiss", 16); graph.hit_limit("Entry", 64);
    return graph.finish();
}
}
