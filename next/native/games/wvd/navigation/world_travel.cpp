#include "world_travel.hpp"
#include <array>

namespace wvd::games::navigation {
tasks::CompiledWorkflow travel_world(const WorldDestination &destination, WorldArrival arrival) {
    using C = tasks::PipelineCompiler;
    using J = nlohmann::json;
    C graph("navigation.world_travel");
    const auto world = C::image("worldmapflag"), target = C::image(destination.target);
    const auto inn = C::image("Inn"), open = C::image("openworldmap"), into = C::image("intoWorldMap");
    const auto expected = arrival == WorldArrival::City ? inn : C::any({open, C::image("dungFlag")});
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
