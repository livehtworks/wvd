#pragma once
#include "location_probes.hpp"
#include "harken_probes.hpp"
#include "chest_probes.hpp"
#include "download_probes.hpp"
#include "network_probes.hpp"
#include <json.hpp>
#include <utility>

namespace wvd::games::vision {
// 透明覆盖层可能保留业务底图。这里只排除会遮住当前输入的已知提示，
// 不做城市/公会/钓鱼/宝箱等全局分类；配方同时供资源冻结使用。
inline nlohmann::json input_blockers(const std::string &phase = {}) {
    using J = nlohmann::json;
    auto probes = J::array({network_retry_prompt(), download_button_zh_hant(), download_button_en(),
        J{{"mode", "pause"}}});
    if (phase == "navigation") {
        probes.push_back(harken_buff_menu());
        for (const auto *image : {"blessing", "ambush", "ignore", "sandman_recover"})
            probes.push_back({{"mode", "template"}, {"image", image}});
    }
    return probes;
}
// 结果不符后的异常诊断：网络/资源/启动/Pause/死亡。正常业务帧不调用此表。
inline nlohmann::json exception_probes() {
    using J = nlohmann::json;
    J probes = J::array({network_prompt_zh_hant(), download_button_zh_hant(), download_button_en()});
    probes.push_back(resource("boot.announcement.page"));
    for (const auto &[image, threshold] : {std::pair{"retry_blank", .65}, std::pair{"retry", .8},
                                         std::pair{"retry", .60}, std::pair{"totitle", .8}})
        probes.push_back({{"mode", "template"}, {"image", image}, {"threshold", threshold}});
    for (const auto *image : {"boot_attention_zh", "boot_attention"})
        probes.push_back({{"mode", "template"}, {"image", image}, {"threshold", .86},
                          {"roi", {250, 430, 420, 220}}});
    probes.push_back({{"mode", "template"}, {"image", "boot_title_logo"}, {"threshold", .86},
                      {"roi", {100, 300, 700, 470}}});
    for (const auto *mode : {"pause", "party_death", "party_defeat"}) probes.push_back({{"mode", mode}});
    return probes;
}
// 特殊流程不与网络故障、战斗、开箱混成一个全局候选池。
inline nlohmann::json special_screen_probes() {
    using J = nlohmann::json;
    J probes = J::array({J{{"mode", "special_dialogue"}}, J{{"mode", "default_dialogue"}}});
    for (const auto *image : {"sandman_recover", "blessing", "ambush", "ignore"})
        probes.push_back({{"mode", "template"}, {"image", image}});
    probes.push_back(harken_buff_menu());
    return probes;
}
// 阻塞页的资源/参数由视觉和发布清单共同消费；低阈值仅保留旧 Retry 退路。
inline nlohmann::json blocking_probes(bool include_party_prompts = true) {
    using J = nlohmann::json;
    J probes = J::array();
    probes.push_back(resource("boot.announcement.page"));
    auto add = [&](const char *image, J roi = nullptr, double threshold = .8) {
        J p{{"mode", "template"}, {"image", image}, {"threshold", threshold}};
        if (!roi.is_null())
            p["roi"] = std::move(roi);
        probes.push_back(std::move(p));
    };
    probes.push_back(download_button_zh_hant());
    probes.push_back(network_prompt_zh_hant());
    probes.push_back(download_button_en());
    add("retry_blank", nullptr, .65);
    add("retry");
    add("retry", nullptr, .60);
    add("totitle");
    // 小地图的继续移动按钮也叫 resume；它常驻迷宫，不是阻塞页。
    add("boot_attention_zh", {250, 430, 420, 220}, .86);
    add("boot_attention", {250, 430, 420, 220}, .86);
    add("boot_title_logo", {100, 300, 700, 470}, .86);
    // Pause 可能保留底层战斗/地图图标，必须先作为覆盖层处理。
    probes.push_back({{"mode", "pause"}});
    add("sandman_recover");
    add("blessing");
    probes.push_back(harken_buff_menu());
    add("ambush");
    add("ignore");
    // 死亡提示只有在正常场景全部不成立时才生效，不能让王城/地图骷髅抢占导航。
    if (include_party_prompts) {
        probes.push_back({{"mode", "party_death"}});
        probes.push_back({{"mode", "default_dialogue"}});
        probes.push_back({{"mode", "party_defeat"}});
    }
    return probes;
}
// 启动就绪只看稳定游戏场景；动作后置还接受已知的中间阻塞页。
inline nlohmann::json boot_probes(bool transient) {
    using J = nlohmann::json;
    J probes = transient ? blocking_probes() : J::array();
    // 各城市的建筑按钮图标相同，不能用来区分地点。王城身份只由其固定塔楼
    // 背景确认；该锚点只读，不用于点击或推断其他城市业务状态。
    probes.push_back(royal_city());
    // 郊外和哈肯楼层菜单也是可交给任务继续收敛的稳定场景；它们不是城市完成证据。
    probes.push_back(outskirts_return_button());
    probes.push_back(harken_floor_menu());
    // 公会页也属于已启动的稳定游戏画面；从工作台直接运行子流程时不能卡在启动门禁。
    probes.push_back(resource("guild.commissions.page", "zh-Hant"));
    probes.push_back(resource("guild.bounties.page", "zh-Hant"));
    // 底部既有关闭锚点只证明在游戏内的详情页，不判断城市、悬赏刷新或任务完成。
    // WANTED列表卡头与全屏“新的悬赏令”布局不同，不能强制复用列表卡头的ROI。
    probes.push_back(resource("guild.bounty.reveal.close", "zh-Hant"));
    for (auto name : {"Inn", "dungFlag", "worldmapflag", "openworldmap", "returnText", "returntoTown",
                       "mapFlag", "fishing/cast", "fishing/striking", "fishing/CloseFishInfo",
                       "cursedWheelTitle", "cursedWheel", "ruins"})
        probes.push_back({{"mode", "template"}, {"image", name}, {"threshold", .8}});
    // 失败后可停在繁中荒屋/轮盘页，不能只接受英文素材而卡在启动门禁。
    // 与time_leap一致，只证明已在游戏内，不证明已跳轮或位于某个章节。
    probes.push_back({{"mode", "template"}, {"image", "cursedWheel_zh_hant"},
        {"threshold", .8}, {"roi", {450, 500, 450, 400}}});
    probes.push_back({{"mode", "template"}, {"image", "cursedWheelTitle_zh_hant"},
        {"threshold", .8}, {"roi", {250, 0, 400, 180}}});
    for (const auto &probe : chest_stage_probes()) probes.push_back(probe);
    probes.push_back({{"mode", "combat_active"}});
    return probes;
}
}
