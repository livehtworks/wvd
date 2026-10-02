#pragma once
#include <json.hpp>

namespace wvd::games::vision {
// 从实际旅店菜单截取名称，不借住宿按钮的偏移定位繁中“離開”。
inline nlohmann::json inn_leave_zh() {
    return {{"mode", "template"}, {"image", "inn_leave_zh_hant"},
        {"threshold", 0.86}, {"roi", {500, 550, 400, 550}}};
}
inline nlohmann::json character_page() {
    return {{"mode", "template"}, {"image", "trait"}, {"threshold", 0.84}};
}
} // namespace wvd::games::vision
