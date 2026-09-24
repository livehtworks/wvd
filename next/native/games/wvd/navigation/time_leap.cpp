#include "time_leap.hpp"
#include "games/wvd/vision/location_probes.hpp"
#include "games/wvd/vision/download_probes.hpp"
#include "causality.hpp"

namespace wvd::games::navigation {
namespace {
tasks::CompiledWorkflow compile_time_leap(const std::string &target_name,
    const std::string &chapter_name, bool allow_download, const CausalitySettings *causality) {
    using C = tasks::PipelineCompiler;
    using J = nlohmann::json;
    if (target_name.empty() || chapter_name.empty() || target_name == chapter_name)
        throw std::runtime_error("TIME_LEAP_TARGET_INVALID");
    C graph(causality ? "navigation.time_leap_with_causality" : "navigation.time_leap_without_causality",
        causality ? std::chrono::seconds{480} : std::chrono::seconds{180});
    const auto title_en = C::image("cursedWheelTitle");
    auto title_zh_hant = C::image("cursedWheelTitle_zh_hant");
    title_zh_hant["roi"] = {250, 0, 400, 180};
    const auto wheel_en = C::image("cursedWheel");
    auto wheel_zh_hant = C::image("cursedWheel_zh_hant");
    wheel_zh_hant["roi"] = {450, 500, 450, 400};
    const auto ruins_en = C::image("ruins");
    const auto ruins_icon = vision::ruins_button();
    const auto title = C::any({title_en, title_zh_hant});
    const auto wheel = C::any({wheel_en, wheel_zh_hant});
    const auto ruins = C::any({ruins_en, ruins_icon});

    std::string translated_target_name;
    if (target_name == "BeautifulOre") translated_target_name = "BeautifulOre_zh_hant";
    else if (target_name == "GhostsOfYore") translated_target_name = "GhostsOfYore_zh_hant";
    else if (target_name == "Triumph") translated_target_name = "Triumph_zh_hant";
    else if (target_name == "FortressArrival") translated_target_name = "FortressArrival_zh_hant";
    else if (target_name == "RescueKing") translated_target_name = "RescueKing_zh_hant";
    else if (target_name == "ReturnRoyalCity") translated_target_name = "ReturnRoyalCity_zh_hant";
    const auto target_en = C::image(target_name);
    J target_zh_hant;
    if (!translated_target_name.empty()) {
        target_zh_hant = C::image(translated_target_name);
        target_zh_hant["roi"] = {150, 250, 650, 900};
    }
    // 这两个历史目标只有繁中素材，不能把不存在的英文模板编进候选。
    const bool zh_only_target = target_name == "RescueKing" || target_name == "ReturnRoyalCity";
    const auto target = translated_target_name.empty() ? target_en
        : zh_only_target ? target_zh_hant : C::any({target_en, target_zh_hant});

    std::string translated_chapter_name;
    if (chapter_name == "cursedwheel_dhi") translated_chapter_name = "cursedwheel_dhi_zh_hant";
        else if (chapter_name == "cursedwheel_impregnableFortress")
            translated_chapter_name = "cursedwheel_impregnableFortress_zh_hant";
        else if (chapter_name == "TradeWaterway") translated_chapter_name = "TradeWaterway_zh_hant";
        else if (chapter_name == "beginningAbyss") translated_chapter_name = "beginningAbyss_zh_hant";
    const auto chapter_en = C::image(chapter_name);
    J chapter_zh_hant;
    if (!translated_chapter_name.empty()) {
        chapter_zh_hant = C::image(translated_chapter_name);
        chapter_zh_hant["roi"] = {250, 120, 400, 220};
    }
    const auto chapter = translated_chapter_name.empty() ? chapter_en : C::any({chapter_en, chapter_zh_hant});
    const auto leap_en = C::image("leap");
    auto leap_zh_hant = C::image("leap_zh_hant");
    leap_zh_hant["roi"] = {250, 1250, 400, 250};
    const auto leap = C::any({leap_en, leap_zh_hant});
    const auto right = C::image("cursedWheelTapRight");
    const auto download_en = vision::download_button_en();
    const auto download_zh_hant = vision::download_button_zh_hant();
    const auto download = C::any({download_en, download_zh_hant});
    const auto chooser = C::any({title, chapter, leap});
    const auto outside = C::all({C::any({vision::royal_city(), vision::city_screen(), C::image("dungFlag"),
        C::image("mapFlag"), C::image("returnText"), C::image("returntotown"), C::image("openworldmap")}),
        C::absent(C::any({title, leap}))});
    const auto opening = C::any({chooser, wheel, download});
    const auto opened_wheel = C::any({chooser, download});
    const auto after_leap = C::any({chooser, outside});
    // 当前就在王城时优先使用稳定塔楼背景完成输入前复核；公共建筑图标只说明
    // “某个城市界面”，不能把它们当成王城身份。
    const auto city = C::all({C::any({vision::royal_city(), vision::city_screen(), C::image("openworldmap")}),
        C::absent(C::any({title, wheel, download, C::image("dungFlag"), C::image("mapFlag"),
                         C::image("chestFlag"), J{{"mode", "combat_active"}}, J{{"mode", "blocking_screen"}}}))});
    graph.route("Entry", {"AtTitle", "OpenWheelEn", "OpenWheelZhHant", "RuinsEn",
                            "RuinsZhHant", "DownloadEn", "DownloadZhHant", "OpenFromRoyalCity"});
    // 旧源码中的 [1,1] 只是前面图片候选全部失败后的兜底点击，不能迁成主动作。
    // 王城背景只确认城市身份；荒屋图标本身给出点击位置，动作后再确认荒屋菜单。
    graph.click("OpenFromRoyalCity", C::all({vision::royal_city(), city, ruins_icon}),
                ruins_icon, wheel, {"Entry"});
    graph.delay_after("OpenFromRoyalCity", 1000);
    graph.postcondition_budget("OpenFromRoyalCity", 30000);
    graph.hit_limit("OpenFromRoyalCity", 3);
    J quick_select = J::array();
    if (!zh_only_target) quick_select.push_back("QuickSelectEn");
    if (!translated_target_name.empty()) quick_select.push_back("QuickSelectZhHant");
    quick_select.push_back("Reset0");
    graph.observe("AtTitle", title, quick_select);
    graph.click("OpenWheelEn", C::all({wheel_en, C::absent(title)}), wheel_en, opened_wheel, {"Entry"});
    graph.click("OpenWheelZhHant", C::all({wheel_zh_hant, C::absent(title)}), wheel_zh_hant,
                opened_wheel, {"Entry"});
    graph.click("RuinsEn", C::all({ruins_en, C::absent(title)}), ruins_en, wheel, {"Entry"});
    graph.click("RuinsZhHant", C::all({ruins_icon, C::absent(title)}), ruins_icon, wheel, {"Entry"});
    if (allow_download) {
        graph.click("DownloadEn", download_en, download_en, opening, {"Entry"});
        graph.click("DownloadZhHant", download_zh_hant, download_zh_hant,
                    opening, {"Entry"});
    } else {
        graph.observe("DownloadEn", download_en, {"DownloadDenied"});
        graph.observe("DownloadZhHant", download_zh_hant, {"DownloadDenied"});
        graph.recovery("DownloadDenied", "boot.download_permission_missing");
    }
    // 原函数的可见目标快路径会直接跳跃，不重置页签也不调整因果。
    if (!zh_only_target) {
        graph.click("QuickSelectEn", C::all({title, target_en}), target_en, leap, {"QuickLeapRoute"});
        graph.delay_after("QuickSelectEn", 2000);
    }
    if (!translated_target_name.empty()) {
        graph.click("QuickSelectZhHant", C::all({title, target_zh_hant}), target_zh_hant,
                    leap, {"QuickLeapRoute"});
        graph.delay_after("QuickSelectZhHant", 2000);
    }
    graph.route("QuickLeapRoute", {"QuickLeapEn", "QuickLeapZhHant"});
    graph.click("QuickLeapEn", leap_en, leap_en, after_leap, {"Done", "Reset0"});
    graph.click("QuickLeapZhHant", leap_zh_hant, leap_zh_hant, after_leap, {"Done", "Reset0"});
    graph.delay_after("QuickLeapEn", 2000);
    graph.delay_after("QuickLeapZhHant", 2000);
    for (int i = 0; i < 10; ++i) {
        const auto name = "Reset" + std::to_string(i);
        graph.fixed_click(name, chooser, chooser, {105, 230},
            i == 9 ? J{"FindChapter"} : J{"Reset" + std::to_string(i + 1)});
        graph.delay_after(name, 500);
    }
    J find_chapter = J::array({"ChapterEn"});
    if (!translated_chapter_name.empty()) find_chapter.push_back("ChapterZhHant");
    find_chapter.push_back("NextTab");
    find_chapter.push_back("ReopenWheelEn");
    find_chapter.push_back("ReopenWheelZhHant");
    graph.route("FindChapter", find_chapter);
    J after_chapter = J::array();
    if (!zh_only_target) after_chapter.push_back("SelectTargetEn");
    if (!translated_target_name.empty()) after_chapter.push_back("SelectTargetZhHant");
    after_chapter.push_back("Scroll0");
    graph.click("ChapterEn", C::all({chooser, chapter_en}), chapter_en, chooser, after_chapter);
    if (!translated_chapter_name.empty())
        graph.click("ChapterZhHant", C::all({chooser, chapter_zh_hant}), chapter_zh_hant,
                    chooser, after_chapter);
    graph.click("NextTab", C::all({chooser, right, C::absent(chapter)}), right, chooser, {"FindChapter"});
    graph.click("ReopenWheelEn", C::all({wheel_en, C::absent(chapter)}), wheel_en,
                opened_wheel, {"FindChapter"});
    graph.click("ReopenWheelZhHant", C::all({wheel_zh_hant, C::absent(chapter)}), wheel_zh_hant,
                opened_wheel, {"FindChapter"});
    for (int i = 0; i < 3; ++i) {
        const auto name = "Scroll" + std::to_string(i);
        graph.swipe(name, C::all({title, C::absent(target)}), title, {450, 1200, 450, 200},
            i == 2 ? J{"FindTarget"} : J{"Scroll" + std::to_string(i + 1)});
        graph.delay_after(name, 2000);
    }
    J find_target = J::array();
    if (!zh_only_target) find_target.push_back("SelectTargetEn");
    if (!translated_target_name.empty()) find_target.push_back("SelectTargetZhHant");
    find_target.push_back("FineScroll");
    graph.route("FindTarget", find_target);
    if (!zh_only_target) {
        graph.click("SelectTargetEn", C::all({title, target_en}), target_en, leap,
                    causality ? J{"Causality"} : J{"LeapRoute"});
        graph.delay_after("SelectTargetEn", 1000);
    }
    if (!translated_target_name.empty()) {
        graph.click("SelectTargetZhHant", C::all({title, target_zh_hant}), target_zh_hant,
                    leap, causality ? J{"Causality"} : J{"LeapRoute"});
        graph.delay_after("SelectTargetZhHant", 1000);
    }
    graph.swipe("FineScroll", C::all({title, C::absent(target)}), title,
        {50, 1200, 50, 1300}, {"FindTarget"});
    graph.delay_after("FineScroll", 1000);
    if (causality) {
        const auto child = graph.define_child("CausalitySettings", adjust_causality(*causality));
        graph.call_child("Causality", child, {"LeapRoute"});
    }
    graph.route("LeapRoute", {"LeapEn", "LeapZhHant"});
    J leap_next = J::array({"Done"});
    if (!zh_only_target) leap_next.push_back("ReselectEn");
    if (!translated_target_name.empty()) leap_next.push_back("ReselectZhHant");
    leap_next.push_back(causality ? "Causality" : "LeapRoute");
    graph.click("LeapEn", leap_en, leap_en, after_leap, leap_next);
    graph.click("LeapZhHant", leap_zh_hant, leap_zh_hant, after_leap, leap_next);
    graph.delay_after("LeapEn", 2000);
    graph.delay_after("LeapZhHant", 2000);
    if (!zh_only_target)
        graph.click("ReselectEn", C::all({title, target_en}), target_en, leap,
                    causality ? J{"Causality"} : J{"LeapRoute"});
    if (!translated_target_name.empty())
        graph.click("ReselectZhHant", C::all({title, target_zh_hant}), target_zh_hant,
                    leap, causality ? J{"Causality"} : J{"LeapRoute"});
    // 离开按钮不是充分条件。只有已执行跳跃路径之后的新帧正常游戏锚点才是终点。
    // 启动时的城市画面不能直达这里，未知加载帧也不能算业务完成。
    graph.observe("Done", outside, {"Terminal"});
    for (const auto *name : {"Entry", "AtTitle", "OpenWheelEn", "OpenWheelZhHant",
        "RuinsEn", "RuinsZhHant", "DownloadEn", "DownloadZhHant", "FindChapter",
        "NextTab", "ReopenWheelEn", "ReopenWheelZhHant", "FindTarget", "FineScroll",
        "LeapRoute", "LeapEn", "LeapZhHant"})
        graph.hit_limit(name, 32);
    if (!zh_only_target) graph.hit_limit("ReselectEn", 32);
    if (!translated_target_name.empty()) graph.hit_limit("ReselectZhHant", 32);
    return graph.finish();
}
}
tasks::CompiledWorkflow time_leap_without_causality(const std::string &target,
    const std::string &chapter, bool allow_download) {
    return compile_time_leap(target, chapter, allow_download, nullptr);
}
tasks::CompiledWorkflow time_leap_with_causality(const std::string &target, const CausalitySettings &settings,
    const std::string &chapter, bool allow_download) {
    return compile_time_leap(target, chapter, allow_download, &settings);
}
}
