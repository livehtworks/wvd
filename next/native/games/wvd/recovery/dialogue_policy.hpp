#pragma once
#include <stdexcept>
#include <string>
#include <string_view>
#include <array>
#include <span>

namespace wvd::games::recovery {
// 冻结在业务编译产物中的任务语义，不是全局GUI配置或可执行脚本。
enum class DialoguePolicy { Default, Jier, GoldenChest, Sandman, SteelTrial };
inline std::string dialogue_policy_name(DialoguePolicy policy) {
    switch (policy) {
    case DialoguePolicy::Default: return "";
    case DialoguePolicy::Jier: return "jier";
    case DialoguePolicy::GoldenChest: return "SSC-goldenchest";
    case DialoguePolicy::Sandman: return "sandman";
    case DialoguePolicy::SteelTrial: return "steeltrail";
    }
    throw std::runtime_error("DIALOGUE_POLICY_INVALID");
}
inline DialoguePolicy dialogue_policy_from_name(const std::string &name) {
    if (name.empty()) return DialoguePolicy::Default;
    if (name == "jier") return DialoguePolicy::Jier;
    if (name == "SSC-goldenchest") return DialoguePolicy::GoldenChest;
    if (name == "sandman") return DialoguePolicy::Sandman;
    if (name == "steeltrail") return DialoguePolicy::SteelTrial;
    throw std::runtime_error("DIALOGUE_POLICY_INVALID");
}
inline std::span<const std::string_view> special_dialogue_options(DialoguePolicy policy) {
    static constexpr std::array<std::string_view, 1> jier{"bounty/cuthimdown"};
    static constexpr std::array<std::string_view, 2> golden{"SSC/dotdotdot", "SSC/shadow"};
    static constexpr std::array<std::string_view, 3> sandman{"sandman/sandman_1", "sandman/sandman_2", "sandman/sandman_bondmate"};
    static constexpr std::array<std::string_view, 3> steel{"ready", "noneed", "quit"};
    switch (policy) {
    case DialoguePolicy::Default: return {};
    case DialoguePolicy::Jier: return jier;
    case DialoguePolicy::GoldenChest: return golden;
    case DialoguePolicy::Sandman: return sandman;
    case DialoguePolicy::SteelTrial: return steel;
    }
    throw std::runtime_error("DIALOGUE_POLICY_INVALID");
}
}
