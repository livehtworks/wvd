#pragma once
#include "chest_probes.hpp"
#include "harken_probes.hpp"
#include <json.hpp>

namespace wvd::games::vision {
// Auto 动作后允许的已知状态。地图本身不是成功证据，但地图上的遭遇/无目标提示可返回分派。
inline nlohmann::json navigation_no_route_probe(const std::string &locale) {
    return resource("navigation.no_route", locale == "zh-Hant" ? "zh-Hant" : "en");
}
inline nlohmann::json auto_route_probes(const std::string &locale = {}) {
    using J = nlohmann::json;
    J probes = J::array({J{{"mode", "combat_active"}}});
    for (const auto &probe : chest_stage_probes()) probes.push_back(probe);
    for (const auto *name : {"RiseAgain"})
        probes.push_back({{"mode", "template"}, {"image", name}, {"threshold", .8}});
    if (locale == "zh-Hant") {
        probes.push_back(harken_floor_menu());
        probes.push_back(harken_buff_menu());
        probes.push_back(outskirts_return_button());
    }
    probes.push_back(navigation_no_route_probe(locale));
    return probes;
}
inline nlohmann::json auto_route_outside_probes() {
    nlohmann::json probes = nlohmann::json::array();
    for (const auto *name : {"Inn", "EdgeOfTown", "returnText", "returntoTown", "openworldmap", "worldmapflag"})
        probes.push_back({{"mode", "template"}, {"image", name}, {"threshold", .8}});
    return probes;
}
// Normal dungeon controls first. Only the map and character panels share this
// background; other flow classification belongs to the caller's failure path.
// Keep the dependency list identical to the probes actually evaluated.
inline nlohmann::json auto_route_moving_probes() {
    using J = nlohmann::json;
    return J::array({J{{"mode", "template"}, {"image", "dungFlag"}, {"threshold", .8}},
        J{{"mode", "template"}, {"image", "mapFlag"}, {"threshold", .8}},
        J{{"mode", "template"}, {"image", "trait"}, {"threshold", .8}},
        J{{"mode", "template"}, {"image", "recover"}, {"threshold", .8}}});
}
inline nlohmann::json map_route_post_probes() {
    using J = nlohmann::json;
    J probes = J::array({J{{"mode", "template"}, {"image", "mapFlag"}, {"threshold", .8}},
        J{{"mode", "template"}, {"image", "dungFlag"}, {"threshold", .8}}, J{{"mode", "combat_active"}}});
    for (const auto &probe : chest_stage_probes()) probes.push_back(probe);
    for (const auto &probe : auto_route_outside_probes()) probes.push_back(probe);
    return probes;
}
}
