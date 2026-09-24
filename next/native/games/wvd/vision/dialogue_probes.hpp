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
inline nlohmann::json story_auto_control() {
    return {{"mode", "template"}, {"image", "story_auto_control"}, {"threshold", .9},
            {"roi", {20, 1430, 200, 120}}};
}
inline nlohmann::json story_advance_arrow() {
    return {{"mode", "template"}, {"image", "chest_reward_advance"}, {"threshold", .9},
            {"roi", {750, 1400, 150, 150}}};
}
// 普通剧情只点继续箭头；已知选项页由独立对话策略处理，不能在导航中代选。
inline nlohmann::json ordinary_story_page() {
    const nlohmann::json no_choice = {
        {"mode", "not"}, {"conditions", nlohmann::json::array({{{"mode", "default_dialogue"}}})}};
    return {{"mode", "all"},
            {"conditions", nlohmann::json::array({story_auto_control(), story_advance_arrow(), no_choice})}};
}
}
