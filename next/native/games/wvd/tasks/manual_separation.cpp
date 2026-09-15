#include "manual_separation.hpp"
#include "games/wvd/navigation/dungeon_entry.hpp"
#include "games/wvd/navigation/map_route.hpp"
#include "games/wvd/navigation/time_leap.hpp"
#include "games/wvd/supply/inn.hpp"
#include <algorithm>

namespace wvd::games::tasks {
namespace {
nlohmann::json first_targets() {
    return {{"stair_2", "左下", {827, 547}}, {"harken", "左下"}};
}
}
WvdTaskPlan manual_separation_plan(const WvdQuestDefinition &definition, bool second) {
    if (definition.id != "manualSepDemon" || definition.type != "quest")
        throw std::runtime_error("MANUAL_SEPARATION_TASK_INVALID");
    const auto plan = WvdTaskPlan::parse(definition);
    if (!second) return plan.with_route(first_targets());
    return plan.with_entry({{"press", "COS/COS", {"EdgeOfTown", {1, 1}}, 1},
        {"press", "COS/COSB2F", {1, 1}, 1}})
        .with_route({{"stair_3", "左上", {720, 822}}, {"position", "左上", {79, 447}}});
}
void configure_manual_separation_units(runtime::RunDefinition &definition) {
    if (definition.max_business_units != 1 || !definition.continuation_units.empty())
        throw std::runtime_error("MANUAL_SEPARATION_UNITS_ALREADY_CONFIGURED");
    definition.max_business_units = 2;
    definition.continuation_units.push_back(definition.initial);
}
CompiledWorkflow manual_separation(const WvdQuestDefinition &definition, const nlohmann::json &profile,
    const std::set<std::string> &images, bool allow_download) {
    using C = PipelineCompiler;
    using J = nlohmann::json;
    const auto first_plan = manual_separation_plan(definition, false);
    const auto second_plan = manual_separation_plan(definition, true);
    const auto first_route = traverse_dungeon(first_plan.with_route(J::array({first_targets().at(0)})), profile, images, allow_download);
    const auto second_route = traverse_dungeon(second_plan, profile, images, allow_download);
    // 每个 Session 只走一条路线；第一段的住宿/跳跃和第二段的入本余量不相加为无限段。
    // 恢复包装另外保留120秒，最终仍受原生30分钟定义上限约束。
    C graph("tasks.manualSepDemon", std::max(first_route.time_limit, second_route.time_limit) + std::chrono::seconds{360});
    auto phase = [](int value) { return C::business("/manual_separation/phase", value); };
    const auto map = C::image("mapFlag"), inn = C::image("Inn");
    const auto after_back = C::any({inn, C::image("dungFlag"), C::image("leaveDung"), C::image("returnText")});
    const auto exited = C::all({C::any({inn, C::image("EdgeOfTown"), C::image("returnText"),
        C::image("returntoTown"), C::image("openworldmap"), C::image("worldmapflag")}),
        C::absent(map), C::absent(J{{"mode", "combat_active"}})});
    const auto return_page = C::any({after_back, exited});
    const auto leap_page = C::any({C::image("cursedWheelTitle"), C::image("cursedWheel"), C::image("ruins")});
    graph.route("Entry", {"UncertainTransfer", "UncertainPayment", "FirstUnit", "SecondUnit"});
    graph.observe("UncertainTransfer", C::business("/manual_separation/transfer_pending", true), {"TransferUnconfirmed"});
    graph.recovery("TransferUnconfirmed", "quest.manual_transfer_unconfirmed");
    graph.observe("UncertainPayment", C::business("/inn_payment_pending", true), {"PaymentUnconfirmed"});
    graph.recovery("PaymentUnconfirmed", "departure.inn_payment_unconfirmed");
    graph.observe("FirstUnit", C::business("/unit_index", 0), {"FirstRoute", "FirstBack", "SecondBack", "RestPhase", "LeapPhase"});
    graph.observe("SecondUnit", C::all({C::business("/unit_index", 1), phase(5)}), {"EnterSecond"});
    graph.observe("FirstRoute", phase(0), {"StartedInCity", "TraverseFirst"});
    graph.confirm("StartedInCity", "manual.city.start", "manual_started_in_city", C::all({inn, C::absent(map)}), {"FirstBack"});
    const auto first = graph.define_child("FirstDungeon", first_route);
    graph.call_child("TraverseFirst", first, {"FirstPoints", "RouteIncomplete"});
    graph.observe("FirstPoints", C::business("/task_step", 1), {"Harken"});
    // 哈肯的子图只有选择、自动移动后才接受离本终点；不能把路线中任意提前回城
    // 当作第二个点完成。单独承接这个离本目标，而不改普通地图点的确认规则。
    const auto harken = graph.define_child("HarkenExit", navigation::reach_map_target(first_plan.route().at(1), {}));
    graph.call_child("Harken", harken, {"HarkenConfirmed"});
    graph.confirm("HarkenConfirmed", "manual.harken", "target_completed", exited, {"FirstConfirmed"}, 1);
    graph.confirm("FirstConfirmed", "manual.first.route", "manual_route_completed", exited, {"FirstBack"});
    graph.recovery("RouteIncomplete", "quest.manual_route_incomplete");
    graph.observe("FirstBack", phase(1), {"PrepareFirstBack"});
    graph.confirm("PrepareFirstBack", "manual.back1.prepare", "manual_first_back_prepared", exited, {"Back1"});
    graph.back("Back1", exited, return_page, {"Back1Confirmed"});
    graph.confirm("Back1Confirmed", "manual.back1.done", "manual_first_back_completed", return_page, {"SecondBack"});
    graph.observe("SecondBack", phase(2), {"PrepareSecondBack"});
    graph.confirm("PrepareSecondBack", "manual.back2.prepare", "manual_second_back_prepared", return_page, {"Back2"});
    graph.back("Back2", return_page, inn, {"Back2Confirmed"});
    graph.confirm("Back2Confirmed", "manual.back2.done", "manual_second_back_completed", inn, {"RestPhase"});
    graph.observe("RestPhase", phase(3), {"Rest"});
    const auto rest = graph.define_child("Inn", supply::rest_at_inn(profile.at("ACTIVE_ROYALSUITE_REST").get<bool>(), true));
    graph.call_child("Rest", rest, {"Rested"});
    graph.confirm("Rested", "manual.rest", "manual_rest_completed", inn, {"LeapPhase"});
    graph.observe("LeapPhase", phase(4), {"PrepareLeap"});
    graph.confirm("PrepareLeap", "manual.leap.prepare", "manual_leap_prepared", leap_page, {"Leap"});
    const auto leap = graph.define_child("TimeLeap", navigation::time_leap_without_causality("BeautifulOre", "cursedwheel_dhi", allow_download));
    graph.call_child("Leap", leap, {"Leaped"});
    graph.confirm("Leaped", "manual.leap.done", "manual_leap_completed", C::any({inn, C::image("EdgeOfTown"), C::image("COS/COS")}), {"Terminal"});
    const auto entry = graph.define_child("SecondEntry", navigation::enter_dungeon(second_plan));
    graph.call_child("EnterSecond", entry, {"TraverseSecond"});
    const auto second = graph.define_child("SecondDungeon", second_route);
    graph.call_child("TraverseSecond", second, {"SecondPoints", "RouteIncomplete"});
    graph.observe("SecondPoints", C::business("/task_step", 2), {"SecondConfirmed"});
    graph.confirm("SecondConfirmed", "manual.second.route", "manual_route_completed", map, {"Terminal"});
    graph.interrupt_on({{"mode", "blocking_screen"}, {"parallel_basic", true}}, "quest.manual_common_screen_requires_dispatch");
    return graph.finish();
}
}
