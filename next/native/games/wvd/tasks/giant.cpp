#include "giant.hpp"
#include "games/wvd/navigation/dungeon_entry.hpp"
#include "games/wvd/recovery/boot.hpp"
#include "games/wvd/supply/inn.hpp"

namespace wvd::games::tasks {
WvdTaskPlan giant_plan(const WvdQuestDefinition &definition) {
    if (definition.id != "gaintKiller" || definition.type != "quest")
        throw std::runtime_error("GIANT_TASK_INVALID");
    return WvdTaskPlan::parse(definition)
        .with_entry({{"press", "impregnableFortress", {"EdgeOfTown", {1, 1}}, 1},
                     {"press", "fortressb7f", {1, 1}, 1}})
        .with_route({{"position", "左上", {560, 982}}, {"harken2", "左上"}});
}
CompiledWorkflow giant_iteration(const WvdQuestDefinition &definition, const nlohmann::json &profile,
                                 const std::set<std::string> &images, bool allow_download) {
    using C = PipelineCompiler;
    using J = nlohmann::json;
    // 原 case 不受 ACTIVE_REST 控制，间隔为 REST_INTERVEL+1；不套普通副本公式。
    if (!profile.at("REST_INTERVEL").is_number_integer() || profile.at("REST_INTERVEL").get<std::int64_t>() < 0)
        throw std::runtime_error("GIANT_REST_INTERVAL_INVALID");
    const auto plan = giant_plan(definition);
    const auto route = traverse_dungeon(plan, profile, images, allow_download);
    C graph("tasks.gaintKiller", route.time_limit + std::chrono::seconds{360});
    const auto map = C::image("mapFlag"), inn = C::image("Inn");
    const J combat{{"mode", "combat_active"}};
    const auto inside = C::any({map, C::image("dungFlag"), combat, C::image("chestFlag"), C::image("whowillopenit")});
    const auto city = C::all({inn, C::absent(inside), C::absent(C::image("Stay"))});
    graph.route("Entry", {"PaymentPending", "Blocked", "Resume", "Started", "StartedInside",
        "StartedEdge", "StartedFortress", "StartedFloor"});
    graph.observe("PaymentPending", C::business("/inn_payment_pending", true), {"PaymentUncertain"});
    graph.recovery("PaymentUncertain", "departure.inn_payment_unconfirmed");
    graph.observe("Blocked", {{"mode", "blocking_screen"}}, {"ClearBlocking"});
    const auto common = graph.define_child("Common", recovery::clear_common_screens(allow_download));
    graph.call_child("ClearBlocking", common, {"Entry"});
    for (const auto *node : {"Entry", "Blocked", "ClearBlocking"})
        graph.hit_limit(node, 32);
    graph.observe("Resume", C::business("/giant_cycle_active", true), {"ReturnPhase", "RoutePhase"});
    // 先按具体场景选路，再用新帧确认该场景；不在每个确认中重复匹配全部入口。
    // 五个分支的并集与原开始条件相同，且共享同一开始回执和阻塞层反证。
    for (const auto &[name, scene] : std::vector<std::pair<std::string, J>>{
        {"Started", city}, {"StartedInside", inside}, {"StartedEdge", C::image("EdgeOfTown")},
        {"StartedFortress", C::image("impregnableFortress")}, {"StartedFloor", C::image("fortressb7f")}})
        graph.confirm(name, "giant.start", "giant_cycle_started",
            C::all({scene, C::absent(J{{"mode", "blocking_screen"}})}), {"ReturnPhase", "RoutePhase"});
    graph.observe("ReturnPhase", C::business("/giant_route_completed", true), {"Return"});
    graph.observe("RoutePhase", C::business("/giant_route_completed", false), {"Enter"});
    const auto enter = graph.append("DungeonEntry", navigation::enter_dungeon(plan), {"Traverse"});
    graph.route("Enter", {enter});
    const auto dungeon = graph.define_child("Dungeon", route);
    graph.call_child("Traverse", dungeon, {"AllPoints", "PrematureExit"});
    graph.observe("AllPoints", C::business("/task_step", 2), {"RouteConfirmed"});
    graph.recovery("PrematureExit", "quest.giant_route_incomplete");
    graph.confirm("RouteConfirmed", "giant.route", "giant_route_completed", inside, {"Return"});

    // 每次输入后先重查旅店。禁止旧 fallback 整串执行时入城后继续点角色。
    J return_known{city, map};
    for (const auto *name : {"returntotown", "returnText", "leaveDung", "blessing"})
        return_known.push_back(C::image(name));
    graph.route("Return", {"PaidInn", "AtInn", "Exit0", "Exit1", "Exit2", "Exit3", "CloseMap"});
    graph.observe("PaidInn", C::all({C::business("/inn_rest_completed", true), C::image("Stay"),
        C::absent(C::image("OK")), C::absent(inside)}), {"Rest"});
    graph.hit_limit("Return", 32);
    graph.observe("AtInn", city, {"RestDue", "RestNotDue"});
    for (std::size_t i = 0; i < 4; ++i) {
        const auto target = return_known.at(i + 2);
        const auto name = "Exit" + std::to_string(i);
        graph.click(name, C::all({target, C::absent(inn), C::absent(combat)}), target,
            C::any(return_known), {"Return"});
        graph.delay_after(name, 2000);
        graph.hit_limit(name, 8);
    }
    graph.back("CloseMap", C::all({map, C::absent(combat)}), C::any(return_known), {"Return"});
    graph.hit_limit("CloseMap", 1);
    graph.observe("RestDue", C::business("/giant_rest_due", true), {"Rest"});
    graph.observe("RestNotDue", C::business("/giant_rest_due", false), {"Completed"});
    const auto rest = graph.append("Inn", supply::rest_at_inn(profile.at("ACTIVE_ROYALSUITE_REST").get<bool>(), true), {"Completed"});
    graph.route("Rest", {rest});
    graph.confirm("Completed", "giant.complete", "giant_cycle_completed", city, {"Terminal"});
    return graph.finish();
}
}
