#include "mining.hpp"
#include "games/wvd/navigation/auto_route.hpp"
#include "games/wvd/navigation/dungeon_entry.hpp"
#include "games/wvd/navigation/world_travel.hpp"
#include "games/wvd/recovery/boot.hpp"
#include "games/wvd/supply/party.hpp"
#include "games/wvd/supply/inn.hpp"

namespace wvd::games::tasks {
CompiledWorkflow mining_iteration(const WvdQuestDefinition &definition, const nlohmann::json &profile,
                                  bool allow_download) {
    if (definition.id != "FFXI-Org" || definition.type != "quest") throw std::runtime_error("MINING_TASK_INVALID");
    using C = PipelineCompiler;
    using J = nlohmann::json;
    const auto plan = WvdTaskPlan::parse(definition);
    if (!plan.return_destination()) throw std::runtime_error("MINING_RETURN_REQUIRED");
    C graph("tasks.FFXI-Org", std::chrono::seconds{900});
    auto scoped = [](const char *name, J roi) { auto p = C::image(name); p["roi"] = std::move(roi); return p; };
    const auto position = scoped("FFXI/org_position", {692, 68, 140, 140});
    const auto receive = scoped("FFXI/receive", {4, 664, 890, 283});
    const auto no_pick = scoped("FFXI/needpickaxe", {4, 664, 890, 283});
    const auto exhausted = C::any({scoped("FFXI/nothingToDig", {320, 667, 423, 474}),
                                   scoped("FFXI/nothingToDig2", {320, 667, 423, 474})});
    const auto dungeon = C::image("dungFlag"), inn = C::image("Inn"), world = C::image("openworldmap");
    const auto return_text = C::image("returnText"), leave = C::image("leaveDung");
    const J blocked{{"mode", "blocking_screen"}};
    const auto site = C::all({dungeon, position, C::absent(C::image("mapFlag")), C::absent(J{{"mode", "combat_active"}})});
    const auto quiet_site = C::all({site, C::absent(C::any({receive, no_pick, exhausted, blocked}))});
    const auto mine_page = C::any({site, receive, no_pick, exhausted});
    const auto exiting = C::any({world, return_text, leave, dungeon, no_pick, exhausted});
    graph.route("Entry", {"PaymentPending", "Blocked", "RefillPending", "Mine", "Enter"});
    graph.observe("PaymentPending", C::business("/inn_payment_pending", true), {"PaymentUncertain"});
    graph.recovery("PaymentUncertain", "departure.inn_payment_unconfirmed");
    graph.observe("Blocked", blocked, {"ClearBlocking"});
    const auto common = graph.define_child("Common", recovery::clear_common_screens(allow_download));
    graph.call_child("ClearBlocking", common, {"Entry"});
    graph.observe("RefillPending", C::business("/mining/refill_pending", true), {"Exit"});
    const auto entry = graph.define_child("DungeonEntry", navigation::enter_dungeon(plan));
    graph.call_child("Enter", entry, {"Seek"});
    const auto seek = graph.define_child("TravelToMark", navigation::auto_route("mark_auto"),
        {"StoppedExit", "BlockedExit", "EncounterExit"});
    graph.call_child("Seek", seek, {"Blocked", "Mine", "PositionMissing"});
    graph.recovery("PositionMissing", "quest.mining_position_not_confirmed");
    graph.observe("Mine", C::all({mine_page, C::absent(blocked)}), {"Dispatch"});
    graph.route("Dispatch", {"Blocked", "Reward", "NeedPickaxes", "NoOre", "Dismissed", "Unknown"});
    graph.confirm("Reward", "mining.reward", "mining_reward_observed", {{"mode", "mining_reward"}}, {"DismissReward"});
    // 奖励页本身证明已获得物品；必须等它消失才允许下一次计数，同页重复观察幂等。
    graph.fixed_click("DismissReward", C::all({receive, C::absent(blocked)}),
        C::any({quiet_site, no_pick, exhausted}), {450, 600}, {"RewardClosed"});
    graph.delay_after("DismissReward", 500);
    graph.postcondition_budget("DismissReward", 10000);
    graph.confirm("RewardClosed", "mining.reward.close", "mining_reward_dismissed", C::all({mine_page, C::absent(receive)}), {"Dispatch"});
    graph.confirm("NeedPickaxes", "mining.refill", "mining_refill_requested", C::all({no_pick, C::absent(blocked)}), {"Exit"});
    graph.observe("NoOre", C::all({exhausted, C::absent(blocked)}), {"Exit"});
    graph.confirm("Dismissed", "mining.reward.close", "mining_reward_dismissed", quiet_site, {"Dig"});
    graph.fixed_click("Dig", quiet_site, C::any({mine_page, blocked}), {450, 600}, {"Dispatch"});
    graph.delay_after("Dig", 500);
    graph.observe("Unknown", {{"mode", "unknown_frozen"}, {"extra_known", {position, receive, no_pick, exhausted}}}, {"Frozen"});
    graph.recovery("Frozen", "dungeon.unknown_static_window");

    graph.route("Exit", {"Blocked", "PaidInn", "AtInn", "AtWorld", "ReturnText", "Leave", "ClosePrompt", "ExitSite"});
    graph.observe("PaidInn", C::all({C::business("/inn_rest_completed", true), C::business("/mining/refill_pending", true),
        C::image("Stay"), C::absent(C::image("OK"))}), {"Rest"});
    graph.observe("AtWorld", world, {"Refill", "Finished"});
    graph.observe("AtInn", C::all({inn, C::absent(dungeon)}), {"Refill", "Finished"});
    graph.click("ReturnText", C::all({return_text, C::absent(world), C::absent(blocked)}), return_text,
        C::any({world, inn}), {"Exit"});
    graph.click("Leave", C::all({leave, C::absent(return_text), C::absent(blocked)}), leave, exiting, {"Exit"});
    graph.fixed_click("ClosePrompt", C::all({C::any({no_pick, exhausted}), C::absent(blocked)}), exiting, {1, 1}, {"Exit"});
    graph.fixed_click("ExitSite", C::all({dungeon, C::absent(blocked)}), exiting, {1, 1}, {"Exit"});
    graph.observe("Refill", C::business("/mining/refill_pending", true), {"ReturnToCity"});
    const auto travel = graph.define_child("WorldReturn", navigation::travel_world(*plan.return_destination(), navigation::WorldArrival::City));
    graph.call_child("ReturnToCity", travel, {"PartyReady", "Assemble"});
    graph.observe("PartyReady", C::business("/mining/party_ready", true), {"Rest"});
    const auto party = graph.define_child("Party", supply::assemble_party(std::string("FFXI/FFXIStone")));
    graph.call_child("Assemble", party, {"Assembled"});
    graph.confirm("Assembled", "mining.party", "mining_party_assembled", inn, {"Rest"});
    const auto rest = graph.define_child("InnRest", supply::rest_at_inn(profile.at("ACTIVE_ROYALSUITE_REST").get<bool>(), true));
    graph.call_child("Rest", rest, {"Rested"});
    graph.confirm("Rested", "mining.rest", "mining_refill_completed", inn, {"Finished"});
    graph.confirm("Finished", "mining.complete", "mining_cycle_completed", C::any({world, inn}), {"Terminal"});
    for (const auto *name : {"Entry", "Blocked", "ClearBlocking", "Mine", "Dispatch", "Reward", "DismissReward",
        "RewardClosed", "Dismissed", "Dig", "Exit", "ReturnText", "Leave", "ClosePrompt", "ExitSite"})
        graph.hit_limit(name, 128);
    return graph.finish();
}
}
