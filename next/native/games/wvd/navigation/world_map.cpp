#include "world_map.hpp"
#include <array>

namespace wvd::games::navigation {
tasks::CompiledWorkflow enter_city(const std::string &city) {
    using C = tasks::PipelineCompiler;
    C graph("navigation.enter_city");
    const auto world = C::image("worldmapflag"), inn = C::image("Inn");
    const auto target = C::image(city);
    // 到城优先于旧地图点位。每个点都重新识别目标，不继续发送上一帧整批坐标。
    graph.route("Entry", {"Arrived", "Click0"});
    graph.observe("Arrived", C::all({inn, C::absent(world)}), {"Terminal"});
    const std::array<std::array<int, 2>, 5> offsets{
        {{0, 0}, {0, -55}, {-35, -35}, {35, -35}, {0, 35}}};
    for (std::size_t i = 0; i < offsets.size(); ++i) {
        const auto next = "Click" + std::to_string((i + 1) % offsets.size());
        graph.click("Click" + std::to_string(i), C::all({world, C::absent(inn)}), target,
                    C::any({world, inn}), {"Arrived", next}, offsets[i]);
    }
    return graph.finish();
}
} // namespace wvd::games::navigation
