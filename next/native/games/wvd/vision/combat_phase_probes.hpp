#pragma once
#include "chest_probes.hpp"
#include <string_view>

namespace wvd::games::vision {
inline nlohmann::json combat_phase(const char *phase) {
    return {{"mode", "combat_phase"}, {"phase", phase}};
}
inline nlohmann::json combat_popup_probes() {
    using J = nlohmann::json;
    return J::array({J{{"mode", "template"}, {"image", "combat_skill_detail"}, {"threshold", .8}},
        J{{"mode", "template"}, {"image", "combat_skill_confirm"}, {"threshold", .8}},
        J{{"mode", "template"}, {"image", "close"}, {"threshold", .8}, {"roi", {0,600,900,1000}}}});
}
inline nlohmann::json combat_end_probes() {
    using J = nlohmann::json;
    auto probes = J::array({J{{"mode", "template"}, {"image", "dungFlag"}, {"threshold", .8}}});
    const auto stages = chest_page_condition();
    for (const auto &probe : stages.at("conditions")) probes.push_back(probe);
    probes.push_back(J{{"mode", "template"}, {"image", "RiseAgain"}, {"threshold", .8}});
    return probes;
}
// The compiler freezes every reachable resource, even when the live phase skips it.
inline nlohmann::json combat_phase_dependencies(std::string_view phase) {
    using J = nlohmann::json;
    auto probes = J::array({J{{"mode", "combat_active"}}});
    if (phase == "clear" || phase == "menu" || phase == "finished" || phase == "target_handoff")
        for (const auto &probe : combat_popup_probes()) probes.push_back(probe);
    if (phase == "menu") probes.push_back(J{{"mode", "template"}, {"image", "flee"},
        {"threshold", .8}, {"roi", {660,1080,240,220}}});
    if (phase == "ended" || phase == "finished" || phase == "detail_handoff" || phase == "target_handoff")
        for (const auto &probe : combat_end_probes()) probes.push_back(probe);
    if (phase == "dungeon") probes.push_back(combat_end_probes().at(0));
    if (phase == "chest") {
        const auto stages = chest_page_condition();
        for (const auto &probe : stages.at("conditions")) probes.push_back(probe);
    }
    if (phase == "revival") probes.push_back(combat_end_probes().back());
    if (phase == "detail_handoff" || phase == "target_handoff") {
        probes.push_back(combat_popup_probes().at(0));
        probes.push_back(J{{"mode", "prepared_actor"}});
    }
    if (phase == "finished" || phase == "detail_handoff" || phase == "target_handoff")
        for (const auto *image : {"notenoughsp", "notenoughmp"})
            probes.push_back(J{{"mode", "template"}, {"image", image}, {"threshold", .8}});
    return probes;
}
}
