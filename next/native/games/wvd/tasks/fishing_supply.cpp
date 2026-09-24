#include "fishing_supply.hpp"
#include "fishing.hpp"
#include "dungeon_route.hpp"
#include "games/wvd/navigation/dungeon_entry.hpp"

namespace wvd::games::tasks {
namespace {
using C = PipelineCompiler;
using J = nlohmann::json;
J phase(unsigned value) { return C::business("/fishing/refill_phase", value); }
CompiledWorkflow transfer_bait() {
    C graph("quest.fishing.transfer_bait", std::chrono::seconds{600});
    const auto items = C::image("itemList"), icon = C::image("fishing/iconbait"), transfer = C::image("transfer");
    const auto recipient = C::image("whowillyougiveitto"), box = C::image("fishing/baitbox");
    const auto menu = C::any({items, icon, transfer, recipient, box});
    const auto inn = C::image("Inn");
    graph.route("Entry", {"Pending", "ExitPhase", "Find"});
    graph.observe("Pending", C::business("/fishing/transfer_pending", true), {"Uncertain"});
    graph.recovery("Uncertain", "quest.fishing_transfer_unconfirmed");
    graph.observe("ExitPhase", phase(3), {"Exit"});
    graph.route("Find", {"Box", "Recipient", "Transfer", "Icon", "OpenBag"});
    graph.observe("Box", box, {"AllSent", "Prepare"});
    graph.observe("Recipient", recipient, {"Box"});
    graph.click("Transfer", transfer, transfer, menu, {"Box", "Recipient", "Find"});
    graph.click("Icon", icon, icon, menu, {"Box", "Recipient", "Transfer", "Find"}, {639, 0});
    graph.fixed_click("OpenBag", items, menu, {135, 1294}, {"Box", "Recipient", "Transfer", "Icon", "SelectBag"});
    graph.fixed_click("SelectBag", items, menu, {660, 1200}, {"Find"});
    graph.observe("AllSent", C::business("/fishing/transfer_inputs_confirmed", 70), {"Finished"});
    graph.confirm("Prepare", "fishing.bait.prepare", "fishing_transfer_prepared", box, {"Give"});
    graph.click("Give", box, box, box, {"Given"});
    graph.delay_after("Give", 500);
    graph.confirm("Given", "fishing.bait.given", "fishing_transferred", box, {"Box"});
    graph.confirm("Finished", "fishing.supplies.finished", "fishing_supplies_finished", box, {"Exit"});
    graph.route("Exit", {"AtInn", "Return0", "Return1", "Return2", "Return3", "Return4"});
    graph.confirm("AtInn", "fishing.supplies.returned", "fishing_supplies_returned", C::all({inn, C::absent(menu)}), {"Terminal"});
    // 旧 fallback 的字符串return是Android返回键，不是名为return的模板。
    // 各页分别证明场景，避免输入前把所有菜单模板都重扫一遍而使当前帧过期。
    const std::array<J, 5> exit_pages{box, recipient, transfer, icon, items};
    for (std::size_t i = 0; i < exit_pages.size(); ++i) {
        const auto name = "Return" + std::to_string(i);
        graph.back(name, C::all({exit_pages[i], C::absent(inn)}), C::any({menu, inn}), {"Exit"});
        graph.hit_limit(name, 16);
    }
    for (const auto *name : {"Find", "Recipient", "Transfer", "Icon", "OpenBag", "SelectBag", "Exit"}) graph.hit_limit(name, 16);
    for (const auto *name : {"Box", "Prepare", "Give", "Given"}) graph.hit_limit(name, 71);
    graph.interrupt_on({{"mode", "blocking_screen"}, {"parallel_basic", true}}, "quest.fishing_transfer_unconfirmed");
    return graph.finish();
}
}
CompiledWorkflow fishing_cycle(const WvdQuestDefinition &definition, const J &profile,
    const std::set<std::string> &images, bool allow_download) {
    if (definition.type != "quest" || (definition.id != "fishing" && definition.id != "fishing2")) throw std::runtime_error("FISHING_TASK_INVALID");
    const auto plan = WvdTaskPlan::parse(definition);
    const auto to_shop = plan.with_route({{"position", "右上", {818, 928}}});
    const auto to_water = plan.with_entry({{"press", "DH", {"EdgeOfTown", {1, 1}}, 1},
        {"press", "DH-R6", "input swipe 650 250 650 900", 1}}).with_route({{"position", "右上", {339, 555}}});
    const auto approach = traverse_dungeon(to_shop, profile, images, allow_download);
    C graph("tasks." + definition.id, approach.time_limit + std::chrono::seconds{380});
    const auto cast = C::image("fishing/cast"), striking = C::image("fishing/striking"), reward = C::image("fishing/CloseFishInfo");
    const auto fishing = C::any({cast, striking, reward});
    const auto dungeon = C::image("dungFlag"), map = C::image("mapFlag"), inn = C::image("Inn"), world = C::image("intoWorldMap");
    const auto menu = C::any({C::image("itemList"), C::image("fishing/iconbait"), C::image("transfer"),
        C::image("whowillyougiveitto"), C::image("fishing/baitbox")});
    const auto city = C::any({inn, world});
    const J empty{{"mode", "fishing_bait_empty"}};
    graph.route("Entry", {"TransferPending", "ToSupplies", "Transferring", "LeavingSupplies", "BackToWater", "Fish"});
    graph.observe("TransferPending", C::business("/fishing/transfer_pending", true), {"Uncertain"});
    graph.recovery("Uncertain", "quest.fishing_transfer_unconfirmed");
    graph.observe("Fish", phase(0), {"Round"});
    const auto round = graph.define_child("Round", fishing_round(definition.id == "fishing2", allow_download), {"BaitExit"});
    graph.call_child("Round", round, {"NeedBait", "RoundDone"});
    graph.confirm("NeedBait", "fishing.refill.request", "fishing_bait_requested", C::all({cast, empty}), {"ToSupplies"});
    graph.observe("RoundDone", fishing, {"Terminal"});
    graph.observe("ToSupplies", phase(1), {"AtMenu", "AtCity", "AtDungeon", "QuitFishing"});
    graph.observe("AtMenu", menu, {"SuppliesEntered"});
    graph.observe("AtCity", city, {"Inventory"});
    graph.observe("AtDungeon", C::any({dungeon, map}), {"ApproachShop"});
    graph.click("QuitFishing", fishing, C::image("fishing/quit"), C::any({dungeon, map}), {"ApproachShop"});
    const auto travel_shop = graph.define_child("Approach", approach);
    graph.call_child("ApproachShop", travel_shop, {"Inventory"});
    graph.route("Inventory", {"AtMenu", "CityMenu", "InventoryReady"});
    graph.fixed_click("CityMenu", world, C::any({city, menu}), {50, 1535}, {"AtMenu", "InventoryReady"});
    // 只在已完成接近路线的地图/本内页或已确认城内菜单执行原固定点，不在未知页连点。
    // 业务条件只能选分支，不能变成动作目标；输入仍单独读取新帧的视觉正证据。
    graph.observe("InventoryReady", C::any({city, C::all({C::any({map, dungeon}), C::business("/task_step", 1)})}), {"OpenInventory"});
    graph.fixed_click("OpenInventory", C::any({city, map, dungeon}),
        C::any({city, menu, map, dungeon}), {860, 1150}, {"AtMenu", "InventoryReady"});
    graph.hit_limit("InventoryReady", 16);
    graph.hit_limit("OpenInventory", 16);
    graph.confirm("SuppliesEntered", "fishing.supplies.entered", "fishing_supplies_entered", menu, {"TransferBait"});
    graph.observe("Transferring", phase(2), {"TransferBait"});
    graph.observe("LeavingSupplies", phase(3), {"TransferBait"});
    const auto giving = graph.define_child("Supply", transfer_bait());
    graph.call_child("TransferBait", giving, {"Terminal"});
    graph.observe("BackToWater", phase(4), {"AlreadyFishing", "EnterDungeon"});
    graph.observe("AlreadyFishing", fishing, {"StillEmpty", "Refilled"});
    const auto entry = graph.define_child("WaterEntry", navigation::enter_dungeon(to_water));
    graph.call_child("EnterDungeon", entry, {"ApproachWater"});
    const auto water = graph.define_child("WaterRoute", traverse_dungeon(to_water, profile, images, allow_download));
    graph.call_child("ApproachWater", water, {"FindFishing"});
    graph.route("FindFishing", {"AlreadyFishing", "StartFishing", "CloseMap", "TurnWater"});
    graph.click("StartFishing", C::image("fishing/startfishing"), C::image("fishing/startfishing"), fishing, {"AlreadyFishing"});
    graph.click("CloseMap", map, map, dungeon, {"FindFishing"});
    graph.swipe("TurnWater", C::all({dungeon, C::absent(map)}), C::any({dungeon, fishing, C::image("fishing/startfishing")}),
        {450, 900, 450, 600}, {"AlreadyFishing", "StartFishing", "ApproachRod"});
    graph.fixed_click("ApproachRod", C::all({dungeon, C::absent(map)}), C::any({dungeon, fishing, C::image("fishing/startfishing")}),
        {450, 500}, {"FindFishing"});
    for (const auto *name : {"FindFishing", "CloseMap", "TurnWater", "ApproachRod"}) graph.hit_limit(name, 16);
    graph.observe("StillEmpty", C::all({cast, empty}), {"RefillFailed"});
    graph.recovery("RefillFailed", "quest.fishing_bait_still_empty");
    graph.confirm("Refilled", "fishing.refill.done", "fishing_refilled", C::all({cast, C::absent(empty)}), {"Terminal"});
    graph.delay_after("Refilled", 10000);
    graph.interrupt_on({{"mode", "blocking_screen"}, {"parallel_basic", true}}, "quest.fishing_common_screen_requires_dispatch");
    return graph.finish();
}
}
