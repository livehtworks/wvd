#pragma once
#include <json.hpp>

namespace wvd::games::vision {
// 阻塞页的资源/参数由视觉和发布清单共同消费；低阈值仅保留旧 Retry 退路。
inline nlohmann::json blocking_probes(bool include_party_death = true) {
    using J = nlohmann::json;
    J probes = J::array();
    auto add = [&](const char *image, J roi = nullptr, double threshold = .8) {
        J p{{"mode", "template"}, {"image", image}, {"threshold", threshold}};
        if (!roi.is_null())
            p["roi"] = std::move(roi);
        probes.push_back(std::move(p));
    };
    add("startdownload", {222, 901, 465, 84});
    add("retry_blank", nullptr, .65);
    add("retry");
    add("retry", nullptr, .60);
    add("totitle");
    add("resume");
    add("boot_attention", {250, 430, 420, 220}, .86);
    add("boot_title_logo", {100, 300, 700, 470}, .86);
    // Pause 可能保留底层战斗/地图图标，必须先作为覆盖层处理。
    probes.push_back({{"mode", "pause"}});
    // 死亡提示只有在正常场景全部不成立时才生效，不能让王城/地图骷髅抢占导航。
    if (include_party_death)
        probes.push_back({{"mode", "party_death"}});
    return probes;
}
// 启动就绪只看稳定游戏场景；动作后置还接受已知的中间阻塞页。
inline nlohmann::json boot_probes(bool transient) {
    using J = nlohmann::json;
    J probes = transient ? blocking_probes() : J::array();
    for (auto name : {"Inn", "dungFlag", "worldmapflag", "openworldmap", "returnText", "returntoTown",
                       "mapFlag", "chestFlag", "whowillopenit", "fishing/cast", "fishing/striking", "fishing/CloseFishInfo"})
        probes.push_back({{"mode", "template"}, {"image", name}, {"threshold", .8}});
    probes.push_back({{"mode", "combat_active"}});
    return probes;
}
}
