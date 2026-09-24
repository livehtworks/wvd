#pragma once
#include <json.hpp>

namespace wvd::games::vision {
inline nlohmann::json chest_open_probes() {
    return nlohmann::json::array({
        {{"mode", "template"}, {"image", "chestFlag"}, {"threshold", .8}},
        {{"mode", "template"}, {"image", "chest_open_zh_hant"}, {"threshold", .8},
         {"roi", {330, 1200, 240, 130}}}
    });
}

inline nlohmann::json chest_choose_probes() {
    return nlohmann::json::array({
        {{"mode", "template"}, {"image", "whowillopenit"}, {"threshold", .8}},
        {{"mode", "template"}, {"image", "chest_choose_zh_hant"}, {"threshold", .8},
         {"roi", {280, 880, 340, 145}}}
    });
}

inline nlohmann::json chest_stage_probes() {
    auto probes = chest_open_probes();
    for (const auto &probe : chest_choose_probes()) probes.push_back(probe);
    probes.push_back({{"mode", "template"}, {"image", "chestOpening"}, {"threshold", .8}});
    probes.push_back({{"mode", "template"}, {"image", "chest_reward_advance"}, {"threshold", .8},
                      {"roi", {750, 1400, 150, 150}}});
    return probes;
}
}
