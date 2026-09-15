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
// moving 的原谓词完全展开：本内锚点成立，地图、遭遇和退场锚点均不成立。
// 统一定义也供发布时收集资源；不能只优化识别却漏封存隐式模板。
inline nlohmann::json auto_route_moving_probes() {
    using J = nlohmann::json;
    J probes = J::array({J{{"mode", "template"}, {"image", "dungFlag"}, {"threshold", .8}},
        J{{"mode", "template"}, {"image", "mapFlag"}, {"threshold", .8}},
        J{{"mode", "combat_active"}}});
    for (const auto *name : {"chestFlag", "chestOpening", "whowillopenit", "RiseAgain"})
        probes.push_back({{"mode", "template"}, {"image", name}, {"threshold", .8}});
    for (const auto &probe : auto_route_outside_probes()) probes.push_back(probe);
    return probes;
}
inline nlohmann::json map_route_post_probes() {
    using J = nlohmann::json;
    J probes = J::array({J{{"mode", "template"}, {"image", "mapFlag"}, {"threshold", .8}},
        J{{"mode", "template"}, {"image", "dungFlag"}, {"threshold", .8}}, J{{"mode", "combat_active"}}});
    for (const auto *name : {"chestFlag", "whowillopenit", "chestOpening"})
        probes.push_back({{"mode", "template"}, {"image", name}, {"threshold", .8}});
    for (const auto &probe : auto_route_outside_probes()) probes.push_back(probe);
    return probes;
}
}
