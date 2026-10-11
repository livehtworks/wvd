#pragma once
#include "location_probes.hpp"
#include "search_regions.hpp"
#include "supply_scene_plan.hpp"
#include "chest_probes.hpp"
#include <optional>

namespace wvd::games::vision {
// Runtime leaves and preparation dependencies share these exact recipes.
inline std::optional<nlohmann::json> builtin_template_probe(const std::string &mode) {
    using J = nlohmann::json;
    if (mode == "fast_forward_off")
        return J{{"mode","template"},{"image","fastforward_off"},{"roi",{190,1440,100,100}}};
    if (mode == "target_marker" || mode == "next_low_confidence")
        return J{{"mode","template"},{"image",mode=="target_marker" ? "combatTarget" : "next"},
            {"roi",combat_target_search_roi()},{"threshold",mode=="target_marker" ? .86 : .60}};
    return std::nullopt;
}
inline nlohmann::json movement_page_probes(const std::string &locale) {
    using J = nlohmann::json;
    return J::array({J{{"mode","template"},{"image","dungFlag"}},
        locale=="zh-Hant" ? resource("dungeon.map.open","zh-Hant") : J{{"mode","template"},{"image","mapFlag"},{"threshold",.8}},
        J{{"mode","auto_route_moving"}},J{{"mode","input_clear"},{"phase","navigation"}}});
}
inline nlohmann::json combat_resource_error_text() {
    return {{"mode","ocr"},{"language","zh-Hant"},{"expected",{"SP不足","MP不足","SP 不足","MP 不足","SP不夠","MP不夠"}},
        {"match","contains"},{"unique",false},{"threshold",.9},{"roi",{0,600,900,1000}}};
}
inline nlohmann::json implicit_ocr_probes(const std::string &mode) {
    using J = nlohmann::json;
    if (mode=="revival_prompt") return J::array({resource("party.revival.action","zh-Hant")});
    if (mode=="combat_resource_error") return J::array({combat_resource_error_text()});
    return J::array();
}
inline nlohmann::json revival_normal_page_probes() {
    using J = nlohmann::json;
    // These are actual page anchors, not HP bars or the rescue screen's X.
    // Keep runtime and publication dependencies on the same recipes.
    auto probes = J::array({J{{"mode","template"},{"image","dungFlag"}},
        J{{"mode","template"},{"image","trait"}},
        J{{"mode","template"},{"image","recover"}}});
    const auto stages = chest_page_condition();
    for (const auto &probe : stages.at("conditions")) probes.push_back(probe);
    return probes;
}
}
