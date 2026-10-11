#pragma once
#include "location_probes.hpp"
#include <string_view>

namespace wvd::games::vision {
inline nlohmann::json chest_phase(const char *phase) {
    return {{"mode", "chest_phase"}, {"phase", phase}};
}
// All known chest stages are scene boundaries, not proof of receiving a particular item.
inline nlohmann::json chest_page_condition() {
    auto stages = nlohmann::json::array();
    for (const auto *image : {"chestFlag", "whowillopenit", "chestOpening", "chest_reward_advance"}) {
        nlohmann::json probe{{"mode", "template"}, {"image", image}, {"threshold", .8}};
        if (std::string_view(image) == "chest_reward_advance") probe["roi"] = {730, 1330, 170, 270};
        stages.push_back(std::move(probe));
    }
    return {{"mode", "any"}, {"conditions", std::move(stages)}};
}
inline nlohmann::json chest_open_probes() {
    return nlohmann::json::array({
        resource("chest.open.option", "en"),
        resource("chest.open.option", "zh-Hant")
    });
}

inline nlohmann::json chest_choose_probes() {
    return nlohmann::json::array({
        resource("chest.choose.page", "en"),
        resource("chest.choose.page", "zh-Hant")
    });
}

inline nlohmann::json chest_stage_probes() {
    auto probes = chest_open_probes();
    for (const auto &probe : chest_choose_probes()) probes.push_back(probe);
    probes.push_back({{"mode", "template"}, {"image", "chestOpening"}, {"threshold", .8}});
    probes.push_back(resource("chest.reward.page"));
    return probes;
}
inline nlohmann::json chest_phase_dependencies() {
    using J = nlohmann::json;
    auto probes = chest_page_condition().at("conditions");
    for (const auto *mode : {"party_death", "party_defeat", "combat_active"})
        probes.push_back(J{{"mode", mode}});
    for (const auto *image : {"dungFlag", "ambush", "RiseAgain"})
        probes.push_back(J{{"mode", "template"}, {"image", image}, {"threshold", .8}});
    return probes;
}
}
