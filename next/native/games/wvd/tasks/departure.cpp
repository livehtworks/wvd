#include "departure.hpp"
#include "games/wvd/navigation/world_travel.hpp"
#include "games/wvd/supply/inn.hpp"
#include "games/wvd/supply/party.hpp"

namespace wvd::games::tasks {
CompiledWorkflow prepare_departure(const WvdTaskPlan &plan, const nlohmann::json &profile,
                                   bool force_rest) {
    using C = PipelineCompiler;
    using J = nlohmann::json;
    C graph("tasks.departure." + plan.definition().id, std::chrono::seconds{180});
    const auto world = C::image("worldmapflag"), open = C::image("openworldmap");
    const auto town = C::image("returntoTown"), prompt = C::image("returnText");
    const auto inside = C::any({C::image("dungFlag"), C::image("mapFlag"), C::image("chestFlag"), J{{"mode", "combat_active"}}});
    const auto city = C::all({C::image("Inn"), C::absent(world), C::absent(C::image("Stay")),
                              C::absent(C::image("PartyManagementTitle")), C::absent(prompt)});
    const auto outside = C::all({C::absent(inside), C::absent(world), C::absent(prompt)});
    const auto eot = C::all({outside, C::image("EdgeOfTown"), C::absent(C::image("Inn"))});
    const auto known = C::any({city, world, open, town, prompt, eot, inside});
    graph.route("Entry", {"Dispatch"});
    graph.route("Dispatch", {"Inside", "Prompt", "PendingInn", "City", "Town", "World", "Open", "Edge"});
    graph.hit_limit("Dispatch", 32);
    graph.observe("Inside", inside, {"InsideExit"});
    graph.recovery("InsideExit", "departure.already_inside");
    graph.click("Prompt", prompt, prompt, known, {"Dispatch"});
    graph.observe("PendingInn", C::all({C::image("Stay"), C::absent(C::image("OK")), C::absent(inside)}),
                    {"PaidBeforeInterruption", "UnconfirmedInn"});
    graph.observe("PaidBeforeInterruption", C::business("/inn_rest_completed", true), {"Rest"});
    graph.observe("UnconfirmedInn", C::business("/inn_rest_completed", false), {"InnUncertain"});
    graph.recovery("InnUncertain", "departure.inn_payment_unconfirmed");
    graph.observe("City", city, {"PartyDue", "PartyNotDue"});
    graph.observe("PartyDue", C::business("/party_refresh_due", true), {"Assemble"});
    graph.observe("PartyNotDue", C::business("/party_refresh_due", false), {"RestDecision"});
    const auto party = graph.append("Party", supply::assemble_party(), {"PartyConfirmed"});
    graph.route("Assemble", {party});
    graph.confirm("PartyConfirmed", "departure.party", "party_reassembled", city, {"RestDecision"});
    graph.route("RestDecision", force_rest ? J{"AlreadyRested", "NeedsRest"} : J{"OrdinaryRest", "SkipRest"});
    if (force_rest) {
        graph.observe("AlreadyRested", C::business("/inn_rest_completed", true), {"ReadyCity"});
        graph.observe("NeedsRest", C::business("/inn_rest_completed", false), {"Rest"});
    } else {
        graph.observe("OrdinaryRest", C::business("/ordinary_rest_due", true), {"Rest"});
        graph.observe("SkipRest", C::business("/ordinary_rest_due", false), {"ReadyCity"});
    }
    const auto rest = graph.append("Inn", supply::rest_at_inn(profile.at("ACTIVE_ROYALSUITE_REST").get<bool>(), true), {"ReadyCity"});
    graph.route("Rest", {rest});
    graph.observe("ReadyCity", city, {"Terminal"});
    graph.observe("Edge", eot, {"Terminal"});

    // returntoTown 与 openworldmap 的旧回城条件不同，不能合并成单个 required()。
    graph.observe("Town", C::all({outside, town}), force_rest ? J{"BackTown"} : J{"TownDue", "TownSkipped"});
    if (!force_rest) {
        graph.observe("TownDue", C::business("/city_supply_due", true), {"BackTown"});
        graph.observe("TownSkipped", C::business("/city_supply_due", false), {"ReadyTown"});
        graph.observe("ReadyTown", C::all({outside, town}), {"Terminal"});
    }
    graph.back("BackTown", C::all({outside, town, C::absent(C::image("Inn"))}),
                C::any({city, town}), {"City", "BackTown"});
    graph.observe("Open", C::all({outside, open, C::absent(town)}),
                    force_rest ? J{"OpenForRest"} : J{"OpenDue", "OpenSkipped"});
    if (!force_rest) {
        graph.observe("OpenDue", C::business("/ordinary_rest_due", true), {"OpenForRest"});
        graph.observe("OpenSkipped", C::business("/ordinary_rest_due", false), {"ReadyOpen"});
        graph.observe("ReadyOpen", C::all({outside, open}), {"Terminal"});
    }
    graph.click("OpenForRest", C::all({outside, open}), open, C::any({world, city}), {"Dispatch"});
    graph.observe("World", C::all({world, C::absent(inside)}), {"Travel"});
    if (plan.return_destination()) {
        const auto travel = graph.append("ReturnCity", navigation::travel_world(*plan.return_destination(), navigation::WorldArrival::City), {"Dispatch"});
        graph.route("Travel", {travel});
    } else
        graph.recovery("Travel", "departure.return_destination_missing");
    return graph.finish();
}
}
