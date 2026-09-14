#pragma once
#include <json.hpp>

namespace wvd::games::vision {
// 启动页顺序探针的唯一资源/参数定义，由视觉与发布清单共同消费。
inline nlohmann::json boot_probes(bool transient) {
    using J = nlohmann::json;
    J probes = J::array();
    auto add = [&](const char *image, J roi = nullptr, double threshold = .8) {
        J p{{"mode", "template"}, {"image", image}, {"threshold", threshold}};
        if (!roi.is_null())
            p["roi"] = std::move(roi);
        probes.push_back(std::move(p));
    };
    if (transient) {
        add("boot_title_logo", {100, 300, 700, 470}, .86);
        add("boot_attention", {250, 430, 420, 220}, .86);
        add("startdownload", {222, 901, 465, 84});
        add("retry");
        add("retry_blank", nullptr, .65);
        add("totitle");
        add("resume");
    }
    for (auto name : {"Inn", "dungFlag", "worldmapflag", "openworldmap", "returnText", "returntoTown",
                       "mapFlag", "chestFlag", "whowillopenit", "fishing/cast", "fishing/striking", "fishing/CloseFishInfo"})
        add(name);
    probes.push_back({{"mode", "combat_active"}});
    return probes;
}
}
