#include "bounty_cycle.hpp"
#include "bounty_visit.hpp"
#include "games/wvd/navigation/dungeon_entry.hpp"
#include "games/wvd/navigation/auto_route.hpp"
#include "games/wvd/navigation/harken_exit.hpp"
#include "games/wvd/navigation/map_route.hpp"
#include "games/wvd/navigation/time_leap.hpp"
#include "games/wvd/navigation/world_travel.hpp"
#include "games/wvd/quests/bounty_cycle.hpp"
#include "games/wvd/supply/inn.hpp"
#include "games/wvd/vision/location_probes.hpp"
#include "games/wvd/vision/harken_probes.hpp"

namespace wvd::games::tasks {
namespace {
using C = PipelineCompiler;
using J = nlohmann::json;
using Phase = quests::BountyCycle::Phase;
const J jier_positions{{"position", "左下", {452, 545}}, {"position", "左下", {452, 1026}}};
J phase(Phase value) { return C::all({C::business("/bounty_cycle/phase", static_cast<int>(value)), C::business("/bounty_cycle/unit_matches", true)}); }
CompiledWorkflow return_to_bounty_city(bool guild) {
    C graph(guild ? "quest.bounty.return_guild" : "quest.bounty.return_fortress", std::chrono::seconds{240});
    const auto target = guild ? vision::guild_button() : vision::inn_button();
    const auto map = C::image("mapFlag");
    const auto encounter = C::any({J{{"mode", "combat_active"}}, C::image("chestFlag"), C::image("RiseAgain")});
    const auto harken = C::any({vision::harken_buff_menu(), vision::harken_floor_menu(),
                                vision::outskirts_return_button()});
    J known{target, map, C::image("dungFlag"), vision::city_screen(), harken};
    const std::vector<std::string> exits{"returntotown", "returnText", "leaveDung", "blessing"};
    for (const auto &name : exits) known.push_back(C::image(name));
    const auto destination = guild ? C::all({target, vision::royal_city()}) : target;
    const auto done = C::all({destination, C::absent(map), C::absent(encounter)});
    graph.route("Entry", guild ? J{"Done", "WrongCity", "Harken", "Dungeon", "Back"}
                               : J{"Done", "Harken", "Dungeon", "Exit0", "Exit1", "Exit2", "Exit3", "Back"});
    graph.observe("Done", done, {"Terminal"});
    if (guild) {
        graph.observe("WrongCity", C::all({vision::city_screen(), C::absent(vision::royal_city())}), {"WrongCityExit"});
        graph.recovery("WrongCityExit", "quest.bounty_return_wrong_city");
    }
    const auto harken_exit = graph.define_child("HarkenExit", navigation::leave_harken());
    graph.observe("Harken", harken, {"LeaveHarken"});
    graph.call_child("LeaveHarken", harken_exit, {"Entry"});
    const auto return_harken = graph.define_child("ReturnHarken", navigation::auto_route("dungFlag"), {"BlockedExit"});
    graph.observe("Dungeon", C::all({C::image("dungFlag"), C::absent(map), C::absent(encounter)}), {"GoHarken"});
    graph.call_child("GoHarken", return_harken, {"Entry"});
    if (!guild)
        for (std::size_t i = 0; i < exits.size(); ++i) {
            const auto image = C::image(exits[i]);
            const auto name = "Exit" + std::to_string(i);
            graph.click(name, C::all({image, C::absent(target), C::absent(encounter)}), image, C::any(known), {"Entry"});
            graph.delay_after(name, 2000);
            graph.hit_limit(name, 16);
        }
    graph.back("Back", C::all({guild ? C::any(known) : map, C::absent(target), C::absent(encounter)}), C::any(known), {"Entry"});
    graph.hit_limit("Entry", 32);
    graph.hit_limit("Back", 16);
    graph.hit_limit("Harken", 16);
    graph.hit_limit("Dungeon", 16);
    return graph.finish();
}
}
WvdTaskPlan scorpion_plan(const WvdQuestDefinition &definition, bool hands_route) {
    if (definition.type != "quest" || (definition.id != "Scorpionesses" && definition.id != "Scorpionesses_plus_6_hands") ||
        (hands_route && definition.id != "Scorpionesses_plus_6_hands")) throw std::runtime_error("SCORPION_TASK_INVALID");
    return WvdTaskPlan::parse(definition).with_entry({{"press", "beginningAbyss", {"EdgeOfTown", {1, 1}}, 1},
        {"press", hands_route ? "B5FWarpedOnesNest" : "B2FTemple", {{1, 1}}, 1}})
        .with_route(hands_route ? J{{"position", "左上", {454, 662}}, {"position", "左上", {135, 714}}}
            : J{{"position", "左下", {505, 760}}, {"position", "左上", {506, 821}}});
}
WvdTaskPlan jier_plan(const WvdQuestDefinition &definition) {
    if (definition.type != "quest" || definition.id != "jier") throw std::runtime_error("JIER_TASK_INVALID");
    auto points = jier_positions;
    points.push_back({"harken", "左上", nullptr});
    return WvdTaskPlan::parse(definition).with_entry({{"press", "beginningAbyss", {"EdgeOfTown", {1, 1}}, 1},
        {"press", "B4FLabyrinth", {{1, 1}}, 1}}).with_route(points);
}
CompiledWorkflow bounty_cycle(const WvdQuestDefinition &definition, const J &profile,
    const std::set<std::string> &images, const PublicFlowLibrary &library,
    const J &board_root, const std::string &locale, bool allow_download) {
    const bool jier = definition.id == "jier";
    const auto first_plan = jier ? jier_plan(definition) : scorpion_plan(definition);
    const auto dialogue = jier ? recovery::DialoguePolicy::Jier : recovery::DialoguePolicy::Default;
    const bool hands = definition.id == "Scorpionesses_plus_6_hands";
    const bool ore = !jier && profile.at("ACTIVE_BEAUTIFUL_ORE").get<bool>();
    const bool triumph = !jier && profile.at("ACTIVE_TRIUMPH").get<bool>();
    if (profile.at("REST_INTERVEL").get<std::int64_t>() < 0) throw std::runtime_error("BOUNTY_REST_INTERVAL_INVALID");
    // harken是退出动作，不是假装仍在地图上确认的第三个坐标点。两点完成后独立执行它。
    const auto first_route = traverse_dungeon(jier ? first_plan.with_route(jier_positions) : first_plan, profile, images, allow_download, dialogue);
    C graph("tasks." + definition.id, first_route.time_limit + std::chrono::seconds{360});
    const auto inn = vision::inn_button(), guild = vision::guild_button(),
               edge = vision::edge_of_town_button(), map = C::image("mapFlag");
    const auto royal_city = vision::royal_city();
    const auto leap_page = C::any({C::image("cursedWheelTitle"), C::image("cursedWheelTitle_zh_hant"),
        C::image("cursedWheel"), C::image("cursedWheel_zh_hant"), C::image("ruins"), vision::ruins_button()});
    // 与旧 CursedWheelTimeLeap 的调用入口一致：先从已确认城市进入因果轮。
    // 这里仅标记准备阶段，绝不把城市画面当作“已跳跃”。
    const auto start_page = C::all({C::any({leap_page, royal_city, inn, guild, edge, C::image("openworldmap")}),
        C::absent(J{{"mode", "blocking_screen"}}), C::absent(J{{"mode", "combat_active"}}),
        C::absent(C::image("chestFlag"))});
    const auto outside = C::any({royal_city, inn, edge, C::image("dungFlag"), C::image("returnText"),
        C::image("returntotown"), C::image("openworldmap")});
    graph.route("Entry", {"PendingTransfer", "PendingPayment", "PendingReport", "Resume", "Start"});
    graph.observe("PendingTransfer", C::business("/bounty_cycle/transfer_pending", true), {"TransferUncertain"});
    graph.recovery("TransferUncertain", "quest.bounty_transfer_unconfirmed");
    graph.observe("PendingPayment", C::business("/inn_payment_pending", true), {"PaymentUncertain"});
    graph.recovery("PaymentUncertain", "departure.inn_payment_unconfirmed");
    graph.observe("PendingReport", C::business("/bounty_report_pending", true), {"ReportUncertain"});
    graph.recovery("ReportUncertain", "quest.bounty_report_unconfirmed");
    graph.observe("Resume", C::business("/bounty_cycle/active", true), {"Stage"});
    graph.confirm("Start", "bounty.cycle.start", jier ? "jier_started" : hands ? "scorpion_hands_started" : "scorpion_started", start_page, {"Stage"});
    graph.route("Stage", hands ? J{"LeapPhase", "TravelPhase", "RevealPhase", "FirstRoutePhase", "FirstReturnPhase",
        "SecondRoutePhase", "SecondReturnPhase", "ReportsPhase", "RestPhase"} :
        J{"LeapPhase", "TravelPhase", "RevealPhase", "FirstRoutePhase", "FirstReturnPhase", "ReportsPhase", "RestPhase"});
    graph.observe("LeapPhase", phase(Phase::Leap), {"PrepareLeap"});
    graph.confirm("PrepareLeap", "bounty.leap.prepare", "bounty_leap_prepared", start_page, {"Leap"});
    // 这两个原case没有传CSC_symbol，即便ACTIVE_CSC开启也不修改因果；并非忽略配置。
    const auto leap = graph.define_child("TimeLeap", navigation::time_leap_without_causality(
        jier ? "requestToRescueTheDuke" : ore ? "BeautifulOre" : triumph ? "Triumph" : "GhostsOfYore", ore ? "cursedwheel_dhi" : "cursedwheel_impregnableFortress", allow_download));
    graph.call_child("Leap", leap, {"Leaped"});
    graph.confirm("Leaped", "bounty.leap.done", "bounty_leap_completed", outside, {"TravelPhase"});
    graph.delay_after("Leaped", 10000);
    graph.observe("TravelPhase", phase(Phase::Travel), {ore ? "Travelled" : "PrepareTravel"});
    if (!ore) {
        graph.confirm("PrepareTravel", "bounty.travel.prepare", "bounty_travel_prepared", outside, {"ReturnFortress"});
        const auto fortress = graph.define_child("Fortress", return_to_bounty_city(false));
        graph.call_child("ReturnFortress", fortress, {"GoRoyalCity"});
        const auto travel = graph.define_child("RoyalCity", navigation::travel_city_to_city(
            {"City_RoyalCityLuknalia", TaskSwipe{{450, 150}, {500, 150}}, {550, 1}}));
        graph.call_child("GoRoyalCity", travel, {"Travelled"});
    }
    graph.confirm("Travelled", "bounty.travel.done", ore ? "bounty_travel_skipped" : "bounty_travel_completed",
                  ore ? C::any({royal_city, inn, guild, edge}) : royal_city, {"RevealPhase"});
    graph.observe("RevealPhase", phase(Phase::Reveal), {"Reveal"});
    const auto board = graph.define_child("BountyBoard",
        visit_bounty_board(BountyVisit::Reveal, library, board_root, locale));
    graph.call_child("Reveal", board, {"Revealed"});
    graph.confirm("Revealed", "bounty.cycle.reveal", "bounty_cycle_revealed", edge, {"Terminal"});
    const auto return_guild = graph.define_child("ReturnGuild", return_to_bounty_city(true));
    for (const bool second : {false, true}) {
        const std::string name = second ? "Second" : "First";
        if (second && !hands) continue;
        const auto plan = second ? scorpion_plan(definition, true) : first_plan;
        graph.observe(name + "RoutePhase", phase(second ? Phase::SecondRoute : Phase::FirstRoute), {name + "Enter"});
        const auto entry = graph.define_child(name + "Entry", navigation::enter_dungeon(plan));
        graph.call_child(name + "Enter", entry, {name + "Traverse"});
        const auto route = graph.define_child(name + "Dungeon", second ? traverse_dungeon(plan, profile, images, allow_download) : first_route);
        graph.call_child(name + "Traverse", route, {name + "Points", "Incomplete"});
        graph.observe(name + "Points", C::business("/task_step", 2), {jier ? "LeaveByHarken" : name + "RouteDone"});
        if (jier) {
            const auto exit = graph.define_child("HarkenExit", navigation::reach_map_target(first_plan.route().back()));
            graph.call_child("LeaveByHarken", exit, {name + "RouteDone"});
        }
        graph.confirm(name + "RouteDone", "bounty.route." + name, "bounty_route_completed",
            jier ? C::all({C::any({inn, guild, edge, C::image("returnText"), C::image("openworldmap")}), C::absent(map)}) : map, {name + "ReturnPhase"});
        graph.observe(name + "ReturnPhase", phase(second ? Phase::SecondReturn : Phase::FirstReturn), {name + "Return"});
        graph.call_child(name + "Return", return_guild, {name + "Returned"});
        graph.confirm(name + "Returned", "bounty.return." + name, "bounty_return_completed", guild, {"Terminal"});
    }
    graph.recovery("Incomplete", "quest.bounty_route_incomplete");
    graph.observe("ReportsPhase", phase(Phase::Reports), {"Reports"});
    graph.route("Reports", {"AllReported", "Deliver"});
    graph.observe("AllReported", C::business("/bounty_cycle/reports_remaining", 0), {"ReportsDone"});
    const auto report = graph.define_child("BountyReport",
        visit_bounty_board(BountyVisit::Report, library, board_root, locale));
    graph.call_child("Deliver", report, {"Reports"});
    graph.hit_limit("Reports", 3);
    graph.hit_limit("Deliver", hands ? 2 : 1);
    graph.confirm("ReportsDone", "bounty.cycle.reports", "bounty_cycle_reported", edge, {"RestPhase"});
    graph.observe("RestPhase", phase(Phase::Rest), {"RestDue", "NoRest"});
    graph.observe("RestDue", C::business("/bounty_cycle/rest_due", true), {"Rest"});
    graph.observe("NoRest", C::business("/bounty_cycle/rest_due", false), {"Completed"});
    const auto rest = graph.define_child("Inn", supply::rest_at_inn(profile.at("ACTIVE_ROYALSUITE_REST").get<bool>(), true));
    graph.call_child("Rest", rest, {"Completed"});
    graph.confirm("Completed", "bounty.cycle.done", "bounty_cycle_completed", C::any({inn, edge}), {"Terminal"});
    graph.interrupt_on({{"mode", "blocking_screen"}, {"parallel_basic", true}}, "quest.bounty_common_screen_requires_dispatch");
    return graph.finish();
}
}
