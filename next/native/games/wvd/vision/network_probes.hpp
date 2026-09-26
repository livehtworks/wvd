#pragma once
#include <json.hpp>

namespace wvd::games::vision {
inline nlohmann::json network_retry_button_zh_hant() {
    return {{"mode", "template"}, {"image", "network_retry_zh_hant"},
            {"threshold", .86}, {"roi", {260, 800, 380, 220}}};
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
        {{"mode", "template"}, {"image", "retry"}, {"threshold", .86},
         {"roi", {200, 600, 500, 600}}}}}};
}
}
