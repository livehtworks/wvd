#pragma once
#include <json.hpp>

namespace wvd::games::vision {
// 启动页和地图地点下载页的按钮高度不同；只用按钮文字作动作锚点。
inline nlohmann::json download_button_en() {
    return {{"mode", "template"}, {"image", "startdownload"},
            {"threshold", .80}, {"roi", {200, 860, 500, 460}}};
}
inline nlohmann::json download_button_zh_hant() {
    return {{"mode", "template"}, {"image", "startdownload_zh_hant"},
            {"threshold", .86}, {"roi", {200, 860, 500, 460}}};
}
} // namespace wvd::games::vision
