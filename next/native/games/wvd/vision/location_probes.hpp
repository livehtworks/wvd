#pragma once
#include <json.hpp>
#include <utility>

namespace wvd::games::vision {
namespace detail {
inline nlohmann::json bright_icon(const char *image, nlohmann::json roi) {
    return {{"mode", "bright_mask"}, {"image", image}, {"roi", std::move(roi)},
            {"threshold", .82}, {"min_brightness", 145}};
}
}

// 各城市共用旅店、公会等建筑图标；只有这块固定塔楼背景能够确认王城身份。
// 这是只读地点证据，不能作为点击目标。
inline nlohmann::json royal_city() {
    return {{"mode", "template"}, {"image", "royal_city_background"},
            {"roi", {480, 400, 320, 350}}, {"threshold", .90}};
}

// 城市公共动作只截取不随语言变化的白色图标。图标可以用于定位点击和证明
// “当前在某个城市主界面”，但绝不能据此推断具体城市。
inline nlohmann::json inn_button() {
    return detail::bright_icon("inn_icon", {0, 450, 220, 250});
}
inline nlohmann::json guild_button() {
    return detail::bright_icon("guild_icon", {0, 650, 300, 250});
}
inline nlohmann::json edge_of_town_button() {
    return detail::bright_icon("edge_of_town_icon", {650, 700, 250, 250});
}
inline nlohmann::json city_screen() {
    return {{"mode", "any"},
            {"conditions", nlohmann::json::array(
                {inn_button(), guild_button(), edge_of_town_button()})}};
}
}
