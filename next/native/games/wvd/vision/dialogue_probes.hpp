#pragma once
#include <array>
#include <string_view>
#include <json.hpp>
#include "boot_probes.hpp"

namespace wvd::games::vision {
// 固定旧 reflectImage(dialogueChoices) 的排序结果；mod 只替换已知图片，不扩充候选名。
inline constexpr std::array<std::string_view, 14> default_dialogue_names{
    "!adventurersbones", "!halfBone", "#hammer", "DontBuyIt", "buyNothing", "dontGiveAntitoxin",
    "donthelp", "givehimnothing", "idonthaveany", "ignorethequest", "nope", "nothanks",
    "spareit", "strange_things"};
inline nlohmann::json default_dialogue_probes() {
    auto probes = nlohmann::json::array();
    for (auto name : default_dialogue_names)
        probes.push_back({{"mode", "template"}, {"image", "dialogueChoices/" + std::string(name)}, {"threshold", .8}});
    return probes;
}
inline nlohmann::json default_dialogue_normal_probes() {
    auto probes = boot_probes(false);
    for (auto name : {"trait", "recover", "spellskill/skillDetail", "City_RoyalCityLuknalia",
                      "City_fortress", "City_DHI", "City_portTownGrandLegion"})
        probes.push_back({{"mode", "template"}, {"image", name}});
    return probes;
}
}
