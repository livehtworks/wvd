#pragma once
#include <stdexcept>
#include <string>
#include <string_view>
#include <array>
#include <span>

namespace wvd::games::recovery {
// 冻结在业务编译产物中的任务语义，不是全局GUI配置或可执行脚本。
enum class DialoguePolicy {
    Default, Jier, GoldenChest, Sandman, SteelTrial, Fordraig,
    CaveOfSeparationOutbound, CaveOfSeparationEna, CaveOfSeparationRequest, CaveOfSeparationReturn
};
inline std::string dialogue_policy_name(DialoguePolicy policy) {
    switch (policy) {
    case DialoguePolicy::Default: return "";
    case DialoguePolicy::Jier: return "jier";
    case DialoguePolicy::GoldenChest: return "SSC-goldenchest";
    case DialoguePolicy::Sandman: return "sandman";
    case DialoguePolicy::SteelTrial: return "steeltrail";
    case DialoguePolicy::Fordraig: return "fordraig";
    case DialoguePolicy::CaveOfSeparationOutbound: return "CaveOfSeperation.outbound";
    case DialoguePolicy::CaveOfSeparationEna: return "CaveOfSeperation.ena";
    case DialoguePolicy::CaveOfSeparationRequest: return "CaveOfSeperation.request";
    case DialoguePolicy::CaveOfSeparationReturn: return "CaveOfSeperation.return";
    }
    throw std::runtime_error("DIALOGUE_POLICY_INVALID");
}
inline DialoguePolicy dialogue_policy_from_name(const std::string &name) {
    if (name.empty()) return DialoguePolicy::Default;
    if (name == "jier") return DialoguePolicy::Jier;
    if (name == "SSC-goldenchest") return DialoguePolicy::GoldenChest;
    if (name == "sandman") return DialoguePolicy::Sandman;
    if (name == "steeltrail") return DialoguePolicy::SteelTrial;
    if (name == "fordraig") return DialoguePolicy::Fordraig;
    if (name == "CaveOfSeperation.outbound") return DialoguePolicy::CaveOfSeparationOutbound;
    if (name == "CaveOfSeperation.ena") return DialoguePolicy::CaveOfSeparationEna;
    if (name == "CaveOfSeperation.request") return DialoguePolicy::CaveOfSeparationRequest;
    if (name == "CaveOfSeperation.return") return DialoguePolicy::CaveOfSeparationReturn;
    throw std::runtime_error("DIALOGUE_POLICY_INVALID");
}
inline std::span<const std::string_view> special_dialogue_options(DialoguePolicy policy) {
    static constexpr std::array<std::string_view, 1> jier{"bounty/cuthimdown"};
    static constexpr std::array<std::string_view, 2> golden{"SSC/dotdotdot", "SSC/shadow"};
    static constexpr std::array<std::string_view, 3> sandman{"sandman/sandman_1", "sandman/sandman_2", "sandman/sandman_bondmate"};
    static constexpr std::array<std::string_view, 3> steel{"ready", "noneed", "quit"};
    static constexpr std::array<std::string_view, 2> fordraig{"fordraig/thedagger", "fordraig/InsertTheDagger"};
    static constexpr std::array<std::string_view, 1> cos_outbound{"COS/takehimwithyou"};
    static constexpr std::array<std::string_view, 1> cos_return{"COS/requestwasfor"};
    switch (policy) {
    case DialoguePolicy::Default: return {};
    case DialoguePolicy::Jier: return jier;
    case DialoguePolicy::GoldenChest: return golden;
    case DialoguePolicy::Sandman: return sandman;
    case DialoguePolicy::SteelTrial: return steel;
    case DialoguePolicy::Fordraig: return fordraig;
    case DialoguePolicy::CaveOfSeparationOutbound:
    case DialoguePolicy::CaveOfSeparationEna:
    case DialoguePolicy::CaveOfSeparationRequest: return cos_outbound;
    case DialoguePolicy::CaveOfSeparationReturn: return cos_return;
    }
    throw std::runtime_error("DIALOGUE_POLICY_INVALID");
}
// 停点只供观察，不是对话选项或 UserStopped。
// 冻结视觉绑定将其作为嵌套导航的普通阻塞出口。
inline std::span<const std::string_view> dialogue_task_stops(DialoguePolicy policy) {
    static constexpr std::array<std::string_view, 1> ena{"COS/EnaTheAdventurer"};
    static constexpr std::array<std::string_view, 1> request{"COS/requestwasfor"};
    switch (policy) {
    case DialoguePolicy::CaveOfSeparationEna: return ena;
    case DialoguePolicy::CaveOfSeparationRequest: return request;
    case DialoguePolicy::Default:
    case DialoguePolicy::Jier:
    case DialoguePolicy::GoldenChest:
    case DialoguePolicy::Sandman:
    case DialoguePolicy::SteelTrial:
    case DialoguePolicy::Fordraig:
    case DialoguePolicy::CaveOfSeparationOutbound:
    case DialoguePolicy::CaveOfSeparationReturn: return {};
    }
    throw std::runtime_error("DIALOGUE_POLICY_INVALID");
}
}
