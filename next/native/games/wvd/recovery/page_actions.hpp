#pragma once
#include "games/wvd/tasks/pipeline_compiler.hpp"
#include "games/wvd/vision/boot_probes.hpp"
#include "games/wvd/vision/dialogue_probes.hpp"
#include "games/wvd/vision/inn_leave_probes.hpp"
#include <algorithm>
#include <optional>

namespace wvd::games::recovery::page_actions {
using J = nlohmann::json;
using C = tasks::PipelineCompiler;

// These are page actions within the existing compiler, not child executors.
// Callers retain their own result page, next edges, delays and input/call budgets.
struct DownloadRetry {
    J ready;
    int interval_ms{3000};
    unsigned max_submissions{};
};
inline void download(C &graph, const std::string &name, const J &button,
                     const J &post, J next, std::optional<DownloadRetry> retry) {
    graph.click(name, button, button, post, std::move(next));
    if (retry)
        graph.retry_menu_input(name, retry->ready, retry->interval_ms, retry->max_submissions);
}
inline void download(C &graph, const std::string &name, const J &button,
                     const J &post, J next) {
    download(graph, name, button, post, std::move(next), DownloadRetry{button});
}
inline void download_choices(C &graph, bool allowed, const std::string &en_name,
                             const std::string &zh_name, const J &post, J next,
                             const std::string &denied_name) {
    const auto en = vision::download_button_en(), zh = vision::download_button_zh_hant();
    if (allowed) {
        download(graph, en_name, en, post, next);
        download(graph, zh_name, zh, post, std::move(next));
    } else {
        graph.observe(en_name, en, {denied_name});
        graph.observe(zh_name, zh, {denied_name});
        graph.recovery(denied_name, "boot.download_permission_missing");
    }
}
// Public authoring resolves semantic resources before compilation. Match the
// complete existing click contract; names/IDs alone never authorize an action.
inline std::optional<J> author_download_prompt(const J &parameters) {
    if (parameters.at("operation") != "click" ||
        parameters.at("scene") != parameters.at("target") ||
        parameters.at("postcondition") != C::absent(parameters.at("target")) ||
        parameters.value("offset", J::array({0, 0})) != J::array({0, 0}))
        return std::nullopt;
    for (const auto &prompt : {vision::download_button_en(), vision::download_button_zh_hant()}) {
        auto resolved = prompt;
        resolved.erase("locale_only");
        if (parameters.at("target") == prompt || parameters.at("target") == resolved)
            return prompt;
    }
    return std::nullopt;
}
inline J author_download_retry_ready(const J &parameters, const J &prompt,
                                     const std::string &phase) {
    const auto same_page = C::all({parameters.at("scene"), C::absent(parameters.at("postcondition"))});
    auto blockers = vision::input_blockers(phase);
    blockers.erase(std::remove(blockers.begin(), blockers.end(), prompt), blockers.end());
    // Exclude only the handled prompt. Network, Pause, other languages and the
    // caller's phase blockers are still checked before any authorized retry.
    return C::all({same_page, C::absent(C::any(std::move(blockers)))});
}

inline void pause_sequence(C &graph, const std::string &prefix, const J &response,
                           const std::string &cleared, const std::string &frozen,
                           int consecutive_input_limit) {
    const J pause{{"mode", "pause"}};
    // Six confirmed attempts are the old freeze policy; Unknown delivery is
    // owned by FlowExecutor and can never advance to another press here.
    for (unsigned i = 0; i < 6; ++i) {
        const auto name = prefix + std::to_string(i);
        graph.fixed_click(name, pause, response, {450, 760},
            {cleared, i < 5 ? prefix + std::to_string(i + 1) : frozen});
        graph.hit_limit(name, consecutive_input_limit);
        graph.delay_after(name, 2000);
        graph.postcondition_budget(name, 10000);
    }
}
inline void continue_story(C &graph, const std::string &name, const J &post, J next) {
    graph.click(name, vision::ordinary_story_page(), vision::story_advance_arrow(), post, std::move(next));
}
inline void continue_inn_notice(C &graph, const std::string &name, const J &scene,
                                const J &post, J next) {
    graph.click(name, scene, vision::notice_advance_arrow(), post, std::move(next));
}
inline void close_character(C &graph, const std::string &name, const J &post, J next) {
    const auto character = vision::character_page();
    graph.fixed_click(name, character, post, {66, 1500}, std::move(next));
    graph.retry_menu_input(name, character, 3000);
}

// The two Auto strategies use the same cleanup, but keep their own popup
// predicate/order, postcondition and result. An X is never a scene by itself.
// The narrower UnownedDetail BACK in take_turn is intentionally not widened.
inline void auto_popup_cleanup(C &graph, const J &popup, const J &post, J next) {
    const J battle{{"mode", "combat_active"}};
    auto close = C::image("close");
    close["roi"] = {0, 600, 900, 1000};
    const auto ok = C::image("combat_skill_confirm");
    const auto detail = C::image("combat_skill_detail");
    graph.click("ClosePopup", C::all({battle, popup}), close, post, next);
    graph.retry_menu_input("ClosePopup", C::all({battle, popup, close}), 3000);
    graph.click("CancelPopup", C::all({battle, popup, C::absent(close)}), ok, post, next, {-280, 0});
    graph.retry_menu_input("CancelPopup", C::all({battle, popup, C::absent(close), ok}), 3000);
    graph.back("BackPopup", C::all({battle, detail, C::absent(close), C::absent(ok)}), post, std::move(next));
}
} // namespace wvd::games::recovery::page_actions
