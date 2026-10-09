#include "world_travel.hpp"
#include "games/wvd/vision/location_probes.hpp"
#include "games/wvd/vision/download_probes.hpp"
#include "games/wvd/vision/dialogue_probes.hpp"
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
    graph.click("Open", C::all({open, C::absent(world)}), open, world, {"Travel"});
    graph.retry_menu_input("Open", vision::menu_retry_ready(C::all({open, C::absent(world)})), 3000);
    const auto travel = graph.define_child("World", travel_world(destination, WorldArrival::City));
    graph.call_child("Travel", travel, {"Arrived"});
    graph.observe("Arrived", arrived, {"Terminal"});
    return graph.finish();
}
tasks::CompiledWorkflow travel_world(const WorldDestination &destination, WorldArrival arrival) {
    using C = tasks::PipelineCompiler;
    using J = nlohmann::json;
    C graph("navigation.world_travel", std::chrono::seconds{300});
    const auto world = C::image("worldmapflag"), target = C::image(destination.target);
    const auto inn = vision::inn_button(), open = C::image("openworldmap"), into = C::image("intoWorldMap");
    const auto download_en = vision::download_button_en();
    const auto download_zh_hant = vision::download_button_zh_hant();
    const auto download = C::any({download_en, download_zh_hant});
    const auto story = vision::ordinary_story_page();
    const auto advance = vision::story_advance_arrow();
    const auto expected = arrival == WorldArrival::City ? city_arrival(destination) : C::any({open, C::image("dungFlag")});
    // 城市底图可能透过剧情/下载框显示；底图出现不等于已经能进行下一步。
    const auto arrived = C::all({expected, C::absent(world), C::absent(story), C::absent(download)});
    const auto searching = C::all({world, C::absent(expected), C::absent(story), C::absent(download)});
    const auto located = C::all({searching, target});
    const auto missing = C::all({searching, C::absent(target)});
    graph.route("Entry", {"DownloadEn", "DownloadZhHant", "Arrived", "Story", "OpenWorld", "Locate", "Poll"});
    graph.hit_limit("Entry", 256);
    graph.click("DownloadEn", download_en, download_en, C::absent(download), {"Entry"});
    graph.click("DownloadZhHant", download_zh_hant, download_zh_hant, C::absent(download), {"Entry"});
    graph.retry_menu_input("DownloadEn", download_en, 3000);
    graph.retry_menu_input("DownloadZhHant", download_zh_hant, 3000);
    graph.wait("Poll", 1000, {"Entry"});
    graph.hit_limit("Poll", 256);
    graph.observe("Arrived", arrived, {"Terminal"});
    graph.click("Story", story, advance, C::any({story, arrived, download}), {"Entry"});
    graph.delay_after("Story", 700);
    graph.hit_limit("Story", 50);
    if (arrival == WorldArrival::City)
        graph.click("OpenWorld", C::all({into, C::absent(world)}), into,
                    C::any({world, arrived, story, download}), {"Entry"});
    else
        graph.click("OpenWorld", C::all({inn, into, C::absent(world)}), into,
                    C::any({world, arrived, story, download}), {"Entry"});
    graph.route("Locate", {"DownloadEn", "DownloadZhHant", "Story", "Arrived", "Click0", "Relocate"});
    graph.retry_menu_input("OpenWorld", vision::menu_retry_ready(C::all({into, C::absent(world)})), 3000);
    graph.hit_limit("Locate", 128);
    if (destination.swipe) {
        const auto &s = *destination.swipe;
        graph.swipe("Relocate", missing, C::any({world, arrived, story, download}),
                    {s.from[0], s.from[1], s.to[0], s.to[1]},
                    {"DownloadEn", "DownloadZhHant", "Story", "Arrived", "Click0", "Dismiss"});
        graph.delay_after("Relocate", 1000);
        graph.fixed_click("Dismiss", missing, C::any({world, arrived, story, download}), destination.dismiss, {"Locate"});
    } else
        graph.fixed_click("Relocate", missing, C::any({world, arrived, story, download}), destination.dismiss, {"Locate"});
    const std::array<std::array<int, 2>, 5> offsets{
        {{0, 0}, {0, -55}, {-35, -35}, {35, -35}, {0, 35}}};
    for (std::size_t i = 0; i < offsets.size(); ++i) {
        const auto next = "Click" + std::to_string((i + 1) % offsets.size());
        graph.click("Click" + std::to_string(i), located, target,
                    C::any({arrived, download, C::absent(world)}),
                    {"DownloadEn", "DownloadZhHant", "Story", "Arrived", next, "Relocate"}, offsets[i]);
        graph.delay_after("Click" + std::to_string(i), 1500);
        graph.postcondition_budget("Click" + std::to_string(i), 22000);
        graph.retry_menu_input("Click" + std::to_string(i), vision::menu_retry_ready(located), 3000);
    }
    return graph.finish();
}
}
