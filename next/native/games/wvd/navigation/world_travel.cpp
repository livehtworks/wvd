#include "world_travel.hpp"
#include "games/wvd/vision/location_probes.hpp"
#include <array>

namespace wvd::games::navigation {
namespace {
nlohmann::json city_arrival(const WorldDestination &destination) {
    if (destination.target == "City_RoyalCityLuknalia")
        return vision::royal_city();
    return vision::inn_button();
}
}
tasks::CompiledWorkflow travel_city_to_city(const WorldDestination &destination) {
    using C = tasks::PipelineCompiler;
    C graph("navigation.city_to_city", std::chrono::seconds{120});
    const auto world = C::image("worldmapflag");
    const auto open = C::image("intoWorldMap");
    const auto arrived = C::all({city_arrival(destination), C::absent(world)});
    graph.route("Entry", {"OnWorld", "Open"});
    graph.observe("OnWorld", world, {"Travel"});
    graph.click("Open", C::all({vision::inn_button(), open, C::absent(world)}), open, world, {"Travel"});
    const auto travel = graph.define_child("World", travel_world(destination, WorldArrival::City));
    graph.call_child("Travel", travel, {"Arrived"});
    graph.observe("Arrived", arrived, {"Terminal"});
    return graph.finish();
}
tasks::CompiledWorkflow travel_world(const WorldDestination &destination, WorldArrival arrival) {
    using C = tasks::PipelineCompiler;
    using J = nlohmann::json;
    C graph("navigation.world_travel");
    const auto world = C::image("worldmapflag"), target = C::image(destination.target);
    const auto inn = vision::inn_button(), open = C::image("openworldmap"), into = C::image("intoWorldMap");
    const auto expected = arrival == WorldArrival::City ? city_arrival(destination) : C::any({open, C::image("dungFlag")});
    const auto arrived = C::all({expected, C::absent(world)});
    const auto searching = C::all({world, C::absent(expected)});
    const auto located = C::all({searching, target});
    const auto missing = C::all({searching, C::absent(target)});
    graph.route("Entry", {"Arrived", "OpenWorld", "Locate"});
    graph.observe("Arrived", arrived, {"Terminal"});
    if (arrival == WorldArrival::City)
        graph.click("OpenWorld", C::all({open, C::absent(inn), C::absent(world)}), open,
                    C::any({world, arrived}), {"Arrived", "Locate"});
    else
        graph.click("OpenWorld", C::all({inn, into, C::absent(world)}), into,
                    C::any({world, arrived}), {"Arrived", "Locate"});
    graph.route("Locate", {"Arrived", "Click0", "Relocate"});
    if (destination.swipe) {
        const auto &s = *destination.swipe;
        graph.swipe("Relocate", missing, C::any({world, arrived}),
                    {s.from[0], s.from[1], s.to[0], s.to[1]}, {"Arrived", "Click0", "Dismiss"});
        graph.delay_after("Relocate", 1000);
        graph.fixed_click("Dismiss", missing, C::any({world, arrived}), destination.dismiss, {"Locate"});
    } else
        graph.fixed_click("Relocate", missing, C::any({world, arrived}), destination.dismiss, {"Locate"});
    const std::array<std::array<int, 2>, 5> offsets{
        {{0, 0}, {0, -55}, {-35, -35}, {35, -35}, {0, 35}}};
    for (std::size_t i = 0; i < offsets.size(); ++i) {
        const auto next = "Click" + std::to_string((i + 1) % offsets.size());
        graph.click("Click" + std::to_string(i), located, target, C::any({world, arrived}),
                    {"Arrived", next, "Relocate"}, offsets[i]);
        graph.delay_after("Click" + std::to_string(i), 1500);
        graph.postcondition_budget("Click" + std::to_string(i), 22000);
    }
    return graph.finish();
}
}
