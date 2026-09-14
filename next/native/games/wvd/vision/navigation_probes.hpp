#pragma once
#include <json.hpp>

namespace wvd::games::vision {
// Auto 动作后允许的已知状态。地图本身不是成功证据，但地图上的遭遇/无目标提示可返回分派。
inline nlohmann::json auto_route_probes() {
    using J = nlohmann::json;
    J probes = J::array({J{{"mode", "combat_active"}}});
    for (const auto *name : {"chestFlag", "chestOpening", "whowillopenit", "RiseAgain",
                             "NoChestCanBeFound", "theRouteToTheDestinationCannotBeFound"})
        probes.push_back({{"mode", "template"}, {"image", name}, {"threshold", .8}});
    return probes;
}
inline nlohmann::json auto_route_outside_probes() {
    nlohmann::json probes = nlohmann::json::array();
    for (const auto *name : {"Inn", "EdgeOfTown", "returnText", "returntoTown", "openworldmap", "worldmapflag"})
        probes.push_back({{"mode", "template"}, {"image", name}, {"threshold", .8}});
    return probes;
}
}
