#include "gold_income.hpp"
#include "games/wvd/quests/gold_income.hpp"
#include "games/wvd/navigation/time_leap.hpp"
#include "games/wvd/navigation/world_travel.hpp"
#include "games/wvd/navigation/return_city.hpp"
#include "games/wvd/vision/location_probes.hpp"

namespace wvd::games::tasks {
namespace {
using C = PipelineCompiler;
using J = nlohmann::json;
J option(const char *name) { return C::image(std::string("7000G/") + name); }
CompiledWorkflow accept_job() {
    C graph("quest.7000G.accept", std::chrono::seconds{180});
    const auto guild = C::image("guild"), go = option("illgonow"), hungry = option("iminhungry"), district = option("olddist");
    const auto story = C::any({C::image("fastforward"), option("royalcapital")});
    graph.route("Entry", {"Guild", "Go", "District", "Hungry"});
    graph.click("Guild", guild, guild, go, {"Go"});
    graph.click("Go", go, go, C::any({district, hungry}), {"WaitForStory"});
    // 旧流程等待15秒；拆开保留单节点10秒上限，并在后半段重新确认页面。
    graph.delay_after("Go", 10000); graph.postcondition_budget("Go", 22000);
    graph.observe("WaitForStory", C::any({district, hungry}), {"District", "Hungry"});
    graph.delay_after("WaitForStory", 5000);
    graph.click("Hungry", C::all({hungry, C::absent(district)}), hungry, district, {"District"});
    graph.click("District", district, district, story, {"Terminal"});
    return graph.finish();
}
CompiledWorkflow royal_capital() {
    C graph("quest.7000G.royalcapital", std::chrono::seconds{120});
    const auto capital = option("royalcapital"), world = C::image("intoWorldMap"), fast = C::image("fastforward");
    const auto story = C::any({capital, fast});
    graph.observe("Entry", story, {"Dismiss0"});
    graph.delay_after("Entry", 4000);
    graph.fixed_click("Dismiss0", story, story, {1, 1}, {"Dismiss1"});
    graph.fixed_click("Dismiss1", story, story, {1, 1}, {"Capital", "More"});
    graph.delay_after("Dismiss1", 8000);
    graph.click("Capital", capital, capital, C::any({world, fast}), {"Done", "UntilWorld"});
    graph.fixed_click("More", C::all({fast, C::absent(capital)}), story, {1, 1}, {"Capital", "More"});
    graph.fixed_click("UntilWorld", C::all({fast, C::absent(world)}), C::any({world, fast}), {1, 1}, {"Done", "UntilWorld"});
    graph.observe("Done", world, {"Terminal"});
    for (const auto *name : {"More", "UntilWorld"}) { graph.hit_limit(name, 32); graph.delay_after(name, 2000); }
    return graph.finish();
}
CompiledWorkflow ask_person(int person) {
    if (person < 0 || person > 2) throw std::runtime_error("GOLD_INCOME_PERSON_INVALID");
    C graph("quest.7000G.person." + std::to_string(person), std::chrono::seconds{180});
    const auto world = C::image("intoWorldMap"), fast = C::image("fastforward"), why = option("why");
    const auto leave = option("leavethechild"), refuse = option("icantagreewithU");
    const auto target = person == 2 ? leave : world;
    const auto story = C::any({fast, why, target});
    const std::array<J, 3> positions{J{450, 1111}, J{200, 1180}, J{680, 1200}};
    graph.route("Entry", {"AlreadyTalking", "Person"});
    graph.observe("AlreadyTalking", fast, {"Talk"});
    graph.fixed_click("Person", C::all({world, C::absent(fast)}), C::any({world, story}), positions.at(person), {"AlreadyTalking", "Person"});
    graph.route("Talk", {"Arrived", "Why", "Dismiss"});
    graph.observe("Arrived", target, person == 2 ? J{"Leave"} : J{"Terminal"});
    graph.click("Why", C::all({why, C::absent(target)}), why, story, {"Talk"});
    graph.fixed_click("Dismiss", C::all({fast, C::absent(target), C::absent(why)}), story, {1, 1}, {"Talk"});
    if (person == 2) graph.click("Leave", leave, leave, C::any({refuse, fast}), {"Done", "AfterLeave"});
    if (person == 2) {
        graph.observe("Done", refuse, {"Terminal"});
        graph.fixed_click("AfterLeave", C::all({fast, C::absent(refuse)}), C::any({refuse, fast}), {1, 1}, {"Done", "AfterLeave"});
        graph.delay_after("AfterLeave", 1000); graph.hit_limit("AfterLeave", 32);
    }
    for (const auto *name : {"Person", "Talk", "Why", "Dismiss"}) graph.hit_limit(name, 32);
    graph.delay_after("Why", 2000); graph.delay_after("Dismiss", 2000);
    return graph.finish();
}
CompiledWorkflow refuse() {
    C graph("quest.7000G.refuse", std::chrono::seconds{120});
    const auto no = option("icantagreewithU"), go = option("illgo"), district = option("olddist"), fast = C::image("fastforward");
    const auto next = C::any({go, district, fast});
    graph.click("Entry", no, no, next, {"Done", "District", "Dismiss"});
    graph.observe("Done", go, {"Terminal"});
    graph.click("District", C::all({district, C::absent(go)}), district, next, {"Done", "Dismiss"});
    graph.fixed_click("Dismiss", C::all({fast, C::absent(go), C::absent(district)}), next, {1, 1}, {"Done", "District", "Dismiss"});
    graph.hit_limit("Dismiss", 32); graph.delay_after("Dismiss", 1000);
    return graph.finish();
}
CompiledWorkflow volunteer() {
    C graph("quest.7000G.volunteer", std::chrono::seconds{120});
    const auto go = option("illgo"), hard = option("noeasytask"), ruins = C::image("ruins"), fast = C::image("fastforward");
    graph.click("Entry", go, go, C::any({hard, fast}), {"Hard", "ToHard"});
    graph.fixed_click("ToHard", C::all({fast, C::absent(hard)}), C::any({hard, fast}), {1, 1}, {"Hard", "ToHard"});
    graph.click("Hard", hard, hard, C::any({ruins, fast}), {"Done", "ToRuins"});
    graph.fixed_click("ToRuins", C::all({fast, C::absent(ruins)}), C::any({ruins, fast}), {1, 1}, {"Done", "ToRuins"});
    graph.observe("Done", ruins, {"Terminal"});
    for (const auto *name : {"ToHard", "ToRuins"}) { graph.hit_limit(name, 32); graph.delay_after(name, 1000); }
    return graph.finish();
}
}
CompiledWorkflow gold_income_cycle(const WvdQuestDefinition &definition, bool allow_download) {
    if (definition.type != "quest" || definition.id != "7000G") throw std::runtime_error("GOLD_INCOME_TASK_INVALID");
    C graph("tasks.7000G", std::chrono::seconds{1500});
    const auto leap_page = C::any({C::image("cursedWheelTitle"), C::image("cursedWheel"), C::image("ruins")});
    const auto inn = C::image("Inn"), world = C::image("intoWorldMap");
    const auto royal_city = vision::royal_city();
    const auto outside = C::any({inn, C::image("EdgeOfTown"), C::image("returntotown"), C::image("returnText"), C::image("leaveDung"), C::image("blessing")});
    const auto story = C::any({C::image("fastforward"), option("royalcapital")});
    const std::array<J, 11> scenes{leap_page, outside, inn, royal_city, story, world, world, world,
        option("icantagreewithU"), option("illgo"), C::image("ruins")};
    std::vector<CompiledWorkflow> steps;
    steps.push_back(navigation::time_leap_without_causality("FortressArrival", "cursedwheel_impregnableFortress", allow_download));
    steps.push_back(navigation::return_to_fortress());
    steps.push_back(navigation::travel_city_to_city({"City_RoyalCityLuknalia", TaskSwipe{{450, 150}, {500, 150}}, {550, 1}}));
    steps.push_back(accept_job()); steps.push_back(royal_capital());
    for (int i = 0; i < 3; ++i) steps.push_back(ask_person(i));
    steps.push_back(refuse()); steps.push_back(volunteer());
    graph.route("Entry", {"Pending", "Active", "Start"});
    graph.observe("Pending", C::business("/gold_income/pending", true), {"Uncertain"});
    graph.recovery("Uncertain", "quest.gold_income_outcome_unconfirmed");
    graph.observe("Active", C::business("/gold_income/active", true), {"Stage"});
    graph.confirm("Start", "gold.start", "gold_income_started", leap_page, {"Stage"});
    J stages = J::array();
    for (std::size_t i = 0; i < steps.size(); ++i) {
        const auto n = std::to_string(i);
        stages.push_back("Phase" + n);
        graph.observe("Phase" + n, C::all({C::business("/gold_income/phase", i), C::business("/gold_income/unit_matches", true)}), {"Prepare" + n});
        graph.confirm("Prepare" + n, "gold.prepare." + n, "gold_income_prepared", scenes[i], {"Execute" + n});
        const auto child = graph.define_child("Step" + n, steps[i]);
        graph.call_child("Execute" + n, child, {"Confirmed" + n});
        graph.confirm("Confirmed" + n, "gold.done." + n, "gold_income_advanced", scenes[i + 1], i + 1 == steps.size() ? J{"Terminal"} : J{"Stage"});
        if (i == 0) graph.delay_after("Confirmed" + n, 10000);
    }
    graph.route("Stage", stages);
    // 十个真实业务阶段共用此路由；默认五次节点命中不能截断第六阶段。
    graph.hit_limit("Stage", static_cast<int>(steps.size()));
    // 默认对话不能代替7000G明确的剧情阶段；未确认输入保留pending，不借重启重放。
    graph.interrupt_on({{"mode", "blocking_screen"}, {"parallel_basic", true}}, "quest.gold_income_common_screen_requires_dispatch");
    return graph.finish();
}
}
