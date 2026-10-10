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
CompiledWorkflow return_to_bounty_city(bool guild, bool fortress = false) {
    C graph(guild ? "quest.bounty.return_guild" : "quest.bounty.return_fortress", std::chrono::seconds{240});
    const auto target = guild ? vision::guild_button() : vision::inn_button();
    const auto map = C::image("mapFlag");
    const auto encounter = C::any({J{{"mode", "combat_active"}}, C::image("chestFlag"), C::image("RiseAgain")});
    const auto harken = C::any({vision::harken_buff_menu(), vision::harken_floor_menu(),
                                vision::outskirts_return_button()});
    J known{target, map, C::image("dungFlag"), vision::city_screen(), harken};
    const std::vector<std::string> exits{"returntotown", "returnText", "leaveDung", "blessing"};
    const auto exit_probe = [](const std::string &name) {
        auto probe = C::image(name);
        if (name == "blessing") probe["locale_only"] = "en";
        return probe;
    };
    for (const auto &name : exits) known.push_back(exit_probe(name));
    const auto city_identity = fortress || !guild ? vision::fortress_city() : vision::royal_city();
    const auto destination = C::all({target, city_identity});
    const auto done = C::all({destination, C::absent(map), C::absent(encounter)});
    graph.route("Entry", guild ? J{"Done", "WrongCity", "Harken", "Dungeon", "Back"}
                               : J{"Done", "Harken", "Dungeon", "Exit0", "Exit1", "Exit2", "Exit3", "Back"});
    graph.observe("Done", done, {"Terminal"});
    if (guild) {
        graph.observe("WrongCity", C::all({vision::city_screen(), C::absent(city_identity)}), {"WrongCityExit"});
        graph.recovery("WrongCityExit", "quest.bounty_return_wrong_city");
    }
    const auto harken_exit = graph.define_child("HarkenExit", navigation::leave_harken());
    graph.observe("Harken", harken, {"LeaveHarken"});
    graph.call_child("LeaveHarken", harken_exit, {"Entry"});
    const auto return_harken = graph.define_child("ReturnHarken", navigation::auto_route("dungFlag"));
    graph.observe("Dungeon", C::all({C::image("dungFlag"), C::absent(map), C::absent(encounter)}), {"GoHarken"});
    // 返哈肯途中遇怪/宝箱或导航暂时停止是业务插入，交还调用者处理后再续返城。
    graph.call_child("GoHarken", return_harken, {"Entry"}, {{"blocked", {"Entry"}}, {"encounter", {"EncounterExit"}}, {"stopped", {"StoppedExit"}}});
    graph.handoff("EncounterExit", "encounter");
    graph.handoff("StoppedExit", "stopped");
    if (!guild)
        for (std::size_t i = 0; i < exits.size(); ++i) {
            const auto image = exit_probe(exits[i]);
            const auto name = "Exit" + std::to_string(i);
            graph.click(name, C::all({image, C::absent(target), C::absent(encounter)}), image, C::any(known), {"Entry"});
            graph.delay_after(name, 2000);
            graph.hit_limit(name, 16);
        }
    graph.back("Back", C::all({guild ? C::any(known) : map, C::absent(target), C::absent(encounter)}), C::any(known), {"Entry"});
    graph.hit_limit("Back", 16);
    return graph.finish();
}
}
WvdTaskPlan scorpion_plan(const WvdQuestDefinition &definition, bool hands_route,
                         const std::string &locale) {
    if (definition.type != "quest" || (definition.id != "Scorpionesses" && definition.id != "Scorpionesses_plus_6_hands") ||
        (hands_route && definition.id != "Scorpionesses_plus_6_hands")) throw std::runtime_error("SCORPION_TASK_INVALID");
    const bool zh_hant = locale == "zh-Hant";
    const auto abyss = zh_hant ? "outskirts_abyss_zh_hant" : "beginningAbyss";
    const auto floor = hands_route ?
        (zh_hant ? "outskirts_abyss_b5f_zh_hant" : "B5FWarpedOnesNest") :
        (zh_hant ? "outskirts_abyss_b2f_zh_hant" : "B2FTemple");
    auto plan = WvdTaskPlan::parse(definition).with_entry({{"press", abyss, {"EdgeOfTown", {1, 1}}, 1},
        {"press", floor, {{1, 1}}, 1}})
        .with_route(hands_route ? J{{"position", "左上", {454, 662}}, {"position", "左上", {135, 714}}}
            : J{{"position", "左下", {505, 760}}, {"dungFlag"}});
    if (zh_hant && !hands_route)
        plan = plan.with_floor("map_abyss_b2f_zh_hant");
    return hands_route ? plan : plan.with_last_harken_arrival();
}
WvdTaskPlan jier_plan(const WvdQuestDefinition &definition, const std::string &locale) {
    if (definition.type != "quest" || definition.id != "jier") throw std::runtime_error("JIER_TASK_INVALID");
    auto points = jier_positions;
    points.push_back({"harken", "左上", nullptr});
    const bool zh_hant = locale == "zh-Hant";
    return WvdTaskPlan::parse(definition).with_entry({{"press", zh_hant ? "outskirts_abyss_zh_hant" : "beginningAbyss", {"EdgeOfTown", {1, 1}}, 1},
        {"press", zh_hant ? "outskirts_abyss_b4f_zh_hant" : "B4FLabyrinth", {{1, 1}}, 1}}).with_route(points);
}
WvdTaskPlan giant_bounty_plan(const WvdQuestDefinition &definition) {
    if (definition.type != "quest" || definition.id != "GiantBounty")
        throw std::runtime_error("GIANT_BOUNTY_TASK_INVALID");
    auto plan = WvdTaskPlan::parse(definition);
    if (!plan.time_leap() || plan.entry_steps().size() != 2 || plan.route().size() != 2 ||
        plan.route().back().target != "dungFlag")
        throw std::runtime_error("GIANT_BOUNTY_PLAN_INCOMPLETE");
    return plan.with_shortcut_battle(3000).with_last_harken_arrival();
}
TaskTimeLeap bounty_time_leap(const WvdTaskPlan &plan, const J &profile) {
    // 任务内的显式跳轮步骤优先；不会修改外部默认配置或额外执行一次默认跳轮。
    if (plan.time_leap()) return *plan.time_leap();
    const bool jier = plan.definition().id == "jier";
    const bool ore = !jier && profile.at("ACTIVE_BEAUTIFUL_ORE").get<bool>();
    return {jier ? "requestToRescueTheDuke" : ore ? "BeautifulOre" :
        profile.at("ACTIVE_TRIUMPH").get<bool>() ? "Triumph" : "GhostsOfYore",
        ore ? "cursedwheel_dhi" : "cursedwheel_impregnableFortress"};
}
CompiledWorkflow bounty_cycle(const WvdQuestDefinition &definition, const J &profile,
    const std::set<std::string> &images, const PublicFlowLibrary &library,
    const J &board_root, const std::string &locale, bool allow_download) {
    const bool jier = definition.id == "jier";
    const bool giant = definition.id == "GiantBounty";
    const auto first_plan = giant ? giant_bounty_plan(definition) :
        jier ? jier_plan(definition, locale) : scorpion_plan(definition, false, locale);
    const auto dialogue = jier ? recovery::DialoguePolicy::Jier : recovery::DialoguePolicy::Default;
    const bool hands = definition.id == "Scorpionesses_plus_6_hands";
    const auto leap_step = bounty_time_leap(first_plan, profile);
    const bool skip_travel = giant || leap_step.chapter == "cursedwheel_dhi";
    if (profile.at("REST_INTERVEL").get<std::int64_t>() < 0) throw std::runtime_error("BOUNTY_REST_INTERVAL_INVALID");
    // 普通蝎女路线的第二步是快捷返哈肯，不再在战后重新选地图坐标。
    const auto first_route = traverse_dungeon(jier ? first_plan.with_route(jier_positions) : first_plan, profile, images, allow_download, dialogue);
    C graph("tasks." + definition.id, first_route.time_limit + std::chrono::seconds{360});
    const auto first_dungeon = graph.define_child("FirstDungeon", first_route);
    const auto inn = vision::inn_button(), guild = vision::guild_button(),
               edge = vision::edge_of_town_button(), map = C::image("mapFlag");
    const auto bounty_city = giant ? vision::fortress_city() : vision::royal_city();
    const auto leap_page = C::any({C::image("cursedWheelTitle"), C::image("cursedWheelTitle_zh_hant"),
        C::image("cursedWheel"), C::image("cursedWheel_zh_hant"), C::image("ruins"), vision::ruins_button()});
    // 与旧 CursedWheelTimeLeap 的调用入口一致：先从已确认城市进入因果轮。
    // 这里仅标记准备阶段，绝不把城市画面当作“已跳跃”。
    const auto start_page = C::all({giant ? C::any({leap_page, bounty_city}) :
        C::any({leap_page, bounty_city, inn, guild, edge, C::image("openworldmap")}),
        C::absent(J{{"mode", "blocking_screen"}}), C::absent(J{{"mode", "combat_active"}}),
        C::absent(C::image("chestFlag"))});
    const auto outside = C::any({bounty_city, inn, edge, C::image("dungFlag"), C::image("returnText"),
        C::image("returntotown"), C::image("openworldmap")});
    graph.route("Entry", {"PendingTransfer", "PendingPayment", "PendingReport", "Resume", "InspectBoard"});
    graph.observe("PendingTransfer", C::business("/bounty_cycle/transfer_pending", true), {"TransferUncertain"});
    graph.recovery("TransferUncertain", "quest.bounty_transfer_unconfirmed");
    graph.observe("PendingPayment", C::business("/inn_payment_pending", true), {"PaymentUncertain"});
    graph.observe("PendingReport", C::business("/bounty_report_pending", true), {"ReportUncertain"});
    graph.recovery("ReportUncertain", "quest.bounty_report_unconfirmed");
    graph.observe("Resume", C::business("/bounty_cycle/active", true), {"Stage"});
    // 先收掉上一轮尚未提交的悬赏。仅报告按钮实际可见时才提交、住宿；
    // 无报告则退出公会直接开始本轮，不能把查看列表算作领取悬赏。
    const auto board_page = locale == "zh-Hant" ?
        library.resource_condition("guild.bounties.page", locale, authoring::ResourceUse::Observation) :
        C::image("Bounties");
    const auto ready_report = library.resource_condition("guild.report.action", locale,
        authoring::ResourceUse::Position);
    const auto board_open = library.compile(board_root,
        [](const J &) -> CompiledWorkflow { throw std::runtime_error("BOUNTY_PUBLIC_NATIVE_BINDING_FORBIDDEN"); },
        J{{"location", 0}}, locale);
    const auto inspect = graph.define_child("InspectBountyBoard", board_open.workflow);
    // 新一轮需要先查看旧报告。断点可能留在荒屋，不能直接调用只接受城市/公会
    // 的公共开页流程然后等待到超时；只在确证该菜单时退出，不改变跳轮账目。
    auto ruins_menu = C::image("cursedWheel_zh_hant");
    ruins_menu["roi"] = {450, 500, 450, 400};
    const auto ruins_title = C::any({C::image("cursedWheelTitle"), C::image("cursedWheelTitle_zh_hant")});
    const auto ruins_ready = C::all({ruins_menu, C::absent(ruins_title),
        J{{"mode", "input_clear"}, {"phase", "navigation"}}});
    J inspect_ready_pages = {vision::city_screen(), board_page,
        library.resource_condition("guild.menu", locale, authoring::ResourceUse::Observation)};
    if (locale == "zh-Hant")
        inspect_ready_pages.push_back(library.resource_condition("guild.commissions.page", locale,
            authoring::ResourceUse::Observation));
    const auto inspect_ready = C::any(inspect_ready_pages);
    // 服务重启不保留上一轮内存账目，但游戏可能仍在战斗/迷宫。
    // 先结束现场遭遇并返城，再查看可提交报告；不能凭此补记上一轮成功或直接跳轮。
    const J current_combat{{"mode", "combat_active"}};
    graph.route("InspectBoard", locale == "zh-Hant" ? J{"InspectCombat", "InspectChest", "InspectRevive", "InspectDungeon", "CloseLateOldReveal", "InspectReady", "InspectOutside", "LeaveRuinsForInspection"}
        : J{"InspectCombat", "InspectChest", "InspectRevive", "InspectDungeon", "InspectReady", "InspectOutside", "LeaveRuinsForInspection"});
    // 复用正式副本中已定义的战斗子流程及其Return出口，不复制整张角色/技能图。
    const auto unfinished_battle = "FirstDungeon_" +
        first_route.nodes.at("Fight").at("operation_args").at("entry").get<std::string>();
    graph.route("RecoverReturn", {"InspectCombat", "InspectChest", "InspectRevive", "ResumeReturn"});
    graph.route("ResumeReturn", {"Entry"});
    graph.observe("InspectCombat", current_combat, {"FinishBattleForInspection"});
    graph.call_child("FinishBattleForInspection", unfinished_battle, {"Entry"},
        {{"blocked", {"RecoverReturn"}}, {"chest", {"RecoverReturn"}}, {"revive", {"RecoverReturn"}}});
    graph.observe("InspectChest", first_route.nodes.at("Chest").at("observation_args"), {"FinishChestForInspection"});
    graph.call_child("FinishChestForInspection", "FirstDungeon_" +
        first_route.nodes.at("OpenChest").at("operation_args").at("entry").get<std::string>(), {"Entry"},
        {{"combat", {"RecoverReturn"}}, {"revive", {"RecoverReturn"}}, {"ambush", {"RecoverReturn"}},
         {"blocked", {"RecoverReturn"}}, {"retry", {"RecoverReturn"}}});
    graph.observe("InspectRevive", first_route.nodes.at("Revive").at("observation_args"), {"ReviveForInspection"});
    graph.call_child("ReviveForInspection", "FirstDungeon_" +
        first_route.nodes.at("Resurrect").at("operation_args").at("entry").get<std::string>(), {"Entry"});
    graph.observe("InspectDungeon", C::all({C::image("dungFlag"), C::absent(current_combat),
        C::absent(C::image("chestFlag")), C::absent(C::image("RiseAgain"))}), {"ReturnForInspection"});
    const auto inspect_return = graph.define_child("InspectReturnCity", return_to_bounty_city(true, giant));
    graph.observe("InspectOutside", vision::outskirts_return_button(), {"ReturnForInspection"});
    graph.call_child("ReturnForInspection", inspect_return, {"InspectBoard"},
        {{"encounter", {"RecoverReturn"}}, {"stopped", {"RecoverReturn"}}});
    if (locale == "zh-Hant") {
        const auto reveal = library.resource_condition("guild.bounty.reveal.close", locale, authoring::ResourceUse::Position);
        // 跳轮后的联网展示卡可以晚于开页回执到达；关卡后仍重新进入悬赏页查报告。
        // 只确认关闭展示，不把它当作领取/提交，也不因此直接开始下一次跳轮。
        // “關閉”不是公会独有控件，技能详情也有；战斗中不得借公会步骤关闭它。
        graph.click("CloseLateOldReveal", C::all({reveal, C::absent(current_combat)}), reveal,
            C::any({board_page, reveal, library.resource_condition("guild.menu", locale, authoring::ResourceUse::Observation)}),
            {"InspectBoard"});
        graph.delay_after("CloseLateOldReveal", 700);
    }
    graph.observe("InspectReady", inspect_ready, {"OpenInspectBoard"});
    // 实机900x1600的“離開”是轮盘按钮下方约240像素，仍由菜单匹配给出横坐标。
    graph.click("LeaveRuinsForInspection", ruins_ready, ruins_menu,
        C::all({vision::city_screen(), C::absent(ruins_menu)}), {"InspectBoard"}, {0, 240});
    graph.retry_menu_input("LeaveRuinsForInspection", vision::menu_retry_ready(ruins_ready));
    graph.call_child("OpenInspectBoard", inspect, {"CheckOldReports"});
    graph.route("CheckOldReports", locale == "zh-Hant" ? J{"CloseLateOldReveal", "OldReportReady", "EmptyOldReportCandidate"}
        : J{"OldReportReady", "EmptyOldReportCandidate"});
    graph.observe("OldReportReady", C::all({board_page, ready_report}), {"CollectOldReports"});
    const auto old_report = graph.define_child("OldBountyReport",
        visit_bounty_board(BountyVisit::Report, library, board_root, locale));
    graph.call_child("CollectOldReports", old_report, {"RestAfterOldReports"});
    const auto old_rest = graph.define_child("OldBountyRest",
        supply::rest_at_inn(profile.at("ACTIVE_ROYALSUITE_REST").get<bool>(), true));
    graph.call_child("RestAfterOldReports", old_rest, {"Start"});
    graph.call_child("PaymentUncertain", old_rest, {"Resume", "InspectBoard"});
    const auto no_old_report = C::all({board_page, C::absent(ready_report)});
    graph.observe("EmptyOldReportCandidate", no_old_report, {"RecheckOldReports"});
    graph.wait("RecheckOldReports", 1000, locale == "zh-Hant" ? J{"CloseLateOldReveal", "OldReportReady", "NoOldReport"}
        : J{"OldReportReady", "NoOldReport"});
    graph.observe("NoOldReport", no_old_report, {"LeaveBoard"});
    const auto leave_board = graph.define_child("LeaveInspectedBoard", leave_bounty_board(library, locale));
    graph.call_child("LeaveBoard", leave_board, {"Start"});
    graph.confirm("Start", "bounty.cycle.start", giant ? "giant_bounty_started" : jier ? "jier_started" : hands ? "scorpion_hands_started" : "scorpion_started", start_page, {"Stage"});
    graph.route("Stage", hands ? J{"LeapPhase", "TravelPhase", "RevealPhase", "FirstRoutePhase", "FirstReturnPhase",
            "SecondRoutePhase", "SecondReturnPhase", "ReportsPhase", "RestPhase"} :
            J{"LeapPhase", "TravelPhase", "RevealPhase", "FirstRoutePhase", "FirstReturnPhase", "ReportsPhase", "RestPhase"});
    graph.observe("LeapPhase", phase(Phase::Leap), {"PrepareLeap"});
    graph.confirm("PrepareLeap", "bounty.leap.prepare", "bounty_leap_prepared", start_page, {"Leap"});
    // 这两个原case没有传CSC_symbol，即便ACTIVE_CSC开启也不修改因果；并非忽略配置。
    const auto leap = graph.define_child("TimeLeap", navigation::time_leap_without_causality(
        leap_step.target, leap_step.chapter, allow_download));
    graph.call_child("Leap", leap, {"Leaped"});
    // 子流程和本节点均已确认跳轮后的场景，不再额外盲等；后续输入仍重新认页。
    graph.confirm("Leaped", "bounty.leap.done", "bounty_leap_completed", outside, {"TravelPhase"});
    graph.observe("TravelPhase", phase(Phase::Travel), {skip_travel ? "Travelled" : "PrepareTravel"});
    if (!skip_travel) {
        graph.confirm("PrepareTravel", "bounty.travel.prepare", "bounty_travel_prepared", outside, {"ReturnFortress"});
        const auto fortress = graph.define_child("Fortress", return_to_bounty_city(false));
        graph.call_child("ReturnFortress", fortress, {"GoRoyalCity"},
            {{"encounter", {"RecoverReturn"}}, {"stopped", {"RecoverReturn"}}});
        const auto travel = graph.define_child("RoyalCity", navigation::travel_city_to_city(
            {"City_RoyalCityLuknalia", TaskSwipe{{450, 150}, {500, 150}}, {550, 1}}));
        graph.call_child("GoRoyalCity", travel, {"Travelled"});
    }
    graph.confirm("Travelled", "bounty.travel.done", skip_travel ? "bounty_travel_skipped" : "bounty_travel_completed",
                  !giant && skip_travel ? C::any({bounty_city, inn, guild, edge}) : bounty_city, {"RevealPhase"});
    graph.observe("RevealPhase", phase(Phase::Reveal), {"Reveal"});
    const auto board = graph.define_child("BountyBoard",
        visit_bounty_board(BountyVisit::Reveal, library, board_root, locale));
    graph.call_child("Reveal", board, {"Revealed"});
    graph.confirm("Revealed", "bounty.cycle.reveal", "bounty_cycle_revealed", giant ? C::all({edge, bounty_city}) : edge, {"Terminal"});
    const auto return_guild = graph.define_child("ReturnGuild", return_to_bounty_city(true, giant));
    for (const bool second : {false, true}) {
        const std::string name = second ? "Second" : "First";
        if (second && !hands) continue;
        const auto plan = second ? scorpion_plan(definition, true, locale) : first_plan;
        graph.observe(name + "RoutePhase", phase(second ? Phase::SecondRoute : Phase::FirstRoute), {name + "Enter"});
        const auto entry = graph.define_child(name + "Entry", navigation::enter_dungeon(plan, !jier));
        graph.call_child(name + "Enter", entry, {name + "Traverse"});
        const auto route = second ? graph.define_child(name + "Dungeon", traverse_dungeon(plan, profile, images, allow_download)) : first_dungeon;
        graph.call_child(name + "Traverse", route, {name + "Points", "Incomplete"});
        graph.observe(name + "Points", C::business("/task_step", 2), {jier ? "LeaveByHarken" : name + "RouteDone"});
        if (jier) {
            const auto exit = graph.define_child("HarkenExit", navigation::reach_map_target(first_plan.route().back()));
            graph.call_child("LeaveByHarken", exit, {name + "RouteDone"}, {{"encounter", {"RecoveryRequired"}}, {"blocked", {"RecoveryRequired"}}});
        }
        graph.confirm(name + "RouteDone", "bounty.route." + name, "bounty_route_completed",
            jier ? C::all({C::any({inn, guild, edge, C::image("returnText"), C::image("openworldmap")}), C::absent(map)})
                 : plan.route().back().harken_arrival ? C::any({map, vision::harken_floor_menu()}) : map,
            {name + "ReturnPhase"});
        graph.observe(name + "ReturnPhase", phase(second ? Phase::SecondReturn : Phase::FirstReturn), {name + "Return"});
        graph.call_child(name + "Return", return_guild, {name + "Returned"},
            {{"encounter", {"RecoverReturn"}}, {"stopped", {"RecoverReturn"}}});
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
    // Rest子流程已按返回并退出菜单；不重复等待离店后的可选信息或城市帧。
    const auto completed = C::any({C::all({C::business("/bounty_cycle/rest_due", true),
        C::business("/inn_rest_completed", true), C::business("/inn_payment_pending", false)}),
        C::all({C::business("/bounty_cycle/rest_due", false),
            giant ? C::all({C::any({inn, edge}), bounty_city}) : C::any({inn, edge})})});
    graph.confirm("Completed", "bounty.cycle.done", "bounty_cycle_completed", completed, {"Terminal"});
    graph.interrupt_on({{"mode", "blocking_screen"}, {"parallel_basic", true}}, "quest.bounty_common_screen_requires_dispatch");
    auto result = graph.finish();
    result.random_maze_events = false;
    result.refresh_images();
    return result;
}
}
