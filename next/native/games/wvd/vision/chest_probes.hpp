#pragma once
#include "location_probes.hpp"

namespace wvd::games::vision {
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
}
