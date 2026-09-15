#include "causality.hpp"
#include <cmath>

namespace wvd::games::navigation {
tasks::CompiledWorkflow adjust_causality(const CausalitySettings &settings) {
    using C = tasks::PipelineCompiler;
    using J = nlohmann::json;
    if (settings.symbol.empty() || settings.options.size() > 16) throw std::runtime_error("CAUSALITY_SETTINGS_INVALID");
    for (const auto &option : settings.options) {
        if (option.image.empty()) throw std::runtime_error("CAUSALITY_OPTION_INVALID");
        for (const auto value : option.rgb)
            if (!std::isfinite(value) || value < 0 || value > 8) throw std::runtime_error("CAUSALITY_COLOR_INVALID");
    }
    C graph("navigation.adjust_causality", std::chrono::seconds{300});
    const auto symbol = C::image(settings.symbol), menu = C::image("CSC"), leap = C::image("leap");
    graph.route("Entry", {"Opened", "Open"});
    graph.observe("Opened", symbol, {"DisableReference"});
    graph.click("Open", C::all({leap, menu, C::absent(symbol)}), menu, symbol, {"Opened"});
    const auto complete = settings.options.empty() ? "Close" : "EnableReference";
    for (const bool enable : {false, true}) {
        if (enable && settings.options.empty()) continue;
        const std::string prefix = enable ? "Enable" : "Disable";
        const J reference{{"mode", "causality_scroll"}, {"operation", "reference"}, {"image", settings.symbol}, {"direction", enable ? "down" : "up"}};
        auto unchanged = reference;
        unchanged["operation"] = "unchanged";
        // Reference只借新帧复制固定ROI；比较节点不更新基线，防止候选重查误把同页当滚到底。
        graph.observe(prefix + "Reference", reference, {prefix + "Choose0", prefix + "Skip0"});
        const auto options = enable ? settings.options : std::vector<CausalityOption>{{"didnottakethequest", {2, 0, 0}}};
        for (std::size_t i = 0; i < options.size(); ++i) {
            const auto suffix = std::to_string(i);
            auto marker = C::image(options[i].image);
            marker["preprocess"] = {{"operation", "multiply"}, {"rgb", options[i].rgb}};
            const J next = i + 1 == options.size() ? J{prefix + "Scroll"} : J{prefix + "Choose" + std::to_string(i + 1), prefix + "Skip" + std::to_string(i + 1)};
            graph.click(prefix + "Choose" + suffix, C::all({symbol, marker}), marker, symbol, next);
            graph.observe(prefix + "Skip" + suffix, C::all({symbol, C::absent(marker)}), next);
            if (enable) {
                graph.delay_after(prefix + "Choose" + suffix, 1000);
                graph.delay_after(prefix + "Skip" + suffix, 1000);
            }
            graph.hit_limit(prefix + "Choose" + suffix, 32); graph.hit_limit(prefix + "Skip" + suffix, 32);
        }
        graph.swipe(prefix + "Scroll", symbol, symbol, enable ? J{150, 400, 150, 500} : J{150, 500, 150, 400},
            {prefix + "Stable", prefix + "Changed"});
        graph.delay_after(prefix + "Scroll", 1000);
        graph.observe(prefix + "Stable", unchanged, enable ? J{"Close"} : J{complete});
        graph.observe(prefix + "Changed", C::absent(unchanged), {prefix + "Reference"});
        for (const auto *name : {"Reference", "Scroll", "Changed"}) graph.hit_limit(prefix + name, 32);
    }
    graph.back("Close", symbol, C::all({leap, C::absent(symbol)}), {"Terminal"});
    graph.delay_after("Close", 500);
    return graph.finish();
}
}
