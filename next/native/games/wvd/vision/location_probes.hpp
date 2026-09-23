#pragma once
#include <json.hpp>
#include <utility>

namespace wvd::games::vision {
namespace detail {
inline nlohmann::json shape_icon(const char *image, nlohmann::json roi, double threshold) {
    return {{"mode", "template"}, {"image", image}, {"roi", std::move(roi)},
            {"threshold", threshold}, {"grayscale", true}};
}
}

// 各城市共用旅店、公会等建筑图标；只有这块固定塔楼背景能够确认王城身份。
// 这是只读地点证据，不能作为点击目标。
inline nlohmann::json royal_city() {
    return {{"mode", "template"}, {"image", "royal_city_background"},
            {"roi", {480, 400, 320, 350}}, {"threshold", .90}};
}

// 只保留图标形状，不包含文字、背景或 Bonus 状态亮点。正负轮廓一起匹配，
// 避免亮色遮罩在公会等非城市画面中误命中亮区域。图标可以用于定位点击和证明
// “当前在某个城市主界面”，但绝不能据此推断具体城市。
inline nlohmann::json inn_button() {
    return detail::shape_icon("inn_icon_shape", {20, 520, 200, 180}, .80);
}
inline nlohmann::json temple_button() {
    return detail::shape_icon("temple_icon_shape", {270, 390, 160, 175}, .75);
}
inline nlohmann::json blacksmith_button() {
    return detail::shape_icon("blacksmith_icon_shape", {365, 555, 180, 180}, .72);
}
inline nlohmann::json ruins_button() {
    return detail::shape_icon("ruins_icon_shape", {730, 525, 170, 180}, .70);
}
inline nlohmann::json guild_button() {
    return detail::shape_icon("guild_icon_shape", {60, 700, 200, 180}, .80);
}
inline nlohmann::json ore_merchant_button() {
    return detail::shape_icon("ore_merchant_icon_shape", {240, 745, 180, 170}, .78);
}
inline nlohmann::json item_shop_button() {
    // Bonus 为橙色，常态为白色；归一化轮廓匹配不依赖这两种颜色的固定 RGB 值。
    return detail::shape_icon("item_shop_icon_shape", {515, 710, 170, 175}, .72);
}
inline nlohmann::json edge_of_town_button() {
    return detail::shape_icon("edge_of_town_icon_shape", {700, 750, 190, 170}, .80);
}
inline nlohmann::json city_screen() {
    return {{"mode", "any"},
            {"conditions", nlohmann::json::array(
                {inn_button(), temple_button(), blacksmith_button(), ruins_button(),
                 guild_button(), ore_merchant_button(), item_shop_button(),
                 edge_of_town_button()})}};
}
}
