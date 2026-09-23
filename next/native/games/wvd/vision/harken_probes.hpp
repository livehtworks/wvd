#pragma once
#include "games/wvd/tasks/pipeline_compiler.hpp"

namespace wvd::games::vision {
inline nlohmann::json harken_buff_menu() {
    using C = tasks::PipelineCompiler;
    auto none = C::image("harken_buff_none_zh_hant");
    none["roi"] = {330, 1300, 250, 140};
    nlohmann::json choices{none};
    for (const int y : {950, 1060, 1170}) {
        auto info = C::image("harken_buff_info_zh_hant");
        info["roi"] = {615, y, 82, 82};
        info["threshold"] = .78;
        choices.push_back(std::move(info));
    }
    return C::all(std::move(choices));
}

inline nlohmann::json harken_floor_menu() {
    using C = tasks::PipelineCompiler;
    auto move = C::image("harken_floor_move_zh_hant");
    move["roi"] = {20, 280, 130, 80};
    auto back = C::image("harken_floor_return_zh_hant");
    back["roi"] = {340, 980, 220, 110};
    return C::all({move, back});
}

inline nlohmann::json harken_return_button() {
    auto back = tasks::PipelineCompiler::image("harken_floor_return_zh_hant");
    back["roi"] = {340, 980, 220, 110};
    return back;
}

inline nlohmann::json outskirts_return_button() {
    auto back = tasks::PipelineCompiler::image("outskirts_return_to_town_zh_hant");
    back["roi"] = {555, 935, 211, 89};
    return back;
}
} // namespace wvd::games::vision
