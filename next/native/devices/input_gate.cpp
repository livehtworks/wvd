#include "input_gate.hpp"
#include <algorithm>
#include <cmath>

namespace wvd::devices {
using namespace contracts;
namespace {
bool in_box(int x, int y, Box b) {
    return b.x >= 0 && b.y >= 0 && b.width > 0 && b.height > 0 && x >= b.x && y >= b.y &&
           static_cast<std::int64_t>(x) < static_cast<std::int64_t>(b.x) + b.width &&
           static_cast<std::int64_t>(y) < static_cast<std::int64_t>(b.y) + b.height;
}
bool positioned(ActionKind kind) {
    return kind == ActionKind::Click || kind == ActionKind::Swipe ||
           kind == ActionKind::TouchDown || kind == ActionKind::TouchMove;
}
} // namespace
InputGate::InputGate(DeviceBackend &backend, InputPolicy policy, std::uint64_t run,
                     std::uint64_t generation, storage::EventJournal &events)
    : backend_(backend), policy_(std::move(policy)), run_(run), generation_(generation),
      events_(events) {
    if (policy_.observed_read_only_viewport &&
        (!policy_.capabilities.empty() || !policy_.permissions.empty() ||
         !policy_.allowed_scenes.empty()))
        throw std::runtime_error("READ_ONLY_VIEWPORT_WITH_INPUT_POLICY");
}
InputGate::~InputGate() { backend_.disconnect(); }
bool InputGate::connect() {
    if (!backend_.offline() && !backend_.verified_access())
        throw std::runtime_error("REAL_DEVICE_NOT_ENABLED");
    std::lock_guard dispatch(dispatch_mutex_);
    if (closed())
        return false;
    return backend_.connect();
}
RawFrame InputGate::capture() {
    std::lock_guard dispatch(dispatch_mutex_);
    if (closed())
        throw std::runtime_error("INPUT_CLOSED");
    {
        std::lock_guard lock(mutex_);
        frame_ = {};
        scene_.clear();
        permit_.reset();
    }
    auto raw = backend_.capture();
    if (raw.device_id != policy_.device_id || raw.viewport_id.empty() || raw.size.width <= 0 ||
        raw.size.height <= 0 || raw.size.width > 16384 || raw.size.height > 16384 ||
        (!policy_.observed_read_only_viewport &&
         (raw.viewport_id != policy_.viewport_id ||
          static_cast<std::int64_t>(raw.size.width) * policy_.recognition_size.height !=
              static_cast<std::int64_t>(raw.size.height) * policy_.recognition_size.width)))
        throw std::runtime_error("CAPTURE_IDENTITY_INVALID");
    std::lock_guard lock(mutex_);
    frame_ = {raw.device_id,
              policy_.game_id,
              policy_.pack_revision,
              raw.viewport_id,
              generation_,
              ++next_frame_,
              epoch_,
              raw.size,
              policy_.observed_read_only_viewport ? raw.size : policy_.recognition_size,
              raw.captured_at.time_since_epoch().count() > 0 ? raw.captured_at
                                                             : std::chrono::steady_clock::now(),
              "BGR8"};
    application_ = raw.foreground_application;
    frame_.connection_generation = raw.connection_generation;
    frame_.backend = raw.backend;
    frame_.foreground_application = raw.foreground_application;
    scene_.clear();
    permit_.reset();
    events_.emit(generation_, "frame.captured",
                 {{"frame_id", frame_.frame_id},
                  {"epoch", epoch_},
                  {"backend", raw.backend},
                  {"width", raw.size.width},
                  {"height", raw.size.height},
                  {"application", application_}});
    return raw;
}
void InputGate::invalidate_frame() {
    std::lock_guard lock(mutex_);
    frame_ = {};
    permit_.reset();
    scene_.clear();
}
FrameIdentity InputGate::frame_identity() const {
    std::lock_guard lock(mutex_);
    return frame_;
}
bool InputGate::same_frame(const FrameIdentity &f) const {
    return f.device_id == policy_.device_id && f.game_id == policy_.game_id &&
           f.pack_revision == policy_.pack_revision && f.viewport_id == frame_.viewport_id &&
           f.generation == generation_ && f.frame_id == frame_.frame_id && f.frame_id > 0 &&
           f.action_epoch == epoch_ && f.raw_size == frame_.raw_size &&
           f.recognition_size == frame_.recognition_size && f.captured_at == frame_.captured_at &&
           f.color_format == "BGR8" && f.connection_generation == frame_.connection_generation &&
           std::chrono::steady_clock::now() - f.captured_at <= policy_.max_frame_age;
}
bool InputGate::current_observation(const Observation &observation) const {
    std::lock_guard lock(mutex_);
    // 只校验已观测证据，不建立场景许可。业务确认不能绕过相同的代次/epoch/帧龄边界。
    return !closed() && observation.outcome != RecognitionOutcome::Error &&
           same_frame(observation.basis) && application_ == policy_.application_id;
}
void InputGate::confirm_scene(const Observation &observation, const std::string &scene) {
    std::lock_guard lock(mutex_);
    if (observation.outcome != RecognitionOutcome::Hit || !same_frame(observation.basis) ||
        !policy_.allowed_scenes.contains(scene))
        throw std::runtime_error("SCENE_UNCONFIRMED");
    scene_ = scene;
}
bool InputGate::authorize(const ActionIntent &intent) {
    std::lock_guard lock(mutex_);
    // 最终拒绝必须由 execute 记录，不能在这里过滤掉输入尝试。
    if (permit_)
        throw std::runtime_error("INPUT_PERMIT_BUSY");
    permit_ = intent;
    return !closed();
}
void InputGate::revoke() {
    std::lock_guard lock(mutex_);
    permit_.reset();
}
std::string InputGate::reject_reason(const Command &c) const {
    if (closed())
        return "INPUT_CLOSED";
    if (policy_.observed_read_only_viewport)
        return "READ_ONLY_VIEWPORT";
    if (!permit_)
        return "NO_ACTION_INTENT";
    const auto &p = *permit_;
    if (p.run_id != run_ || p.generation != generation_ || !same_frame(p.observation.basis))
        return "STALE_ACTION_INTENT";
    if (p.observation.outcome != RecognitionOutcome::Hit)
        return "OBSERVATION_NOT_HIT";
    if (!p.observation.action_eligible)
        return "OBSERVATION_REQUIRES_CONFIRMATION";
    if (!(p.command == c))
        return "COMMAND_INTENT_MISMATCH";
    if (scene_ != p.required_scene || !policy_.allowed_scenes.contains(scene_))
        return "SCENE_UNCONFIRMED";
    if (application_ != policy_.application_id)
        return "APPLICATION_MISMATCH";
    if (!policy_.capabilities.contains(c.kind) || !policy_.permissions.contains(c.kind))
        return "INPUT_PERMISSION_DENIED";
    if (c.text.size() > 4096 || c.duration < 0 || c.duration > 60000 || c.contact < 0 ||
        c.contact > 15 || c.pressure < 0 || c.pressure > 1000 || c.key < 0 || c.key > 1024)
        return "INPUT_PARAMETERS_INVALID";
    if (p.expected_postcondition.empty())
        return "POSTCONDITION_REQUIRED";
    if (positioned(c.kind)) {
        Box screen{0, 0, policy_.recognition_size.width, policy_.recognition_size.height};
        if (!in_box(c.x, c.y, screen) || !in_box(c.x, c.y, p.allowed_area))
            return "TARGET_OUT_OF_BOUNDS";
        if (c.kind == ActionKind::Swipe &&
            (!in_box(c.x2, c.y2, screen) || !in_box(c.x2, c.y2, p.allowed_area)))
            return "TARGET_OUT_OF_BOUNDS";
    }
    if ((c.kind == ActionKind::StartApp || c.kind == ActionKind::StopApp) &&
        c.text != policy_.application_id)
        return "APPLICATION_NOT_ALLOWED";
    if (c.kind == ActionKind::Shell)
        return "SHELL_NOT_ALLOWED";
    if ((c.kind == ActionKind::TouchMove || c.kind == ActionKind::TouchUp) &&
        !touches_.contains(c.contact))
        return "TOUCH_NOT_HELD";
    if (c.kind == ActionKind::KeyUp && !keys_.contains(c.key))
        return "KEY_NOT_HELD";
    if (c.kind == ActionKind::TouchDown && touches_.contains(c.contact))
        return "TOUCH_ALREADY_HELD";
    if (c.kind == ActionKind::KeyDown && keys_.contains(c.key))
        return "KEY_ALREADY_HELD";
    return {};
}
Command InputGate::mapped(const Command &command) const {
    auto result = command;
    auto x = [&](int value) {
        return std::clamp(static_cast<int>(std::lround(double(value) * frame_.raw_size.width /
                                                       policy_.recognition_size.width)),
                          0, frame_.raw_size.width - 1);
    };
    auto y = [&](int value) {
        return std::clamp(static_cast<int>(std::lround(double(value) * frame_.raw_size.height /
                                                       policy_.recognition_size.height)),
                          0, frame_.raw_size.height - 1);
    };
    if (positioned(result.kind)) {
        result.x = x(result.x);
        result.y = y(result.y);
    }
    if (result.kind == ActionKind::Swipe) {
        result.x2 = x(result.x2);
        result.y2 = y(result.y2);
    }
    return result;
}
bool InputGate::execute(const Command &command) {
    {
        std::lock_guard lock(mutex_);
        ++counts_.attempted;
        events_.emit(generation_, "input.attempted", {{"kind", int(command.kind)}});
    }
    std::lock_guard dispatch(dispatch_mutex_);
    Command raw;
    bool context_valid = true;
    bool recheck = false;
    {
        std::lock_guard lock(mutex_);
        recheck = !closed() && permit_.has_value();
    }
    if (!backend_.offline() && recheck) {
        FrameIdentity observed;
        std::string application;
        {
            std::lock_guard lock(mutex_);
            observed = frame_;
            application = application_;
        }
        try {
            context_valid = backend_.context_matches(observed, application);
        } catch (...) {
            context_valid = false;
        }
    }
    {
        std::lock_guard lock(mutex_);
        // 固定 SDK 的 Scroll 先尝试 touch_move 定位光标，即使失败仍会调用 scroll。
        // 这次未获授权的移动必须拒绝，但不能消耗随后真正滚动命令的单次许可。
        if (!closed() && permit_ && permit_->command.kind == ActionKind::Scroll &&
            command.kind == ActionKind::TouchMove && !touches_.contains(command.contact)) {
            ++counts_.rejected;
            events_.emit(generation_, "input.rejected",
                         {{"reason", "SCROLL_CURSOR_MOVE_NOT_AUTHORIZED"}});
            return false;
        }
        auto reason = context_valid ? reject_reason(command) : "DEVICE_CONTEXT_CHANGED";
        if (!context_valid) {
            frame_ = {};
            scene_.clear();
        }
        permit_.reset();
        if (!reason.empty()) {
            ++counts_.rejected;
            events_.emit(generation_, "input.rejected", {{"reason", reason}});
            return false;
        }
        raw = mapped(command);
        // 基准点已通过准入；只收敛离散舍入的上界，并核对实际发送的全部绝对端点。
        const Box raw_bounds{0, 0, frame_.raw_size.width, frame_.raw_size.height};
        if (positioned(raw.kind) &&
            (!in_box(raw.x, raw.y, raw_bounds) ||
             (raw.kind == ActionKind::Swipe && !in_box(raw.x2, raw.y2, raw_bounds)))) {
            ++counts_.rejected;
            events_.emit(generation_, "input.rejected", {{"reason", "RAW_TARGET_OUT_OF_BOUNDS"}});
            return false;
        }
        ++counts_.accepted;
        ++counts_.backend_called;
        ++in_flight_;
        ++epoch_;
        if (command.kind == ActionKind::TouchDown)
            touches_.insert(command.contact);
        if (command.kind == ActionKind::KeyDown)
            keys_.insert(command.key);
        events_.emit(generation_, "input.backend_called",
                     {{"kind", int(command.kind)}, {"x", raw.x}, {"y", raw.y}, {"epoch", epoch_}});
    }
    bool ok = false;
    try {
        ok = backend_.execute(raw);
    } catch (...) {
        std::lock_guard lock(mutex_);
        --in_flight_;
        throw;
    }
    std::lock_guard lock(mutex_);
    --in_flight_;
    if (ok && command.kind == ActionKind::TouchUp)
        touches_.erase(command.contact);
    if (ok && command.kind == ActionKind::KeyUp)
        keys_.erase(command.key);
    events_.emit(generation_, "input.returned", {{"success", ok}});
    return ok;
}
void InputGate::close() {
    closed_.store(true);
    std::lock_guard lock(mutex_);
    permit_.reset();
}
bool InputGate::release_held() {
    std::lock_guard dispatch(dispatch_mutex_);
    std::vector<Command> releases;
    {
        std::lock_guard lock(mutex_);
        for (int contact : touches_) {
            Command c;
            c.kind = ActionKind::TouchUp;
            c.contact = contact;
            releases.push_back(c);
        }
        for (int key : keys_) {
            Command c;
            c.kind = ActionKind::KeyUp;
            c.key = key;
            releases.push_back(c);
        }
    }
    for (const auto &command : releases) {
        {
            std::lock_guard lock(mutex_);
            ++counts_.attempted;
            ++counts_.accepted;
            ++counts_.backend_called;
            ++counts_.cleanup_called;
            ++in_flight_;
            events_.emit(generation_, "input.cleanup_release", {{"kind", int(command.kind)}});
        }
        bool ok = false;
        try {
            ok = backend_.execute(command);
        } catch (...) {
        }
        std::lock_guard lock(mutex_);
        --in_flight_;
        if (ok) {
            if (command.kind == ActionKind::TouchUp)
                touches_.erase(command.contact);
            else
                keys_.erase(command.key);
        }
    }
    return quiescent();
}
bool InputGate::quiescent() const {
    std::lock_guard lock(mutex_);
    return !in_flight_ && touches_.empty() && keys_.empty();
}
InputCounts InputGate::counts() const {
    std::lock_guard lock(mutex_);
    return counts_;
}
} // namespace wvd::devices
