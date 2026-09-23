#include "harken_exit.hpp"
#include "games/wvd/recovery/global_prompt.hpp"
#include "games/wvd/vision/harken_probes.hpp"
#include "games/wvd/vision/location_probes.hpp"

namespace wvd::games::navigation {
tasks::CompiledWorkflow leave_harken() {
    using C = tasks::PipelineCompiler;
    C graph("navigation.harken_exit", std::chrono::seconds{180});
    const auto city = vision::city_screen();
    const auto buff = vision::harken_buff_menu();
    const auto floors = vision::harken_floor_menu();
    const auto outskirts = vision::outskirts_return_button();
    graph.route("Entry", {"City", "Buff", "Floor", "Outskirts"});
    graph.observe("City", city, {"Terminal"});
    const auto choose_buff = graph.define_child("BuffChoice", recovery::dismiss_global_prompt(recovery::GlobalPrompt::Blessing));
    graph.observe("Buff", buff, {"ChooseBuff"});
    graph.call_child("ChooseBuff", choose_buff, {"Entry"});
    graph.click("Floor", floors, vision::harken_return_button(), C::any({outskirts, city}), {"Entry"});
    graph.postcondition_budget("Floor", 20000);
    graph.click("Outskirts", outskirts, outskirts, city, {"Entry"});
    graph.postcondition_budget("Outskirts", 20000);
    for (const auto *name : {"Entry", "Buff", "ChooseBuff", "Floor", "Outskirts"})
        graph.hit_limit(name, 6);
    return graph.finish();
}
} // namespace wvd::games::navigation
