#include "time_leap.hpp"
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
    const auto title = C::image("cursedWheelTitle"), wheel = C::image("cursedWheel");
    const auto target = C::image(target_name), chapter = C::image(chapter_name), leap = C::image("leap");
    const auto right = C::image("cursedWheelTapRight"), ruins = C::image("ruins");
    auto download = C::image("startdownload");
    download["roi"] = {222, 901, 465, 84};
    const auto chooser = C::any({title, chapter, leap});
    const auto outside = C::all({C::any({C::image("Inn"), C::image("dungFlag"), C::image("mapFlag"),
        C::image("EdgeOfTown"), C::image("returnText"), C::image("returntotown"), C::image("openworldmap")}),
        C::absent(C::any({title, leap}))});
    const auto opening = C::any({chooser, wheel, ruins, download});
    const auto after_leap = C::any({chooser, outside});
    graph.route("Entry", {"AtTitle", "OpenWheel", "Ruins", "Download"});
    graph.observe("AtTitle", title, {"QuickSelect", "Reset0"});
    graph.click("OpenWheel", C::all({wheel, C::absent(title)}), wheel, opening, {"Entry"});
    graph.click("Ruins", C::all({ruins, C::absent(title)}), ruins, opening, {"Entry"});
    if (allow_download)
        graph.click("Download", download, download, opening, {"Entry"});
    else {
        graph.observe("Download", download, {"DownloadDenied"});
        graph.recovery("DownloadDenied", "boot.download_permission_missing");
    }
    // 原函数的可见目标快路径会直接跳跃，不重置页签也不调整因果。
    graph.click("QuickSelect", C::all({title, target}), target, leap, {"QuickLeap"});
    graph.delay_after("QuickSelect", 2000);
    graph.click("QuickLeap", leap, leap, after_leap, {"Done", "Reset0"});
    graph.delay_after("QuickLeap", 2000);
    for (int i = 0; i < 10; ++i) {
        const auto name = "Reset" + std::to_string(i);
        graph.fixed_click(name, chooser, chooser, {105, 230},
            i == 9 ? J{"FindChapter"} : J{"Reset" + std::to_string(i + 1)});
        graph.delay_after(name, 500);
    }
    graph.route("FindChapter", {"Chapter", "NextTab", "ReopenWheel"});
    graph.click("Chapter", C::all({chooser, chapter}), chapter, chooser, {"SelectTarget", "Scroll0"});
    graph.click("NextTab", C::all({chooser, right, C::absent(chapter)}), right, chooser, {"FindChapter"});
    graph.click("ReopenWheel", C::all({wheel, C::absent(chapter)}), wheel, opening, {"FindChapter"});
    for (int i = 0; i < 3; ++i) {
        const auto name = "Scroll" + std::to_string(i);
        graph.swipe(name, C::all({title, C::absent(leap)}), title, {450, 1200, 450, 200},
            i == 2 ? J{"FindTarget"} : J{"Scroll" + std::to_string(i + 1)});
        graph.delay_after(name, 2000);
    }
    graph.route("FindTarget", {"SelectTarget", "FineScroll"});
    graph.click("SelectTarget", C::all({title, target}), target, leap, causality ? J{"Causality"} : J{"Leap"});
    graph.delay_after("SelectTarget", 1000);
    graph.swipe("FineScroll", C::all({title, C::absent(target), C::absent(leap)}), title,
        {50, 1200, 50, 1300}, {"FindTarget"});
    graph.delay_after("FineScroll", 1000);
    if (causality) {
        const auto child = graph.define_child("CausalitySettings", adjust_causality(*causality));
        graph.call_child("Causality", child, {"Leap"});
    }
    graph.click("Leap", leap, leap, after_leap, causality ? J{"Done", "Reselect", "Causality"} : J{"Done", "Reselect", "Leap"});
    graph.delay_after("Leap", 2000);
    graph.click("Reselect", C::all({title, target}), target, leap, causality ? J{"Causality"} : J{"Leap"});
    // 离开按钮不是充分条件。只有已执行跳跃路径之后的新帧正常游戏锚点才是终点。
    // 启动时的城市画面不能直达这里，未知加载帧也不能算业务完成。
    graph.observe("Done", outside, {"Terminal"});
    for (const auto *name : {"Entry", "AtTitle", "OpenWheel", "Ruins", "Download", "FindChapter",
        "NextTab", "ReopenWheel", "FindTarget", "FineScroll", "Leap", "Reselect"})
        graph.hit_limit(name, 32);
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
