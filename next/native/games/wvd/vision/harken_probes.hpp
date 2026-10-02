#pragma once
#include "games/wvd/tasks/pipeline_compiler.hpp"

namespace wvd::games::vision {
inline nlohmann::json harken_buff_menu() {
    using C = tasks::PipelineCompiler;
    auto none = C::image("harken_buff_none_zh_hant");
    none["roi"] = {220, 1230, 460, 240};
    nlohmann::json choices = nlohmann::json::array({none});
    for (const int y : {950, 1060, 1170}) {
        auto info = C::image("harken_buff_info_zh_hant");
        // 三个选项保留独立行范围，扩大左右/动画偏移容差，不以同一图标重复证明三行。
        info["roi"] = {540, y - 25, 220, 110};
        info["threshold"] = .78;
        choices.push_back(std::move(info));
    }
    return C::all(std::move(choices));
}

inline nlohmann::json harken_floor_menu() {
    using C = tasks::PipelineCompiler;
    auto move = C::image("harken_floor_move_zh_hant");
    move["roi"] = {0, 230, 250, 170};
    auto back = C::image("harken_floor_return_zh_hant");
    back["roi"] = {200, 920, 500, 250};
    return C::all({move, back});
}

inline nlohmann::json harken_return_button() {
    auto back = tasks::PipelineCompiler::image("harken_floor_return_zh_hant");
    back["roi"] = {200, 920, 500, 250};
    return back;
}

inline nlohmann::json outskirts_return_button() {
    auto back = tasks::PipelineCompiler::image("outskirts_return_to_town_zh_hant");
    back["roi"] = {400, 900, 480, 200};
    return back;
}
} // namespace wvd::games::vision
