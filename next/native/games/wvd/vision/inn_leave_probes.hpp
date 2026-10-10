#pragma once
#include <json.hpp>
#include "location_probes.hpp"

namespace wvd::games::vision {
// 从实际旅店菜单截取名称，不借住宿按钮的偏移定位繁中“離開”。
inline nlohmann::json inn_leave_zh() {
    return {{"mode", "template"}, {"image", "inn_leave_zh_hant"},
        {"threshold", 0.86}, {"roi", {0, 500, 900, 1100}}};
}
inline nlohmann::json character_page() {
    return {{"mode", "template"}, {"image", "trait"}, {"threshold", 0.84}};
}
inline nlohmann::json notice_advance_arrow() {
    // 信息弹窗高度随内容变化，搜索整个下半屏；仍点击真实匹配中心。
    return {{"mode", "template"}, {"image", "chest_reward_advance"},
        {"threshold", .9}, {"roi", {0, 800, 900, 800}}};
}
inline nlohmann::json inn_notice_page() {
    using J = nlohmann::json;
    const auto absent = [](J value) { return J{{"mode", "not"}, {"conditions", J::array({std::move(value)})}}; };
    return {{"mode", "all"}, {"conditions", J::array({notice_advance_arrow(),
        absent(J{{"mode", "template"}, {"image", "story_auto_control"}, {"threshold", .9},
            {"roi", {20, 1430, 200, 120}}}), absent(J{{"mode", "combat_active"}}),
        absent(resource("purchase.premium.button", "zh-Hant"))})}};
}
} // namespace wvd::games::vision
