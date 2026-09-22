#include "cave_of_separation.hpp"
#include "games/wvd/navigation/dungeon_entry.hpp"
#include "games/wvd/navigation/return_city.hpp"
#include "games/wvd/navigation/time_leap.hpp"
#include "games/wvd/navigation/world_travel.hpp"
#include "games/wvd/recovery/boot.hpp"
#include "games/wvd/supply/inn.hpp"
#include "games/wvd/vision/location_probes.hpp"
#include <utility>

namespace wvd::games::tasks {
namespace {
using C = PipelineCompiler;
using J = nlohmann::json;
using Phase = quests::CaveOfSeparation::Phase;
using Segment = CaveOfSeparationSegment;
J phase(Phase value) {
    return C::all({C::business("/cave_of_separation/phase", static_cast<int>(value)),
        C::business("/cave_of_separation/unit_matches", true)});
}
J city() {
    return C::all({C::image("Inn"), C::absent(C::image("mapFlag")), C::absent(C::image("guildRequest")),
        C::absent(C::image("Stay")), C::absent(J{{"mode", "combat_active"}})});
}
// 原 fallback 是可见 return 按钮和 (1,1)，不是 Android 返回键。
// 未知页没有授权输入的场景锚点。
CompiledWorkflow find_city_menu(bool guild, bool royal_arrival = false) {
    C graph(guild ? "quest.cos.find_guild" : "quest.cos.find_inn", std::chrono::seconds{120});
    const auto target = guild ? C::all({C::image("guild"), C::absent(C::image("mapFlag")),
        C::absent(J{{"mode", "combat_active"}})}) : city();
    const auto back = C::image("return"), royal = C::image("City_RoyalCityLuknalia");
    const auto safe = C::all({C::any({C::image("mapFlag"), C::image("dungFlag"), C::image("EdgeOfTown"),
        C::image("Inn"), C::image("guild"), C::image("guildRequest"), royal}),
        C::absent(J{{"mode", "combat_active"}}), C::absent(C::image("RiseAgain"))});
    const auto post = C::any({target, safe, back});
    graph.route("Entry", royal_arrival ? J{"Done", "Royal", "Dismiss"} : J{"Done", "Return", "Dismiss"});
    graph.observe("Done", target, {"Terminal"});
    if (royal_arrival) {
        graph.click("Royal", C::all({royal, C::absent(target)}), royal, post, {"Entry"});
        graph.delay_after("Royal", 1000); graph.hit_limit("Royal", 32);
    } else {
        graph.click("Return", C::all({back, C::absent(target), C::absent(J{{"mode", "combat_active"}})}), back, post, {"Entry"});
        graph.delay_after("Return", 1000); graph.hit_limit("Return", 32);
    }
    graph.fixed_click("Dismiss", C::all({safe, C::absent(target), C::absent(royal_arrival ? royal : back)}), post, {1, 1}, {"Entry"});
    graph.delay_after("Dismiss", 1000); graph.hit_limit("Dismiss", 32); graph.hit_limit("Entry", 64);
    return graph.finish();
}
CompiledWorkflow request_sword() {
    C graph("quest.cos.request", std::chrono::seconds{180});
    const auto okay = C::image("COS/Okay"), request = C::image("guildRequest"), guild = C::image("guild");
    const auto back = C::image("return"), inn = city();
    const auto ready = C::any({okay, request});
    const auto known = C::any({inn, guild, ready, back, C::image("EdgeOfTown")});
    graph.route("Entry", {"Seen", "Observe", "Guild", "DismissOpening"});
    graph.observe("Seen", C::business("/cave_of_separation/request_seen", true), {"AcceptOrReturn"});
    graph.confirm("Observe", "cos.request.observe", "cos_request_observed", ready, {"AcceptOrReturn"});
    graph.click("Guild", C::all({guild, C::absent(ready)}), guild, known, {"Entry"});
    graph.fixed_click("DismissOpening", C::all({known, C::absent(guild), C::absent(ready)}), known, {1, 1}, {"Entry"});
    graph.route("AcceptOrReturn", {"Done", "Okay", "Return", "Dismiss"});
    graph.observe("Done", inn, {"Terminal"});
    graph.confirm("Okay", "cos.request.prepare", "cos_request_prepared", C::all({okay, C::absent(inn)}), {"Accept"});
    graph.click("Accept", okay, okay, C::all({known, C::absent(okay)}), {"Leave"});
    // 已发送 Okay 后，在取得确认回执前不能再次进入 Okay 节点。
    graph.route("Leave", {"Done", "Return", "Dismiss"});
    graph.click("Return", C::all({back, C::absent(inn), C::absent(okay)}), back, known, {"Leave"});
    graph.fixed_click("Dismiss", C::all({known, C::absent(inn), C::absent(okay), C::absent(back)}), known, {1, 1}, {"Leave"});
    for (const auto *node : {"Entry", "Guild", "DismissOpening", "Leave", "Return", "Dismiss"}) graph.hit_limit(node, 32);
    for (const auto *node : {"Guild", "DismissOpening", "Accept", "Return", "Dismiss"}) graph.delay_after(node, 1000);
    return graph.finish();
}
void entry_guards(C &graph, J next) {
    graph.route("Entry", {"Pending", "PaymentPending", "DialoguePending", "Ready"});
    graph.observe("Pending", C::business("/cave_of_separation/pending", true), {"Uncertain"});
    graph.observe("PaymentPending", C::business("/inn_payment_pending", true), {"Uncertain"});
    graph.observe("DialoguePending", C::business("/special_dialogue_pending", true), {"Uncertain"});
    graph.recovery("Uncertain", "quest.cos_side_effect_unconfirmed");
    graph.route("Ready", std::move(next));
}
CompiledWorkflow preparation(const WvdQuestDefinition &definition, const J &profile, bool download) {
    C graph("tasks.CaveOfSeperation.preparation", std::chrono::seconds{1500});
    const auto inn = city(), guild = C::image("guild");
    const auto royal_city = vision::royal_city();
    const auto leap_page = C::any({C::image("cursedWheelTitle"), C::image("cursedWheel"), C::image("ruins")});
    const auto outside = C::all({C::any({inn, C::image("dungFlag"), C::image("mapFlag"), C::image("EdgeOfTown"),
        C::image("returnText"), C::image("returntotown"), C::image("openworldmap")}),
        C::absent(C::image("leap")), C::absent(C::image("cursedWheelTitle"))});
    entry_guards(graph, {"Active", "Start"});
    graph.observe("Active", C::business("/cave_of_separation/active", true), {"Stage"});
    graph.confirm("Start", "cos.start", "cos_started", leap_page, {"Stage"});
    graph.route("Stage", {"LeapPhase", "FortressPhase", "RoyalPhase", "RequestPhase", "RestPhase", "EnterPhase"});
    graph.hit_limit("Stage", 12);
    graph.observe("LeapPhase", phase(Phase::Leap), {"PrepareLeap"});
    graph.confirm("PrepareLeap", "cos.leap.prepare", "cos_leap_prepared", leap_page, {"Leap"});
    const auto leap = graph.define_child("TimeLeap", profile.at("ACTIVE_CSC").get<bool>() ?
        navigation::time_leap_with_causality("GhostsOfYore", {"COS/ArnasPast", {}}, "cursedwheel_impregnableFortress", download) :
        navigation::time_leap_without_causality("GhostsOfYore", "cursedwheel_impregnableFortress", download));
    graph.call_child("Leap", leap, {"Leaped"});
    graph.confirm("Leaped", "cos.leaped", "cos_leaped", outside, {"Stage"});
    graph.delay_after("Leaped", 10000);
    graph.observe("FortressPhase", phase(Phase::Fortress), {"Fortress"});
    const auto fortress = graph.define_child("ReturnFortress", navigation::return_to_fortress());
    graph.call_child("Fortress", fortress, {"AtFortress"});
    graph.confirm("AtFortress", "cos.fortress", "cos_fortress", inn, {"Stage"});
    graph.observe("RoyalPhase", phase(Phase::RoyalCity), {"Royal"});
    const auto royal = graph.define_child("TravelRoyal", navigation::travel_city_to_city(
        {"City_RoyalCityLuknalia", TaskSwipe{{450, 150}, {500, 150}}, {550, 1}}));
    graph.call_child("Royal", royal, {"FindGuild"});
    const auto find = graph.define_child("RoyalGuild", find_city_menu(true, true));
    graph.call_child("FindGuild", find, {"AtRoyal"});
    graph.confirm("AtRoyal", "cos.royal", "cos_royal", royal_city, {"Stage"});
    graph.observe("RequestPhase", phase(Phase::Request), {"Request"});
    const auto request = graph.define_child("RequestSword", request_sword());
    graph.call_child("Request", request, {"Requested"});
    graph.confirm("Requested", "cos.requested", "cos_requested", inn, {"Stage"});
    graph.observe("RestPhase", phase(Phase::Rest), {"Rest"});
    const auto rest = graph.define_child("Inn", supply::rest_at_inn(profile.at("ACTIVE_ROYALSUITE_REST").get<bool>(), true));
    graph.call_child("Rest", rest, {"Rested"});
    graph.confirm("Rested", "cos.rested", "cos_rested", inn, {"Stage"});
    graph.observe("EnterPhase", phase(Phase::Enter), {"Enter"});
    const auto enter = graph.define_child("CaveEntry", navigation::enter_dungeon(cave_of_separation_plan(definition, Segment::Preparation)));
    graph.call_child("Enter", enter, {"Entered"});
    graph.confirm("Entered", "cos.entered", "cos_entered", C::any({C::image("mapFlag"), C::image("dungFlag"), J{{"mode", "combat_active"}}}), {"Terminal"});
    return graph.finish();
}
CompiledWorkflow return_city() {
    C graph("tasks.CaveOfSeperation.return_city", std::chrono::seconds{360});
    graph.use_dialogue(recovery::DialoguePolicy::CaveOfSeparationReturn);
    entry_guards(graph, {"GuildPhase", "InnPhase"});
    graph.observe("GuildPhase", phase(Phase::ReturnGuild), {"FindGuild"});
    const auto guild = graph.define_child("GuildMenu", find_city_menu(true));
    graph.call_child("FindGuild", guild, {"PrepareGuild"});
    graph.confirm("PrepareGuild", "cos.guild.prepare", "cos_guild_prepared", C::image("guild"), {"OpenGuild"});
    graph.click("OpenGuild", C::image("guild"), C::image("guild"), C::image("guildRequest"), {"OpenedGuild"});
    graph.delay_after("OpenGuild", 1000);
    graph.confirm("OpenedGuild", "cos.guild.entered", "cos_guild_entered", C::image("guildRequest"), {"InnPhase"});
    graph.observe("InnPhase", phase(Phase::ReturnInn), {"FindInn"});
    const auto inn = graph.define_child("InnMenu", find_city_menu(false));
    graph.call_child("FindInn", inn, {"Completed"});
    graph.confirm("Completed", "cos.completed", "cos_completed", city(), {"Terminal"});
    return graph.finish();
}
}
recovery::DialoguePolicy cave_of_separation_dialogue(Segment segment) {
    using D = recovery::DialoguePolicy;
    switch (segment) {
    case Segment::Preparation: return D::Default;
    case Segment::B1: return D::CaveOfSeparationOutbound;
    case Segment::B2: return D::CaveOfSeparationEna;
    case Segment::B3: return D::CaveOfSeparationRequest;
    case Segment::Back:
    case Segment::ReturnCity: return D::CaveOfSeparationReturn;
    }
    throw std::runtime_error("COS_SEGMENT_INVALID");
}
WvdTaskPlan cave_of_separation_plan(const WvdQuestDefinition &definition, Segment segment) {
    if (definition.id != "CaveOfSeperation" || definition.type != "quest") throw std::runtime_error("COS_TASK_INVALID");
    auto plan = WvdTaskPlan::parse(definition).with_entry({{"press", "COS/COS", {"EdgeOfTown", {1, 1}}, 1},
        {"press", "COS/COSENT", {1, 1}, 1}});
    switch (segment) {
    case Segment::Preparation:
    case Segment::ReturnCity: return plan;
    case Segment::B1: return plan.with_route({{"position", "右下", {232, 440}}, {"position", "右下", {819, 707}},
        {"position", "右上", {605, 501}}, {"stair_2", "右上", {72, 342}}});
    case Segment::B2: return plan.with_route({{"position", "右上", {394, 448}}, {"position", "右上", {446, 1088}},
        {"position", "左上", {452, 766}}});
    case Segment::B3: return plan.with_route({{"stair_3", "左上", {720, 822}}, {"position", "左下", {239, 600}},
        {"position", "左下", {185, 1185}}, {"position", "左下", {560, 652}}});
    case Segment::Back: return plan.with_route({{"stair_2", "左下", {827, 547}}, {"position", "右上", {394, 448}},
        {"position", "右上", {446, 1088}}, {"position", "左上", {452, 766}}, {"position", "左上", {559, 1087}},
        {"stair_1", "左上", {666, 448}}, {"position", "右下", {660, 919}}});
    }
    throw std::runtime_error("COS_SEGMENT_INVALID");
}
CompiledWorkflow cave_of_separation_segment(const WvdQuestDefinition &definition, const J &profile,
    const std::set<std::string> &images, Segment segment, bool allow_download) {
    const auto plan = cave_of_separation_plan(definition, segment);
    if (segment == Segment::Preparation) return preparation(definition, profile, allow_download);
    if (segment == Segment::ReturnCity) return return_city();
    const auto policy = cave_of_separation_dialogue(segment);
    const auto stop = segment == Segment::B2 ? DungeonTaskStop::CaveEna :
        segment == Segment::B3 ? DungeonTaskStop::CaveRequest : DungeonTaskStop::None;
    const auto route = traverse_dungeon(plan, profile, images, allow_download, policy, stop);
    C graph("tasks.CaveOfSeperation.route." + std::to_string(quests::CaveOfSeparation::segment_index(segment)),
        route.time_limit + std::chrono::seconds{180});
    graph.use_dialogue(policy);
    const auto p = segment == Segment::B1 ? Phase::B1 : segment == Segment::B2 ? Phase::B2 :
        segment == Segment::B3 ? Phase::B3 : Phase::Back;
    entry_guards(graph, {"Phase"});
    graph.observe("Phase", phase(p), {"Arrival"});
    // 新正常段可能从上一段遗留的对话页开始；
    // 先观察任务停点，不能将它交给对话点击流程。
    graph.route("Arrival", stop == DungeonTaskStop::None ? J{"Blocked", "Route"} : J{"AtStop", "Blocked", "Route"});
    if (stop != DungeonTaskStop::None)
        graph.observe("AtStop", C::image(dungeon_task_stop_image(stop)), {"Confirmed"});
    graph.observe("Blocked", J{{"mode", "blocking_screen"}}, {"Clear"});
    const auto common = graph.define_child("ArrivalDialogue", recovery::clear_common_screens(allow_download, policy));
    graph.call_child("Clear", common, {"Arrival"});
    for (const auto *node : {"Arrival", "Blocked", "Clear"}) graph.hit_limit(node, 32);
    const auto dungeon = graph.define_child("Dungeon", route);
    graph.call_child("Route", dungeon, {stop == DungeonTaskStop::None ? "AllPoints" : "Confirmed", "Incomplete"});
    J endpoint;
    if (stop == DungeonTaskStop::None) {
        graph.observe("AllPoints", C::business("/task_step", plan.route().size()), {"Confirmed"});
        const auto &last = plan.route().back();
        J reached{{"mode", last.target == "position" ? "reached" : "through_stair"}, {"position", *last.position}};
        if (last.target != "position") reached["image"] = last.target;
        endpoint = C::all({C::image("mapFlag"), reached, C::absent(J{{"mode", "blocking_screen"}, {"parallel_basic", true}}),
            C::absent(J{{"mode", "combat_active"}}), C::absent(C::image("chestFlag")), C::absent(C::image("RiseAgain"))});
    } else endpoint = C::all({C::image(dungeon_task_stop_image(stop)), C::absent(J{{"mode", "combat_active"}}),
        C::absent(C::image("RiseAgain"))});
    const auto event = segment == Segment::B1 ? "cos_b1_completed" : segment == Segment::B2 ? "cos_ena_confirmed" :
        segment == Segment::B3 ? "cos_request_confirmed" : "cos_back_completed";
    graph.confirm("Confirmed", "cos.route.done", event, endpoint, {"Terminal"});
    graph.recovery("Incomplete", "quest.cos_route_endpoint_unconfirmed");
    return graph.finish();
}
void configure_cave_of_separation_units(runtime::RunDefinition &definition,
    const std::array<runtime::SessionDefinition, quests::CaveOfSeparation::segments_per_cycle> &segments,
    std::size_t cycles) {
    constexpr auto count = quests::CaveOfSeparation::segments_per_cycle;
    if (!cycles || cycles > 256 / count || definition.max_business_units != 1 || !definition.continuation_units.empty())
        throw std::runtime_error("COS_UNIT_BUDGET_INVALID");
    for (std::size_t index = 0; index < count; ++index) {
        const auto &segment = segments[index];
        if (segment.entry.empty() || segment.terminal_node.empty() || segment.checkpoint_node.empty() ||
            segment.time_limit <= std::chrono::milliseconds{0} || segment.time_limit > std::chrono::seconds{1800} || segment.lifecycle)
            throw std::runtime_error("COS_SESSION_DEFINITION_INVALID");
        if (segment.bundle.revision.empty() || segment.bundle.revision != segments.front().bundle.revision)
            throw std::runtime_error("COS_SESSION_REVISION_MISMATCH");
        const auto policy = recovery::dialogue_policy_name(cave_of_separation_dialogue(static_cast<Segment>(index)));
        std::size_t vision_bindings = 0;
        for (const auto &binding : segment.recognitions) {
            if (binding.name != "WvdVision") continue;
            ++vision_bindings;
            if (!binding.parameters.is_object() || binding.parameters.value("dialogue_task", "") != policy)
                throw std::runtime_error("COS_SESSION_DIALOGUE_MISMATCH");
        }
        if (vision_bindings != 1) throw std::runtime_error("COS_SESSION_DIALOGUE_MISSING");
    }
    definition.initial = segments.front();
    definition.max_business_units = cycles * count;
    for (std::size_t unit = 1; unit < definition.max_business_units; ++unit)
        definition.continuation_units.push_back(segments[unit % count]);
}
}
