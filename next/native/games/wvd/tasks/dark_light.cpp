#include "dark_light.hpp"
#include "games/wvd/chest/chest.hpp"
#include "games/wvd/combat/encounter.hpp"
#include "games/wvd/recovery/boot.hpp"
#include "games/wvd/recovery/revival.hpp"
#include "games/wvd/supply/dungeon_recover.hpp"

namespace wvd::games::tasks {
CompiledWorkflow dark_light(const WvdQuestDefinition &definition, const nlohmann::json &profile,
                            const std::set<std::string> &images, bool allow_download) {
    if (definition.id != "darkLight" || definition.type != "quest")
        throw std::runtime_error("DARK_LIGHT_TASK_INVALID");
    using C = PipelineCompiler;
    using J = nlohmann::json;
    const auto chest_flow = chest::open_chest(profile.at("WHO_WILL_OPEN_IT").get<int>(),
        profile.at("QUICK_DISARM_CHEST").get<bool>(), 0);
    C graph("tasks.darkLight", chest_flow.time_limit + std::chrono::seconds{400});
    const J battle{{"mode", "combat_active"}};
    const auto box = C::any({C::image("chestFlag"), C::image("whowillopenit"), C::image("chestOpening")});
    const auto revive = C::image("RiseAgain"), map = C::image("mapFlag");
    const auto encounter = C::any({battle, box, revive});
    const auto panel = C::any({C::image("trait"), C::image("recover")});
    const auto dungeon = C::all({C::image("dungFlag"), C::absent(map), C::absent(encounter), C::absent(panel)});
    const auto outside = C::all({C::any({C::image("Inn"), C::image("EdgeOfTown"), C::image("openworldmap"),
        C::image("returnText"), C::image("returntoTown")}), C::absent(map), C::absent(encounter)});
    const J blocked{{"mode", "blocking_screen"}};
    const auto lamp = C::image("darkLight"), light = C::image("darklight_lightIt");
    const J known{{"mode", "dark_light_post"}};
    graph.route("Entry", {"UnknownFrozen", "Outside", "ResumedTask", "Started", "UnknownTimeout", "Wait"});
    graph.observe("ResumedTask", C::business("/dark_light_active", true), {"Dispatch"});
    graph.confirm("Started", "darklight.enter", "dark_light_entered", C::any({dungeon, encounter, light}), {"Dispatch"});
    graph.route("Dispatch", {"UnknownFrozen", "Blocked", "Outside", "Combat", "Chest", "Revive",
        "Light", "HealingPanel", "Resume", "UnknownTimeout", "Wait"});
    graph.confirm("Outside", "darklight.leave", "dark_light_completed", outside, {"Terminal"});
    graph.observe("UnknownFrozen", {{"mode", "unknown_frozen"}, {"extra_known", {lamp, light}}}, {"FrozenExit"});
    graph.recovery("FrozenExit", "dungeon.unknown_static_window");
    graph.observe("UnknownTimeout", C::business("/encounter_timed_out", true), {"TimeoutExit"});
    graph.recovery("TimeoutExit", "dungeon.encounter_timeout");
    graph.route("Wait", {"Entry"});
    graph.delay_after("Wait", 1000);

    const auto common = graph.define_child("Common", recovery::clear_common_screens(allow_download));
    graph.observe("Blocked", blocked, {"ClearBlocking"});
    graph.call_child("ClearBlocking", common, {"Dispatch"});
    const auto combat_flow = graph.define_child("Battle", combat::fight_encounter(profile, images, 16), {"BlockedExit", "ReviveExit"});
    graph.observe("Combat", battle, {"Fight"});
    graph.call_child("Fight", combat_flow, {"Dispatch"});
    const auto chest_entry = graph.define_child("Box", chest_flow,
        {"CombatExit", "ReviveExit", "AmbushExit", "BlockedExit", "RetryExit"});
    graph.observe("Chest", box, {"OpenChest"});
    graph.call_child("OpenChest", chest_entry, {"Dispatch"});
    const auto resurrection = graph.define_child("Resurrection", recovery::revive_after_defeat(), {"BlockedExit"});
    graph.observe("Revive", revive, {"Resurrect"});
    graph.call_child("Resurrect", resurrection, {"Dispatch"});

    // 共用状态仍结算战斗/宝箱重叠时间与SKIP_*恢复决策，但不调用enter_dungeon。
    graph.confirm("Resume", "darklight.resume", "dungeon_resumed", dungeon, {"Dismiss"});
    graph.fixed_click("Dismiss", dungeon, known, {1, 1}, {"Blocked", "Combat", "Chest", "Revive", "Outside", "Heal"});
    const auto healing = graph.define_child("Healing", supply::recover_in_dungeon(), {"EncounterExit", "BlockedExit"});
    graph.observe("HealingPanel", C::all({panel, C::absent(encounter), C::business("/healing_required", true)}), {"Heal"});
    graph.call_child("Heal", healing, {"LightDispatch"});
    graph.route("LightDispatch", {"UnknownFrozen", "Blocked", "Combat", "Chest", "Revive", "Outside", "Light", "OpenLamp", "UnknownTimeout", "LightWait"});
    graph.route("LightWait", {"LightDispatch"});
    graph.delay_after("LightWait", 1000);
    const auto clear_light = C::all({light, C::absent(encounter), C::absent(blocked)});
    graph.click("Light", clear_light, light, C::all({known, C::absent(light)}), {"Dispatch"});
    graph.postcondition_budget("Light", 10000);
    graph.click("OpenLamp", C::all({dungeon, lamp, C::absent(blocked)}), lamp,
        C::any({light, encounter, outside, blocked}), {"LightDispatch"});
    graph.delay_after("OpenLamp", 1000);
    graph.postcondition_budget("OpenLamp", 10000);
    for (const auto *name : {"Entry", "ResumedTask", "Dispatch", "Wait", "Blocked", "ClearBlocking",
        "Combat", "Fight", "Chest", "OpenChest", "Revive", "Resurrect", "Resume", "Dismiss", "HealingPanel",
        "Heal", "LightDispatch", "LightWait", "Light", "OpenLamp"})
        graph.hit_limit(name, 128);
    return graph.finish();
}
}
