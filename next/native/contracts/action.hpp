#pragma once
#include "recognition.hpp"
#include <set>

namespace wvd::contracts {
enum class ActionKind {
    Click,
    Swipe,
    TouchDown,
    TouchMove,
    TouchUp,
    ClickKey,
    Text,
    KeyDown,
    KeyUp,
    Scroll,
    RelativeMove,
    StartApp,
    StopApp,
    Shell,
    Inactive
};
struct Command {
    ActionKind kind{ActionKind::Click};
    int x{}, y{}, x2{}, y2{}, duration{}, contact{}, pressure{}, key{};
    std::string text;
    bool operator==(const Command &) const = default;
};
struct InputPolicy {
    std::string device_id, game_id, application_id, pack_revision, viewport_id;
    Size recognition_size{900, 1600};
    std::set<ActionKind> capabilities, permissions;
    std::set<std::string> allowed_scenes;
    std::chrono::milliseconds max_frame_age{2000};
};
struct ActionIntent {
    std::uint64_t id{}, run_id{}, generation{};
    Observation observation;
    Command command;
    std::string required_scene, expected_postcondition;
    Box allowed_area;
};
struct InputCounts {
    std::uint64_t attempted{}, accepted{}, rejected{}, backend_called{}, cleanup_called{};
};
} // namespace wvd::contracts
