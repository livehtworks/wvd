#include "harken_exit.hpp"
#include "games/wvd/recovery/global_prompt.hpp"
#include "games/wvd/vision/harken_probes.hpp"
#include "games/wvd/vision/location_probes.hpp"
#include "games/wvd/vision/dialogue_probes.hpp"

namespace wvd::games::navigation {
tasks::CompiledWorkflow leave_harken() {
    using C = tasks::PipelineCompiler;
    C graph("navigation.harken_exit", std::chrono::seconds{180});
    const auto story = vision::ordinary_story_page();
    // 返城可能先触发队友剧情。背景仍是王城不能代表建筑菜单已恢复。
    const auto city = C::all({vision::city_screen(), C::absent(story)});
    const auto buff = vision::harken_buff_menu();
    const auto floors = vision::harken_floor_menu();
    const auto outskirts = vision::outskirts_return_button();
    graph.route("Entry", {"Story", "City", "Buff", "Floor", "Outskirts"});
    graph.observe("City", city, {"Terminal"});
    const auto choose_buff = graph.define_child("BuffChoice", recovery::dismiss_global_prompt(recovery::GlobalPrompt::Blessing));
    graph.observe("Buff", buff, {"ChooseBuff"});
    graph.call_child("ChooseBuff", choose_buff, {"Entry"});
    graph.click("Floor", floors, vision::harken_return_button(), C::any({outskirts, city, story}), {"Entry"});
    graph.postcondition_budget("Floor", 20000);
    // 剧情是已经离开郊外的证据，不重放归还；剧情结束后仍须独立确认城市。
    graph.click("Outskirts", outskirts, outskirts, C::any({city, story}), {"Entry"});
    graph.postcondition_budget("Outskirts", 20000);
    graph.click("Story", story, vision::story_advance_arrow(), C::any({story, city}), {"Entry"});
    graph.delay_after("Story", 700);
    graph.hit_limit("Story", 50);
    graph.hit_limit("Entry", 64);
    for (const auto *name : {"Buff", "ChooseBuff", "Floor", "Outskirts"})
        graph.hit_limit(name, 6);
    return graph.finish();
}
} // namespace wvd::games::navigation
