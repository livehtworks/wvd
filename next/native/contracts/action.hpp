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
    // 仅限输入前的可选证据寿命。0 表示未设置全局墙钟限制；绝不用于结果/业务确认。
    // 设备、连接代次、epoch、最新帧和前台检查仍始终执行。
    std::chrono::milliseconds max_frame_age{0};
    // 仅用于 M3 无输入的系统观察；采用实测尺寸，不把横屏强制变成 WVD 竖屏。
    bool observed_read_only_viewport{false};
};
struct ActionIntent {
    std::uint64_t id{}, run_id{}, generation{};
    Observation observation;
    Command command;
    std::string required_scene, expected_postcondition;
    Box allowed_area;
};
// 一次输入的有限回执。Submitted 只证明底层接受，不证明页面改变或业务完成。
// 回执在当前会话内持有；不跨设备/连接/动作 epoch 复用，也不用于自动重放。
struct SubmittedInput {
    enum class State { Prepared, Submitted, Ambiguous };
    std::string source_node, expected_condition;
    std::int64_t task_id{};
    std::uint64_t intent_id{}, action_epoch{};
    FrameIdentity before;
    State state{State::Prepared};
    std::chrono::steady_clock::time_point submitted_at{};
};
struct InputCounts {
    std::uint64_t attempted{}, accepted{}, rejected{}, backend_called{}, cleanup_called{};
};
} // namespace wvd::contracts
