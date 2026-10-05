#pragma once
#include <json.hpp>

namespace wvd::games::vision {
inline nlohmann::json network_retry_button_zh_hant() {
    // 单按钮时居中，双按钮时“重試”移到右侧；ROI覆盖整条按钮行，
    // 仍由文字模板定位点击中心，不能固定点屏幕中间或左侧“返回标题”。
    return {{"mode", "template"}, {"image", "network_retry_zh_hant"},
            {"threshold", .86}, {"roi", {100, 800, 700, 220}}};
}
inline nlohmann::json network_prompt_zh_hant() {
    // 网络正文和重试按钮必须同时存在；不能用通用“发生错误”误认维护或购买确认。
    return {{"mode", "all"}, {"conditions", {
        {{"mode", "template"}, {"image", "network_error_zh_hant"},
         {"threshold", .86}, {"roi", {200, 650, 500, 180}}},
        network_retry_button_zh_hant()}}};
}
inline nlohmann::json network_retry_prompt() {
    return {{"mode", "any"}, {"conditions", {
        network_prompt_zh_hant(),
        {{"mode", "template"}, {"image", "retry"}, {"locale_only", "en"}, {"threshold", .86},
         {"roi", {200, 600, 500, 600}}}}}};
}
}
