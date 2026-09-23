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
    const bool invalid_device = raw.device_id != policy_.device_id;
    const bool invalid_viewport = raw.viewport_id.empty() || raw.size.width <= 0 ||
                                  raw.size.height <= 0 || raw.size.width > 16384 ||
                                  raw.size.height > 16384;
    // 游戏未启动时模拟器桌面通常是横屏。必须允许读取这一帧，状态机才能确认前台应用
    // 并进入启动游戏的恢复链；只有目标游戏已在前台时才执行严格的纵屏尺寸门禁。
    const bool invalid_shape = !policy_.observed_read_only_viewport &&
        raw.foreground_application == policy_.application_id &&
        (raw.viewport_id != policy_.viewport_id ||
         static_cast<std::int64_t>(raw.size.width) * policy_.recognition_size.height !=
             static_cast<std::int64_t>(raw.size.height) * policy_.recognition_size.width);
    if (invalid_device || invalid_viewport || invalid_shape)
        throw std::runtime_error(
            "CAPTURE_IDENTITY_INVALID:actual=" + raw.device_id + "/" + raw.viewport_id + "/" +
            std::to_string(raw.size.width) + "x" + std::to_string(raw.size.height) +
            ";expected=" + policy_.device_id + "/" + policy_.viewport_id + "/" +
            std::to_string(policy_.recognition_size.width) + "x" +
            std::to_string(policy_.recognition_size.height));
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
    frame_.capture_finished_at = raw.capture_finished_at.time_since_epoch().count() > 0
        ? raw.capture_finished_at : frame_.captured_at;
    if (frame_.captured_at > frame_.capture_finished_at ||
        frame_.capture_finished_at > std::chrono::steady_clock::now())
        throw std::runtime_error("CAPTURE_TIME_INVALID");
    frame_.display_rotation = raw.display_rotation;
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
                  {"application", application_},
                  {"capture_started_ns", std::chrono::duration_cast<std::chrono::nanoseconds>(frame_.captured_at.time_since_epoch()).count()},
                  {"capture_finished_ns", std::chrono::duration_cast<std::chrono::nanoseconds>(frame_.capture_finished_at.time_since_epoch()).count()},
                  {"capture_finish_reported", raw.capture_finished_at.time_since_epoch().count() > 0},
                  {"capture_duration_ms", std::chrono::duration_cast<std::chrono::milliseconds>(
                      frame_.capture_finished_at - frame_.captured_at).count()}});
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
           f.backend == frame_.backend && f.foreground_application == frame_.foreground_application &&
           f.capture_finished_at == frame_.capture_finished_at &&
           f.display_rotation == frame_.display_rotation &&
           f.captured_at.time_since_epoch().count() > 0 &&
           f.captured_at <= std::chrono::steady_clock::now();
}
bool InputGate::current_observation(const Observation &observation) const {
    std::lock_guard lock(mutex_);
    // 业务证据不是待发点击许可。识别耗时不撤销已经观察到的事实；仍拒绝旧代次/旧epoch。
    return !closed() && observation.outcome != RecognitionOutcome::Error &&
           same_frame(observation.basis) && application_ == policy_.application_id;
}
void InputGate::confirm_scene(const Observation &observation, const std::string &scene) {
    std::lock_guard lock(mutex_);
    if (closed() || observation.outcome != RecognitionOutcome::Hit ||
        !same_frame(observation.basis) || application_ != policy_.application_id ||
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
    if (!fresh_for_input(p.observation.basis))
        return "INPUT_EVIDENCE_EXPIRED";
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
    if (command.kind == ActionKind::Inactive) {
        // Maa 的 DoNothing 会调用 inactive 回调。它只是流程路由，不是设备输入，
        // 不需要动作意图，也不能下发给 ADB 或污染输入统计。
        if (closed())
            return false;
        events_.emit(generation_, "input.inactive", {});
        return true;
    }
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

namespace wvd::devices {
bool InputGate::fresh_for_input(const contracts::FrameIdentity &frame) const {
    if (policy_.max_frame_age.count() < 0) return false;
    const auto captured = frame.capture_finished_at.time_since_epoch().count() > 0
        ? frame.capture_finished_at : frame.captured_at;
    const auto now = std::chrono::steady_clock::now();
    return captured <= now && (policy_.max_frame_age.count() == 0 ||
                               now - captured <= policy_.max_frame_age);
}
bool InputGate::input_observation_current(const contracts::Observation &observation) const {
    std::lock_guard lock(mutex_);
    return !closed() && observation.outcome == contracts::RecognitionOutcome::Hit &&
        same_frame(observation.basis) && application_ == policy_.application_id &&
        fresh_for_input(observation.basis);
}
void InputGate::begin_submission(const std::string &node, std::int64_t task,
                                 const contracts::ActionIntent &intent,
                                 const std::string &condition) {
    std::lock_guard lock(mutex_);
    if (submission_) throw std::runtime_error("PREVIOUS_INPUT_NOT_OBSERVED");
    if (!event_scopes_.empty() &&
        (!event_scopes_.back().handler_nodes.contains(node) ||
         event_scopes_.back().owner != std::this_thread::get_id()))
        throw std::runtime_error("EVENT_INPUT_OWNER_INVALID");
    if (closed() || node.empty() || task <= 0 || !intent.id || condition.empty() ||
        intent.run_id != run_ || intent.generation != generation_ ||
        !same_frame(intent.observation.basis))
        throw std::runtime_error("SUBMISSION_IDENTITY_INVALID");
    submission_ = contracts::SubmittedInput{node, condition, task, intent.id, epoch_,
        intent.observation.basis};
    submission_interrupted_ = false;
}
void InputGate::finish_submission(bool accepted) {
    std::lock_guard lock(mutex_);
    if (!submission_ || submission_->state != contracts::SubmittedInput::State::Prepared)
        throw std::runtime_error("SUBMISSION_NOT_PREPARED");
    // 原许可只允许一次输入。成功返回但没有实际输入不能伪造 Submitted。
    const bool exactly_one = epoch_ == submission_->before.action_epoch + 1 && !in_flight_;
    submission_->state = accepted && exactly_one
        ? contracts::SubmittedInput::State::Submitted : contracts::SubmittedInput::State::Ambiguous;
    submission_->action_epoch = epoch_;
    submission_->submitted_at = std::chrono::steady_clock::now();
    if (accepted && !exactly_one)
        throw std::runtime_error("SUBMISSION_INPUT_COUNT_MISMATCH");
}
contracts::SubmittedInput InputGate::pending_submission(const std::string &node,
    std::int64_t task, const std::string &condition) const {
    std::lock_guard lock(mutex_);
    if (!submission_ || submission_->source_node != node || submission_->task_id != task ||
        submission_->expected_condition != condition ||
        submission_->state != contracts::SubmittedInput::State::Submitted ||
        (!submission_interrupted_ && submission_->action_epoch != epoch_))
        throw std::runtime_error("TRANSITION_RECEIPT_MISMATCH");
    return *submission_;
}
bool InputGate::confirm_transition(const contracts::SubmittedInput &receipt,
                                   const contracts::Observation &observed) {
    std::lock_guard lock(mutex_);
    if (closed() || !submission_ || submission_->intent_id != receipt.intent_id ||
        submission_->source_node != receipt.source_node || submission_->task_id != receipt.task_id ||
        submission_->expected_condition != receipt.expected_condition ||
        submission_->before != receipt.before || submission_->action_epoch != receipt.action_epoch ||
        submission_->submitted_at != receipt.submitted_at || receipt.state != contracts::SubmittedInput::State::Submitted ||
        submission_->state != contracts::SubmittedInput::State::Submitted ||
        observed.outcome != contracts::RecognitionOutcome::Hit || !same_frame(observed.basis) ||
        application_ != policy_.application_id ||
        observed.basis.connection_generation != receipt.before.connection_generation ||
        observed.basis.frame_id <= receipt.before.frame_id ||
        observed.basis.action_epoch != (submission_interrupted_ ? epoch_ : receipt.action_epoch) ||
        observed.basis.captured_at < (submission_interrupted_ ? resume_after_ : receipt.submitted_at))
        return false;
    // 只消费观察回执，绝不重建 scene_ / permit_，更不自动提交业务完成。
    submission_.reset();
    submission_interrupted_ = false;
    return true;
}
bool InputGate::has_pending_submission() const {
    std::lock_guard lock(mutex_);
    if (submission_) return true;
    for (const auto &scope : event_scopes_) if (scope.receipt) return true;
    return false;
}
std::uint64_t InputGate::begin_event_scope(std::int64_t parent_task, const std::string &event_id,
                                          const std::set<std::string> &handler_nodes) {
    std::lock_guard lock(mutex_);
    if (closed() || parent_task <= 0 || event_id.empty() || handler_nodes.empty() ||
        event_scopes_.size() >= 8 ||
        in_flight_ || !touches_.empty() || !keys_.empty())
        throw std::runtime_error("EVENT_SCOPE_NOT_QUIESCENT");
    if (submission_ && (submission_->task_id != parent_task ||
                        submission_->state != contracts::SubmittedInput::State::Submitted))
        throw std::runtime_error("EVENT_PARENT_RECEIPT_UNCERTAIN");
    for (const auto &scope : event_scopes_)
        if (scope.event_id == event_id) throw std::runtime_error("EVENT_RECURSION_INVALID");
    EventScope scope;
    scope.token = ++next_event_token_;
    scope.parent_task = parent_task;
    scope.event_id = event_id;
    scope.handler_nodes = handler_nodes;
    scope.entered_at = std::chrono::steady_clock::now();
    scope.owner = std::this_thread::get_id();
    scope.receipt = submission_;
    scope.receipt_interrupted = submission_interrupted_;
    for (const auto &[key, deadline] : observation_phases_) {
        (void)deadline;
        if (key.first != parent_task) continue;
        bool already_paused = false;
        for (const auto &active : event_scopes_) {
            if (active.paused_phases.contains(key)) { already_paused = true; break; }
        }
        if (!already_paused) scope.paused_phases.insert(key);
    }
    permit_.reset();
    submission_.reset();
    submission_interrupted_ = false;
    event_scopes_.push_back(std::move(scope));
    return next_event_token_;
}
void InputGate::end_event_scope(std::uint64_t token) {
    std::lock_guard lock(mutex_);
    if (closed() || event_scopes_.empty() || event_scopes_.back().token != token || submission_ ||
        in_flight_ || !touches_.empty() || !keys_.empty() || permit_ ||
        event_scopes_.back().owner != std::this_thread::get_id())
        throw std::runtime_error("EVENT_RETURN_UNSAFE");
    auto scope = std::move(event_scopes_.back());
    event_scopes_.pop_back();
    const auto now = std::chrono::steady_clock::now();
    for (const auto &key : scope.paused_phases)
        if (auto found = observation_phases_.find(key); found != observation_phases_.end())
            found->second += now - scope.entered_at;
    submission_ = std::move(scope.receipt);
    submission_interrupted_ = submission_.has_value();
    if (submission_interrupted_) resume_after_ = now;
}
std::optional<contracts::SubmittedInput> InputGate::settle_event_replan(
    std::int64_t parent_task, const contracts::Observation &guard) {
    std::lock_guard lock(mutex_);
    if (closed() || parent_task <= 0 || !event_scopes_.empty() || in_flight_ || permit_ ||
        !touches_.empty() || !keys_.empty() ||
        guard.outcome != contracts::RecognitionOutcome::Hit || !same_frame(guard.basis) ||
        application_ != policy_.application_id || guard.basis.generation != generation_ ||
        guard.basis.connection_generation != frame_.connection_generation)
        throw std::runtime_error("EVENT_REPLAN_EVIDENCE_INVALID");
    if (!submission_) return std::nullopt;
    if (submission_->task_id != parent_task ||
        submission_->state != contracts::SubmittedInput::State::Submitted ||
        !submission_interrupted_ || guard.basis.frame_id <= submission_->before.frame_id ||
        guard.basis.action_epoch != epoch_ || guard.basis.captured_at < resume_after_)
        throw std::runtime_error("EVENT_REPLAN_RECEIPT_UNCERTAIN");
    auto receipt = *submission_;
    submission_.reset();
    submission_interrupted_ = false;
    return receipt;
}
std::size_t InputGate::event_depth() const {
    std::lock_guard lock(mutex_);
    return event_scopes_.size();
}
} // namespace wvd::devices

namespace wvd::devices {
void InputGate::begin_observation_phase(std::int64_t task, const std::string &phase,
                                        std::chrono::milliseconds budget) {
    std::lock_guard lock(mutex_);
    if (closed() || task <= 0 || phase.empty() || phase.size() > 128 ||
        budget.count() <= 0 || budget > std::chrono::hours(24))
        throw std::runtime_error("OBSERVATION_PHASE_INVALID");
    const auto key = std::make_pair(task, phase);
    auto found = observation_phases_.find(key);
    if (found == observation_phases_.end()) {
        if (observation_phases_.size() >= 32)
            throw std::runtime_error("OBSERVATION_PHASE_DEPTH_EXCEEDED");
        found = observation_phases_.emplace(key, std::chrono::steady_clock::now() + budget).first;
    }
    if (std::chrono::steady_clock::now() >= found->second)
        throw std::runtime_error("OBSERVATION_PHASE_TIMEOUT:" + phase);
}
void InputGate::end_observation_phase(std::int64_t task, const std::string &phase) {
    std::lock_guard lock(mutex_);
    if (observation_phases_.erase({task, phase}) != 1)
        throw std::runtime_error("OBSERVATION_PHASE_NOT_ACTIVE");
}
std::chrono::steady_clock::time_point InputGate::observation_deadline() const {
    std::lock_guard lock(mutex_);
    auto deadline = std::chrono::steady_clock::time_point::max();
    for (const auto &[key, value] : observation_phases_) {
        bool paused = false;
        for (const auto &scope : event_scopes_)
            if (scope.paused_phases.contains(key)) { paused = true; break; }
        if (paused) continue;
        deadline = std::min(deadline, value);
    }
    return deadline;
}
} // namespace wvd::devices
