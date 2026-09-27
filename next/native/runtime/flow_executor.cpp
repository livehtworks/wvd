#include "flow_executor.hpp"
#include "platform/execution_timing.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace wvd::runtime {
namespace {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
void require(bool condition, const char *code) {
    if (!condition) throw std::runtime_error(code);
}
bool same_device(const contracts::FrameIdentity &a, const contracts::FrameIdentity &b) {
    return a.device_id == b.device_id && a.game_id == b.game_id &&
        a.pack_revision == b.pack_revision && a.generation == b.generation &&
        a.connection_generation == b.connection_generation && a.viewport_id == b.viewport_id &&
        a.raw_size == b.raw_size && a.recognition_size == b.recognition_size &&
        a.display_rotation == b.display_rotation;
}
void require_hit(const contracts::Observation &value, const char *code) {
    if (value.outcome == contracts::RecognitionOutcome::Error)
        throw std::runtime_error(value.error_code.empty() ? "RECOGNITION_ERROR" : value.error_code);
    require(value.outcome == contracts::RecognitionOutcome::Hit, code);
}
} // namespace

FlowExecutor::FlowExecutor(const workflow::FlowProgram &program, FlowPorts &ports,
                           std::chrono::milliseconds total_budget,
                           contracts::ObservationRecoveryPolicy observation_policy)
    : program_(program), ports_(ports), deadline_(Clock::now() + total_budget),
      accounted_at_(Clock::now()), observation_policy_(observation_policy) {
    observation_policy_.validate();
    program_.validate();
    require(total_budget.count() > 0 && total_budget <= std::chrono::hours{24},
            "FLOW_TOTAL_BUDGET_INVALID");
    const auto &root = program_.definitions.at(program_.root_definition);
    Frame frame;
    frame.definition = root.id;
    frame.current = root.entry;
    frame.invoked_at = frame.entered_at = Clock::now();
    if (root.cumulative_budget) frame.invocation_deadline = frame.invoked_at + *root.cumulative_budget;
    frame.hits[root.entry] = 1;
    stack_.push_back(std::move(frame));
}
const workflow::Step &FlowExecutor::step() const {
    const auto &frame = stack_.back();
    return program_.definitions.at(frame.definition).steps.at(frame.current);
}
const std::string &FlowExecutor::current_source_path() const {
    static const std::string empty;
    return stack_.empty() ? empty : step().source_path;
}
std::string FlowExecutor::current_step_id() const {
    return stack_.empty() ? std::string{} : stack_.back().current;
}
nlohmann::json FlowExecutor::progress_snapshot() const {
    nlohmann::json call_stack = nlohmann::json::array();
    nlohmann::json pending = nlohmann::json::array();
    nlohmann::json active_event = nullptr;
    nlohmann::json suspended = nullptr;
    for (std::size_t i = 0; i < stack_.size(); ++i) {
        const auto &frame = stack_[i];
        const auto &node = program_.definitions.at(frame.definition).steps.at(frame.current);
        call_stack.push_back({{"definition", frame.definition}, {"node_id", frame.current},
                              {"source_path", nlohmann::json::parse(node.source_path, nullptr, false)},
                              {"phase", program_.definitions.at(frame.definition).checks
                                  ? program_.definitions.at(frame.definition).checks->phase : node.check_group}});
        if (frame.pending) {
            const auto &input = *frame.pending;
            pending.push_back({{"source_path", input.source_path},
                               {"basis_frame", input.before.frame_id},
                               {"basis_epoch", input.before.action_epoch},
                               {"action_epoch", input.action_epoch},
                               {"attempts", input.attempts},
                               {"delivery_unknown", input.delivery_unknown}});
        }
        if (frame.event) {
            const auto &rule = frame.event->rule;
            const auto &owner = stack_.at(frame.event->owner);
            const auto &source = program_.definitions.at(owner.definition).steps.at(owner.current);
            suspended = {{"pipeline_node", owner.current},
                         {"node_path", nlohmann::json::parse(source.source_path, nullptr, false)}};
            active_event = {{"event_id", rule.id}, {"source_node", owner.current},
                            {"handler_entry", frame.current}, {"depth", i + 1},
                            {"resume", {{"mode", rule.resume == workflow::ResumeMode::Replan ? "replan" : "continue"}}}};
        }
    }
    return {{"step_id", current_step_id()}, {"source_path", current_source_path()},
            {"call_stack", std::move(call_stack)}, {"pending_inputs", std::move(pending)},
            {"active_event", std::move(active_event)}, {"suspended_step", std::move(suspended)},
            {"check_group", stack_.empty() ? "business" : step().check_group},
            {"polling", !stack_.empty() && std::holds_alternative<workflow::Poll>(step().data)},
            {"phase", stack_.empty() ? nlohmann::json(nullptr) :
                program_.definitions.at(stack_.back().definition).checks
                ? nlohmann::json(program_.definitions.at(stack_.back().definition).checks->phase) : nlohmann::json(step().check_group)},
            {"current_block", stack_.empty() ? nlohmann::json(nullptr) : nlohmann::json(stack_.back().definition)},
            {"wait_state", stack_.empty() ? "none" : stack_.back().pending ? "input_result" :
                stack_.back().known_wait ? "normal" : stack_.back().diagnostic_checked ? "unknown" :
                std::holds_alternative<workflow::Wait>(step().data) ? "explicit" : "none"},
            {"return_targets", stack_.empty() ? nlohmann::json(nullptr) :
                stack_.back().returned_targets ? nlohmann::json(*stack_.back().returned_targets) : nlohmann::json(nullptr)},
            {"last_diagnostic", last_diagnostic_},
            {"observation_recovery", observation_recovery_snapshot()}};
}
bool FlowExecutor::has_unresolved_input() const {
    return std::any_of(stack_.begin(), stack_.end(),
                       [](const Frame &f) { return f.pending.has_value(); });
}
void FlowExecutor::report_input_result(const PendingInput &input, const char *outcome,
    std::uint64_t observed_frame, const std::string &reason) noexcept {
    try {
        const auto now = Clock::now();
        const auto ns = [](Clock::duration value) {
            return std::chrono::duration_cast<std::chrono::nanoseconds>(value).count();
        };
        ports_.input_result({{"source_path", input.source_path}, {"outcome", outcome},
            {"reason", reason}, {"action_epoch", input.action_epoch},
            {"basis_frame", input.before.frame_id}, {"observed_frame", observed_frame},
            {"attempts", input.attempts}, {"delivery_unknown", input.delivery_unknown},
            {"result_wait_ns", ns(now - input.result_started_at)},
            {"since_last_submission_ns", ns(now - input.submitted_at)},
            {"event_and_read_pause_ns", ns(input.event_pause)}});
    } catch (...) { /* 诊断失败不能重放输入或改变业务终态。 */ }
}
void FlowExecutor::report_unconfirmed_inputs(const std::string &reason) noexcept {
    for (const auto &frame : stack_)
        if (frame.pending) report_input_result(*frame.pending, "unconfirmed", 0, reason);
}
bool FlowExecutor::is_operation(const std::string &binding, const std::string &operation) const {
    if (stack_.empty()) return false;
    const auto *value = std::get_if<workflow::RegisteredOperation>(&step().data);
    return value && value->binding == binding && value->parameters.value("operation", "") == operation;
}
TickResult FlowExecutor::progress() const {
    return {TickState::Progress, {}, {}, current_source_path()};
}
void FlowExecutor::invalidate_observation() {
    observation_cycle_.reset();
    for (auto &frame : stack_) { frame.selected_frame.reset(); frame.selected_observation.reset(); }
}
contracts::FrameEnvelope FlowExecutor::observation_frame() {
    if (observation_cycle_ && !ports_.reusable(observation_cycle_->frame.identity))
        invalidate_observation();
    if (!observation_cycle_) {
        // 在读调用前结算原区间，故障后可精确暂停本次读耗时，不重复叠加事件暂停。
        account_event_time();
        ports_.observation_window(read_recovery_ ? std::min(deadline_,
            read_recovery_->started + observation_policy_.outage_limit) : deadline_);
        try { observation_cycle_ = ObservationCycle{ports_.capture(), std::nullopt}; }
        catch (const contracts::ObservationUnavailable &error) {
            begin_observation_recovery(error.fault());
            throw;
        }
    } else platform::timing::count(platform::timing::Counter::FrameReuse);
    return observation_cycle_->frame;
}
TickResult FlowExecutor::waiting(std::chrono::milliseconds delay) {
    invalidate_observation(); // 下一轮轮询必须是新帧，不把 NoHit 固定在旧图上。
    return {TickState::Waiting, std::min(Clock::now() + delay, deadline_), {}, current_source_path()};
}
TickResult FlowExecutor::fail(std::string code) {
    if (read_recovery_) {
        read_recovery_->awaiting_input_validation = false;
        finish_observation_recovery("failed");
    }
    terminal_ = {TickState::Failed, {}, std::move(code), current_source_path()};
    return terminal_;
}
TickResult FlowExecutor::business_fail(std::string reason, std::string source) {
    terminal_ = {TickState::BusinessFailed, {}, std::move(reason), std::move(source)};
    return terminal_;
}
TickResult FlowExecutor::return_business_failure(const std::string &reason) {
    if (has_unresolved_input()) return blocked("CHILD_INPUT_UNRESOLVED:" + reason);
    const auto source = current_source_path();
    account_event_time();
    while (stack_.size() > 1) {
        const auto ended = std::move(stack_.back());
        stack_.pop_back();
        if (ended.event) {
            terminal_ = {TickState::Failed, {}, "EVENT_HANDLER_BUSINESS_FAILURE:" + reason, source};
            return terminal_;
        }
        auto &parent = stack_.back();
        const auto &caller = step();
        if (caller.handles_business_failure)
            return route_error(parent, caller, "BUSINESS_FAILURE:" + reason);
    }
    return business_fail(reason, source);
}
TickResult FlowExecutor::blocked(std::string code) {
    terminal_ = {TickState::ExternalBlocked, {}, std::move(code), current_source_path()};
    return terminal_;
}
TickResult FlowExecutor::route_error(Frame &frame, const workflow::Step &current, std::string code) {
    // 有界等待耗尽前再给已知现场一次诊断机会；Error/取消/送达未知不走盲目恢复。
    if (!frame.error_pending && (code == "FLOW_STAGE_TIMEOUT" ||
        code == "FLOW_HIT_LIMIT_EXHAUSTED" || code == "EVENT_RESUME_UNCONFIRMED")) {
        const auto image = observation_frame();
        if (auto event = check_unexpected(frame, current, image, code, true)) return *event;
    }
    if (frame.pending && !frame.pending->delivery_unknown && frame.pending->attempts > 1 &&
        (code == "FLOW_STAGE_TIMEOUT" || code == "AWAIT_RESULT_TIMEOUT")) {
        const auto &source = program_.definitions.at(frame.definition).steps.at(frame.pending->submitted_step);
        const auto &input = std::get<workflow::Input>(source.data);
        if (input.retry && input.effect_binding.empty()) {
            const auto image = observation_frame();
            const auto ready = ports_.recognize(image, input.retry->ready);
            const auto scene = ports_.recognize(image, input.scene);
            if (ready.outcome == contracts::RecognitionOutcome::Error || scene.outcome == contracts::RecognitionOutcome::Error)
                return fail("RETRY_EXHAUSTION_RECOGNITION_ERROR");
            if (ready.outcome == contracts::RecognitionOutcome::Hit && scene.outcome == contracts::RecognitionOutcome::Hit) {
                // 明确可重复的操作仍停留原页，结束本次尝试交给业务失败边。
                // 不把它记为操作成功；付款/送达未知没有此声明，仍禁止清回执重发。
                last_diagnostic_ = {{"reason", "repeatable_input_no_progress"},
                    {"source_path", source.source_path}, {"attempts", frame.pending->attempts},
                    {"frame_id", image.identity.frame_id}};
                report_input_result(*frame.pending, "no_progress", image.identity.frame_id, code);
                frame.pending.reset();
            }
        }
    }
    if (frame.pending) return blocked("INPUT_RESULT_UNCONFIRMED:" + code);
    if (frame.error_pending || current.on_error.empty()) return fail(std::move(code));
    frame.next_pending = frame.error_pending = true;
    frame.input_selection.reset();
    frame.selected_frame.reset();
    frame.selected_observation.reset();
    frame.delay_until.reset();
    frame.entered_at = Clock::now();
    return progress();
}
TickResult FlowExecutor::tick() {
    if (terminal_.state != TickState::Progress) return terminal_;
    if (ports_.cancelled()) {
        if (read_recovery_) {
            read_recovery_->awaiting_input_validation = false;
            finish_observation_recovery("cancelled");
        }
        terminal_ = {TickState::Cancelled, {}, "CANCELLED", current_source_path()};
        return terminal_;
    }
    if (Clock::now() >= deadline_) return fail("FLOW_TOTAL_DEADLINE");
    try {
        require(!stack_.empty() && stack_.size() <= 8, "FLOW_STACK_INVALID");
        account_event_time();
        // 恢复窗口只做只读采集；不重执行当前步骤，也不推进或清空父回执。
        if (read_recovery_) {
            if (Clock::now() >= read_recovery_->started + observation_policy_.outage_limit)
                return fail("OBSERVATION_RECOVERY_EXHAUSTED:" + read_recovery_->last.code);
            if (read_recovery_->suspended) return retry_observation();
        }
        ports_.observation_window(read_recovery_ ? std::min(deadline_,
            read_recovery_->started + observation_policy_.outage_limit) : deadline_);
        for (const auto &frame : stack_) {
            if (frame.invocation_deadline && Clock::now() >= *frame.invocation_deadline)
                return fail("FLOW_INVOCATION_TIMEOUT:" + frame.definition);
            for (const auto &[name, deadline] : frame.phase_deadlines)
                if (Clock::now() >= deadline) return fail("OBSERVATION_PHASE_TIMEOUT:" + name);
        }
        auto &frame = stack_.back();
        const auto &current = step();
        // 无等待的长控制链也要定期观察覆盖层；这不是延长任何输入证据期限。
        const auto rules = effective_events(current);
        if (observation_cycle_ && Clock::now() - observation_cycle_->frame.identity.capture_finished_at >= 100ms &&
            std::any_of(rules.begin(), rules.end(), [](const ScopedEvent &event) {
                return event.rule.category == workflow::EventClass::Overlay;
            })) {
            invalidate_observation();
            const auto image = observation_frame();
            if (auto event = check_events(frame, current, image, workflow::EventClass::Overlay)) return *event;
        }
        if (frame.resume) return resume_event(frame, current);
        return frame.next_pending ? select_next(frame, current) : execute_step(frame, current);
    } catch (const contracts::ObservationUnavailable &) {
        // 只接 observation_frame() 已经登记的只读失败。没有通用 catch/continue。
        if (!read_recovery_) return fail("UNCLASSIFIED_OBSERVATION_FAULT");
        if (ports_.cancelled()) {
            terminal_ = {TickState::Cancelled, {}, "CANCELLED", current_source_path()};
            return terminal_;
        }
        return observation_retry_wait();
    } catch (const std::exception &error) { return fail(error.what()); }
    catch (...) { return fail("FLOW_UNEXPECTED_EXCEPTION"); }
}
void FlowExecutor::clear_unexpected(Frame &frame) {
    if (last_diagnostic_.is_object() &&
        last_diagnostic_.value("source_path", "") == current_source_path())
        last_diagnostic_["outcome"] = "business_resumed";
    frame.no_progress_since.reset();
    frame.next_diagnostic_poll = {};
    frame.diagnostic_checked = false;
}
void FlowExecutor::advance(Frame &frame, bool observed_progress) {
    if (observed_progress) clear_unexpected(frame);
    frame.input_selection.reset();
    frame.selected_frame.reset();
    frame.selected_observation.reset();
    frame.poll_until.reset();
    frame.next_pending = true;
    frame.error_pending = false;
    const auto delay = program_.definitions.at(frame.definition).steps.at(frame.current).delay_after;
    frame.delay_until = delay.count() ? std::optional{Clock::now() + delay} : std::nullopt;
}
TickResult FlowExecutor::reconsider_unsubmitted_input(Frame &frame) {
    // 只撤回分支选择，不撤回输入：转场可使已选按钮在提交前消失。
    // 回到原选择点保留期限；送达未知或已有回执绝不能借此重发。
    if (frame.input_selection && !frame.pending) {
        const auto origin = std::move(*frame.input_selection);
        frame.input_selection.reset();
        --frame.hits.at(frame.current); // 尚未执行的选择不消费按钮命中次数。
        frame.current = origin.predecessor;
        frame.entered_at = origin.entered_at;
        frame.next_pending = true;
        frame.error_pending = origin.error_pending;
    }
    return waiting(50ms);
}
void FlowExecutor::record_known_scene(Frame &frame, const contracts::Observation &observed) {
    ports_.scene_observed(observed);
    // 连续未知窗口只属于当前不认识的页面，不能累计整轮正常转场。
    // 不清业务循环或输入计数，不改变pending与原目标。
    for (const auto &[id, step] : program_.definitions.at(frame.definition).steps)
        if (step.unexpected_only || std::holds_alternative<workflow::Poll>(step.data))
            frame.hits.erase(id);
}
TickResult FlowExecutor::select_next(Frame &frame, const workflow::Step &current) {
    if (frame.delay_until && Clock::now() < *frame.delay_until) {
        if (auto event = poll_wait_events(frame, current)) return *event;
        return waiting(25ms);
    }
    frame.delay_until.reset();
    const auto candidates = frame.error_pending ? current.on_error
        : frame.returned_targets.value_or(current.next);
    if (candidates.empty()) return fail(frame.error_pending ?
        "FLOW_ERROR_ROUTE_MISSING" : "FLOW_SUCCESSOR_MISSING");
    if (frame.event_exits.empty() && Clock::now() - frame.entered_at >= current.time_limit)
        return route_error(frame, current, "FLOW_STAGE_TIMEOUT");
    const auto &definition = program_.definitions.at(frame.definition);
    const auto can_enter = [&](const workflow::Step &candidate) {
        const auto found = frame.hits.find(candidate.id);
        return candidate.max_hit == 0 || found == frame.hits.end() || found->second < candidate.max_hit;
    };
    if (std::none_of(candidates.begin(), candidates.end(), [&](const std::string &id) {
        return can_enter(definition.steps.at(id));
    })) return route_error(frame, current, "FLOW_HIT_LIMIT_EXHAUSTED");

    const auto rules = effective_events(current);
    const bool proactive = !frame.event_exits.empty() ||
        std::any_of(rules.begin(), rules.end(), [](const ScopedEvent &event) {
            return event.rule.category == workflow::EventClass::Overlay;
        });
    std::optional<contracts::FrameEnvelope> image;
    bool overlay_checked = false;
    const auto acquire_image = [&]() -> const contracts::FrameEnvelope & {
        if (!image) image = observation_frame();
        return *image;
    };
    const auto check_overlay = [&]() -> std::optional<TickResult> {
        if (!proactive || overlay_checked) return std::nullopt;
        overlay_checked = true;
        return check_events(frame, current, acquire_image(), workflow::EventClass::Overlay);
    };
    const auto enter = [&](const workflow::Step &candidate,
                           std::optional<contracts::Observation> observed,
                           bool is_poll) -> TickResult {
        // 选择 Poll 只决定何时重看，不能重签原阶段期限或清空不匹配历史。
        const auto previous = frame.current;
        const auto entered = frame.entered_at;
        const bool was_error = frame.error_pending;
        if (std::holds_alternative<workflow::Input>(candidate.data) && candidates.size() > 1)
            frame.input_selection = Frame::InputSelection{previous, entered, was_error};
        else frame.input_selection.reset();
        frame.current = candidate.id;
        if (!is_poll) frame.known_wait = false;
        if (candidate.max_hit > 0) ++frame.hits[candidate.id];
        if (!is_poll) frame.entered_at = Clock::now();
        frame.next_pending = frame.error_pending = false;
        frame.returned_targets.reset();
        frame.selected_frame = image;
        frame.selected_observation = std::move(observed);
        frame.poll_until = is_poll ? std::optional{Clock::now() +
            std::get<workflow::Poll>(candidate.data).interval} : std::nullopt;
        return progress();
    };

    // 业务标量先按原候选优先级求值，确实要读像素时才取帧。
    // 显式 Overlay 仍保留抢占权；不是将所有自动异常处理又提前全扫。
    const workflow::Step *polling = nullptr;
    for (const auto &id : candidates) {
        const auto &candidate = definition.steps.at(id);
        if (candidate.unexpected_only) continue;
        if (std::holds_alternative<workflow::Poll>(candidate.data)) {
            require(!polling || polling == &candidate, "FLOW_MULTIPLE_POLL_FALLBACKS");
            polling = &candidate;
            continue;
        }
        if (!can_enter(candidate)) continue;
        if (auto event = check_overlay()) return *event;
        if (candidate.business_guard) {
            const auto predicate = ports_.operate("BusinessPredicate", *candidate.business_guard,
                std::nullopt, std::nullopt, candidate.source_path);
            if (predicate.state == OperationState::Waiting) continue;
            if (predicate.state != OperationState::Done)
                return fail("BUSINESS_GUARD_ERROR:" + predicate.detail);
        }
        std::optional<contracts::Observation> observed;
        if (candidate.guard) {
            observed = ports_.recognize(acquire_image(), *candidate.guard);
            if (observed->outcome == contracts::RecognitionOutcome::Error)
                return fail(observed->error_code.empty() ? "RECOGNITION_ERROR" : observed->error_code);
            if (observed->outcome == contracts::RecognitionOutcome::NoHit) continue;
        }
        if (candidate.marks_known_scene) {
            require(observed && observed->outcome == contracts::RecognitionOutcome::Hit,
                    "KNOWN_SCENE_EVIDENCE_MISSING");
            record_known_scene(frame, *observed);
        }
        // 无条件控制跳转及唯一 Input->Await 不为选路额外采图。
        // Input、Observe、Await、业务确认在执行时仍走各自的真实观察入口。
        // 只有原调用帧重新选中了有视觉证据的另一动作，才证明原未发送动作已过时。
        // 事件处理器或纯路由经过不能刷新输入前读取故障窗口。
        if (read_recovery_ && read_recovery_->awaiting_input_validation && !frame.pending &&
            read_recovery_->stack_depth == stack_.size() && observed &&
            candidate.source_path != read_recovery_->source_path) {
            read_recovery_->awaiting_input_validation = false;
            finish_observation_recovery("action_no_longer_needed");
        }
        return enter(candidate, std::move(observed), false);
    }

    if (auto event = check_overlay()) return *event;
    const auto &pixels = acquire_image();
    bool known_wait = false;
    if (polling) {
        const auto &policy = std::get<workflow::Poll>(polling->data);
        if (policy.ongoing && can_enter(*polling)) {
            const auto ongoing = ports_.recognize(pixels, *policy.ongoing);
            if (ongoing.outcome == contracts::RecognitionOutcome::Error)
                return fail(ongoing.error_code.empty() ? "POLL_ONGOING_RECOGNITION_ERROR" : ongoing.error_code);
            known_wait = ongoing.outcome == contracts::RecognitionOutcome::Hit;
            if (known_wait) {
                clear_unexpected(frame); // 已知等待，不是未知，也不代表目标完成。
                record_known_scene(frame, ongoing);
                if (policy.progress) {
                    const auto changed = ports_.recognize(pixels, *policy.progress);
                    if (changed.outcome == contracts::RecognitionOutcome::Error)
                        return fail("POLL_PROGRESS_RECOGNITION_ERROR");
                    if (changed.outcome == contracts::RecognitionOutcome::Hit) {
                        frame.entered_at = Clock::now();
                        last_diagnostic_ = {{"reason", "business_progress_observed"},
                            {"source_path", current.source_path}, {"frame_id", pixels.identity.frame_id}};
                    }
                }
            }
        }
    }
    frame.known_wait = known_wait;
    if (!known_wait) {
        if (auto event = check_unexpected(frame, current, pixels, "successor_not_confirmed",
            polling && !can_enter(*polling))) return *event;
        // 未知/冻结诊断只能在正常候选与已登记事件都没解释现场之后进入。
        for (const auto &id : candidates) {
            const auto &candidate = definition.steps.at(id);
            if (!candidate.unexpected_only || !can_enter(candidate) || !frame.diagnostic_checked) continue;
            require(candidate.guard.has_value(), "UNEXPECTED_GUARD_MISSING");
            auto observed = ports_.recognize(pixels, *candidate.guard);
            if (observed.outcome == contracts::RecognitionOutcome::Error)
                return fail(observed.error_code.empty() ? "RECOGNITION_ERROR" : observed.error_code);
            if (observed.outcome == contracts::RecognitionOutcome::NoHit) continue;
            return enter(candidate, std::move(observed), false);
        }
    }
    if (polling) {
        if (!can_enter(*polling)) return route_error(frame, current, "FLOW_HIT_LIMIT_EXHAUSTED");
        if (frame.pending) return blocked("POLL_WITH_PENDING_INPUT");
        return enter(*polling, std::nullopt, true);
    }
    return waiting(50ms);
}
contracts::Command FlowExecutor::command(const workflow::Input &input,
                                          const contracts::Observation &target) const {
    static const std::map<std::string, contracts::ActionKind> kinds{
        {"Click", contracts::ActionKind::Click}, {"Swipe", contracts::ActionKind::Swipe},
        {"ClickKey", contracts::ActionKind::ClickKey}};
    const auto found = kinds.find(input.command.at("kind").get<std::string>());
    require(found != kinds.end(), "FLOW_INPUT_KIND_INVALID");
    contracts::Command value;
    value.kind = found->second;
    value.x = input.command.value("x", 0); value.y = input.command.value("y", 0);
    value.x2 = input.command.value("x2", 0); value.y2 = input.command.value("y2", 0);
    value.duration = input.command.value("duration", 0); value.key = input.command.value("key", 0);
    if (input.use_target_center && (value.kind == contracts::ActionKind::Click ||
                                   value.kind == contracts::ActionKind::Swipe)) {
        require(target.center.has_value(), "FLOW_TARGET_NOT_POSITIONAL");
        std::int64_t x = target.center->x, y = target.center->y;
        if (input.target_offset) {
            x += input.target_offset->x; y += input.target_offset->y;
            if (input.clip_target_to_area) {
                x = std::clamp(x, std::int64_t(input.allowed_area.x),
                    std::int64_t(input.allowed_area.x) + input.allowed_area.width - 1);
                y = std::clamp(y, std::int64_t(input.allowed_area.y),
                    std::int64_t(input.allowed_area.y) + input.allowed_area.height - 1);
            }
        }
        require(x >= std::numeric_limits<int>::min() && x <= std::numeric_limits<int>::max() &&
                y >= std::numeric_limits<int>::min() && y <= std::numeric_limits<int>::max(),
                "FLOW_TARGET_OVERFLOW");
        value.x = static_cast<int>(x); value.y = static_cast<int>(y);
    }
    return value;
}
std::optional<TickResult> FlowExecutor::retry_pending_input(Frame &frame,
    const workflow::Step &current, const contracts::FrameEnvelope &image) {
    auto &pending = *frame.pending;
    const auto &source = program_.definitions.at(frame.definition).steps.at(pending.submitted_step);
    const auto &input = std::get<workflow::Input>(source.data);
    // 这里只推进栈顶活动帧的回执。事件处理器也可显式重试自己的安全按钮，
    // 但挂起的父业务输入不在本调用中，不能因事件标记禁止网络按钮自身的重试。
    if (!input.retry || pending.delivery_unknown) return std::nullopt;
    if (input.retry->max_submissions && pending.attempts >= input.retry->max_submissions)
        return std::nullopt; // 继续观察迟到结果，不把次数耗尽伪装成成功。
    if (ports_.prepare_input()) return waiting(1ms); // 通道重建后下轮重新取帧、先核对结果。
    const auto now = Clock::now();
    const auto &await = std::get<workflow::AwaitResult>(current.data);
    // 事件自己的退出预算可能比按钮结果预算长；退出等待可继续，按钮重试
    // 仍必须在首次结果期限内，不能借事件作用域延长重复输入窗口。
    if (now >= std::min(pending.result_started_at + await.budget + pending.event_pause, deadline_))
        return std::nullopt;
    if (now < pending.submitted_at + input.retry->interval + pending.event_pause - pending.delay_pause_base)
        return std::nullopt;
    require(result_identity_matches(image.identity, pending.before) &&
        image.identity.frame_id > pending.before.frame_id &&
        image.identity.action_epoch >= pending.action_epoch, "RETRY_EVIDENCE_STALE");
    const auto ready = ports_.recognize(image, input.retry->ready);
    if (ready.outcome == contracts::RecognitionOutcome::NoHit) {
        last_diagnostic_ = {{"source_path", source.source_path}, {"reason", "menu_retry_not_ready"},
            {"outcome", "waiting_for_page_or_indicator"}, {"attempts", pending.attempts},
            {"frame_id", image.identity.frame_id}};
        return waiting(250ms);
    }
    require_hit(ready, "RETRY_READY_RECOGNITION_ERROR");
    // 授权条件之外，原场景和按钮还必须在同一新帧重新命中。不能复用旧点位。
    const auto scene = ports_.recognize(image, input.scene);
    const auto target = ports_.recognize(image, input.target);
    if (scene.outcome == contracts::RecognitionOutcome::Error ||
        target.outcome == contracts::RecognitionOutcome::Error) return fail("RETRY_RECOGNITION_ERROR");
    if (scene.outcome == contracts::RecognitionOutcome::NoHit ||
        target.outcome == contracts::RecognitionOutcome::NoHit) return waiting(250ms);
    require_hit(scene, "RETRY_SCENE_ERROR");
    require_hit(target, "RETRY_TARGET_ERROR");
    require((!input.use_target_center || target.action_eligible) &&
        same_device(scene.basis, image.identity) && same_device(target.basis, image.identity) &&
        scene.basis.frame_id == image.identity.frame_id &&
        target.basis.frame_id == image.identity.frame_id, "RETRY_INPUT_EVIDENCE_INVALID");
    // ready/scene 的识别可能已发现转场；再检查一次结果，不把已完成操作重发。
    const auto result = ports_.recognize(image, await.condition);
    if (result.outcome == contracts::RecognitionOutcome::Hit) return waiting(1ms);
    require(result.outcome == contracts::RecognitionOutcome::NoHit, "RETRY_RESULT_RECOGNITION_ERROR");
    if (ports_.cancelled()) return waiting(1ms);
    if (!input.effect_binding.empty()) {
        const auto allowed = ports_.operate(input.effect_binding, {{"phase", "authorize"}},
            image, scene, source.source_path);
        if (allowed.state == OperationState::Waiting) return waiting(250ms);
        if (allowed.state != OperationState::Done) return blocked(allowed.detail);
    }
    const auto action = command(input, target);
    account_event_time(); // 下一段读取耗时只能记一次。
    const auto receipt = ports_.submit(action, scene, target, input.allowed_area, source.source_path);
    invalidate_observation();
    if (receipt.state == SubmissionState::Rejected && receipt.read_fault) {
        begin_observation_recovery(*receipt.read_fault);
        return observation_retry_wait();
    }
    finish_observation_recovery("input_context_readable");
    if (receipt.state == SubmissionState::Rejected) {
        // 门禁在底层发送前发现焦点/视口变化：本次没有新副作用，保留已有pending和期限。
        if (receipt.detail == "INPUT_CONTEXT_CHANGED" ||
            receipt.detail == "INPUT_CHANNEL_CHANGED_REOBSERVE_REQUIRED") return waiting(250ms);
        return fail("INPUT_REJECTED:" + receipt.detail);
    }
    // 保留首次提交/事件暂停/结果预算，重试不能无限续命，也不冒充输入成功。
    // before保存首次输入依据；每次补点只更新最后提交时间/epoch和实际次数。
    pending.action_epoch = receipt.action_epoch;
    pending.submitted_at = receipt.submitted_at;
    pending.delay_pause_base = pending.event_pause;
    pending.animation_pause = {};
    pending.delivery_unknown = receipt.state == SubmissionState::Unresolved;
    ++pending.attempts;
    if (!input.effect_binding.empty())
        ports_.operate(input.effect_binding, {{"phase", "submitted"}, {"delivery_unknown", pending.delivery_unknown}},
            std::nullopt, std::nullopt, source.source_path);
    last_diagnostic_ = {{"source_path", source.source_path}, {"reason", "same_menu_input_retried"},
        {"outcome", "awaiting_retry_result"}, {"attempts", pending.attempts},
        {"frame_id", image.identity.frame_id}, {"action_epoch", receipt.action_epoch},
        {"position", {action.x, action.y}}};
    if (pending.delivery_unknown) return waiting(250ms); // 仍可只读核对，禁止再次提交。
    require(receipt.action_epoch > image.identity.action_epoch &&
        receipt.submitted_at >= image.identity.captured_at, "INPUT_RECEIPT_INVALID");
    return waiting(250ms);
}
TickResult FlowExecutor::execute_step(Frame &frame, const workflow::Step &current) {
    if (frame.event_exits.empty() && Clock::now() - frame.entered_at >= current.time_limit)
        return route_error(frame, current, "FLOW_STAGE_TIMEOUT");
    if (!frame.event_exits.empty()) {
        const auto image = observation_frame();
        if (auto event = check_events(frame, current, image, workflow::EventClass::Overlay)) return *event;
    }
    if (current.business_guard) {
        const auto rules = effective_events(current);
        if (!frame.event_exits.empty() || std::any_of(rules.begin(), rules.end(), [](const ScopedEvent &rule) {
            return rule.rule.category == workflow::EventClass::Overlay;
        })) {
            const auto image = observation_frame();
            if (auto event = check_events(frame, current, image, workflow::EventClass::Overlay)) return *event;
        }
        const auto predicate = ports_.operate("BusinessPredicate", *current.business_guard,
            std::nullopt, std::nullopt, current.source_path);
        if (predicate.state == OperationState::Waiting) return waiting(50ms);
        if (predicate.state != OperationState::Done) return fail("BUSINESS_GUARD_ERROR:" + predicate.detail);
    }
    if (current.guard && !frame.selected_observation) {
        const auto image = observation_frame();
        if (auto event = check_events(frame, current, image, workflow::EventClass::Overlay)) return *event;
        auto observed = ports_.recognize(image, *current.guard);
        if (observed.outcome == contracts::RecognitionOutcome::Error)
            return fail(observed.error_code.empty() ? "RECOGNITION_ERROR" : observed.error_code);
        if (observed.outcome == contracts::RecognitionOutcome::NoHit) {
            if (auto event = check_unexpected(frame, current, image, "step_guard_not_confirmed")) return *event;
            if (std::holds_alternative<workflow::Input>(current.data))
                return reconsider_unsubmitted_input(frame);
            return waiting(50ms);
        }
        if (current.marks_known_scene) record_known_scene(frame, observed);
        frame.selected_frame = image;
        frame.selected_observation = std::move(observed);
    }
    if (std::holds_alternative<workflow::Route>(current.data)) { advance(frame, false); return progress(); }
    if (const auto *observe = std::get_if<workflow::Observe>(&current.data)) {
        // Observe 自身的请求必须执行；不能靠当前 lowering 恰好又填了 guard。
        const auto image = observation_frame();
        if (auto event = check_events(frame, current, image, workflow::EventClass::Overlay)) return *event;
        const auto result = ports_.recognize(image, observe->request);
        if (result.outcome == contracts::RecognitionOutcome::NoHit) {
            frame.selected_frame.reset(); frame.selected_observation.reset();
            if (auto event = check_unexpected(frame, current, image, "observation_not_confirmed")) return *event;
            return waiting(50ms);
        }
        require_hit(result, "OBSERVE_UNCONFIRMED");
        advance(frame); return progress();
    }
    if (const auto *input = std::get_if<workflow::Input>(&current.data)) {
        if (frame.pending) return blocked("INPUT_UNRESOLVED");
        if (ports_.prepare_input()) {
            invalidate_observation();
            return reconsider_unsubmitted_input(frame);
        }
        frame.selected_frame = observation_frame();
        // 本轮持有 FrameEnvelope 的副本（像素共享），reset 缓存不会销毁事件识别的载荷。
        const auto image = *frame.selected_frame;
        if (auto event = check_events(frame, current, image, workflow::EventClass::Overlay)) return *event;
        const auto scene = ports_.recognize(image, input->scene);
        if (scene.outcome == contracts::RecognitionOutcome::NoHit) {
            frame.selected_frame.reset(); frame.selected_observation.reset();
            if (auto event = check_unexpected(frame, current, image, "input_scene_not_confirmed")) return *event;
            return reconsider_unsubmitted_input(frame);
        }
        require_hit(scene, "SCENE_NOT_FOUND");
        const auto target = ports_.recognize(image, input->target);
        if (target.outcome == contracts::RecognitionOutcome::NoHit) {
            frame.selected_frame.reset(); frame.selected_observation.reset();
            if (auto event = check_unexpected(frame, current, image, "input_target_not_confirmed")) return *event;
            return reconsider_unsubmitted_input(frame);
        }
        require_hit(target, "TARGET_NOT_FOUND");
        require((!input->use_target_center || target.action_eligible) &&
            same_device(scene.basis, target.basis) && scene.basis.frame_id == target.basis.frame_id,
            "INPUT_EVIDENCE_INVALID");
        std::optional<recognition::Request> expected;
        std::chrono::milliseconds result_budget{0};
        const auto &definition = program_.definitions.at(frame.definition);
        if (current.next.size() == 1)
            if (const auto *await = std::get_if<workflow::AwaitResult>(
                &definition.steps.at(current.next.front()).data)) {
                expected = await->condition;
                result_budget = await->budget;
            }
        require(expected.has_value(), "INPUT_OBSERVER_NOT_DECLARED");
        if (!input->effect_binding.empty()) {
            const auto allowed = ports_.operate(input->effect_binding, {{"phase", "authorize"}},
                image, scene, current.source_path);
            if (allowed.state == OperationState::Waiting) return waiting(250ms);
            if (allowed.state != OperationState::Done) return blocked(allowed.detail);
        }
        account_event_time(); // 输入前元数据读失败可恢复，但输入本身绝不整体重放。
        const auto submitted = ports_.submit(command(*input, target), scene, target,
                                              input->allowed_area, current.source_path);
        invalidate_observation(); // 包括拒绝/送达未知；绝不再使用提交前的像素授权下一次输入。
        if (submitted.state == SubmissionState::Rejected && submitted.read_fault) {
            begin_observation_recovery(*submitted.read_fault);
            // 尚未发送本次动作；保留当前选择点，不消费新的 pending。
            return observation_retry_wait();
        }
        finish_observation_recovery("input_context_readable");
        if (submitted.state == SubmissionState::Rejected) {
            if (submitted.detail == "INPUT_CONTEXT_CHANGED" ||
                submitted.detail == "INPUT_CHANNEL_CHANGED_REOBSERVE_REQUIRED") {
                frame.selected_frame.reset(); frame.selected_observation.reset();
                return reconsider_unsubmitted_input(frame);
            }
            return fail("INPUT_REJECTED:" + submitted.detail);
        }
        // 先留下实际副作用事实，再验证回执；任何后续失败都不能丢掉它。
        frame.pending = PendingInput{current.source_path, image.identity, submitted.action_epoch,
            submitted.submitted_at, {}, std::move(expected), result_budget,
            submitted.state == SubmissionState::Unresolved, current.id, submitted.submitted_at};
        if (!input->effect_binding.empty())
            ports_.operate(input->effect_binding, {{"phase", "submitted"},
                {"delivery_unknown", frame.pending->delivery_unknown}}, std::nullopt, std::nullopt, current.source_path);
        if (submitted.state == SubmissionState::Unresolved) {
            advance(frame, false);
            return waiting(250ms); // 后置条件可以证明完成，未知送达本身不能授权重发。
        }
        require(submitted.action_epoch > image.identity.action_epoch &&
                submitted.submitted_at >= image.identity.captured_at, "INPUT_RECEIPT_INVALID");
        advance(frame); return progress();
    }
    if (const auto *await = std::get_if<workflow::AwaitResult>(&current.data)) {
        if (!frame.pending) return fail("AWAIT_WITHOUT_SUBMISSION");
        const auto now = Clock::now();
        const auto deadline = std::min(frame.pending->result_started_at + await->budget +
            frame.pending->event_pause, deadline_);
        if (now < frame.pending->submitted_at + await->initial_delay +
            frame.pending->animation_pause) {
            if (!frame.pending->delivery_unknown)
                if (auto event = poll_wait_events(frame, current)) return *event;
            return waiting(await->poll_interval);
        }
        const auto image = observation_frame();
        if (!frame.pending->delivery_unknown)
            if (auto event = check_events(frame, current, image, workflow::EventClass::Overlay)) return *event;
        const auto observed = ports_.recognize(image, await->condition);
        if (observed.outcome == contracts::RecognitionOutcome::NoHit) {
            if (!frame.pending->delivery_unknown)
                if (auto event = check_unexpected(frame, current, image, "input_result_not_confirmed", now >= deadline)) return *event;
            if (frame.event_exits.empty() && now >= deadline) return route_error(frame, current, "AWAIT_RESULT_TIMEOUT");
            if (auto retried = retry_pending_input(frame, current, image)) return *retried;
            return waiting(await->poll_interval);
        }
        require_hit(observed, "AWAIT_RECOGNITION_ERROR");
        require(result_identity_matches(observed.basis, frame.pending->before) &&
            observed.basis.frame_id > frame.pending->before.frame_id &&
            observed.basis.action_epoch >= frame.pending->action_epoch, "AWAIT_EVIDENCE_STALE");
        // 新帧正向确认结果才结束连续尝试；异常处理器返回、纯跳转、动画均不清零。
        // 作者显式repeat_limit仍是业务循环约束，不冒充连续失败次数。
        const auto &submitted_step = program_.definitions.at(frame.definition).steps.at(frame.pending->submitted_step);
        if (submitted_step.consecutive_input_limit) {
            frame.hits.erase(submitted_step.id);
            frame.hits.erase(current.id);
        }
        if (frame.pending->delivery_unknown && !ports_.settle_observed_input())
            return blocked("OBSERVED_RESULT_INPUT_CLEANUP_UNCONFIRMED");
        report_input_result(*frame.pending, "confirmed", observed.basis.frame_id);
        frame.pending.reset();
        finish_observation_recovery("original_result_confirmed");
        advance(frame); return progress();
    }
    if (const auto *poll = std::get_if<workflow::Poll>(&current.data)) {
        if (frame.pending) return blocked("POLL_WITH_PENDING_INPUT");
        if (!frame.poll_until) frame.poll_until = Clock::now() + poll->interval;
        if (Clock::now() < *frame.poll_until) {
            if (auto event = poll_wait_events(frame, current)) return *event;
            return waiting(std::min(25ms, std::max(1ms,
                std::chrono::duration_cast<std::chrono::milliseconds>(*frame.poll_until - Clock::now()))));
        }
        frame.poll_until.reset();
        frame.selected_frame.reset(); frame.selected_observation.reset();
        frame.next_pending = true;
        frame.error_pending = false;
        // 不调用 advance：纯重看既不算业务进展，也不重置异常去抖/调用计时。
        return progress();
    }
    if (const auto *wait = std::get_if<workflow::Wait>(&current.data)) {
        if (Clock::now() < frame.entered_at + wait->duration) {
            if (auto event = poll_wait_events(frame, current)) return *event;
            return waiting(25ms);
        }
        advance(frame, false); return progress();
    }
    if (const auto *call = std::get_if<workflow::Call>(&current.data)) {
        require(stack_.size() < 8, "FLOW_CALL_DEPTH_LIMIT");
        const auto &child = program_.definitions.at(call->definition);
        Frame nested;
        nested.definition = child.id; nested.current = child.entry;
        nested.invoked_at = nested.entered_at = Clock::now();
        if (child.cumulative_budget) nested.invocation_deadline = nested.invoked_at + *child.cumulative_budget;
        nested.hits[child.entry] = 1;
        account_event_time();
        stack_.push_back(std::move(nested));
        return progress();
    }
    if (const auto *returned = std::get_if<workflow::Return>(&current.data)) {
        if (stack_.size() == 1) return returned->outcome == "handoff"
            ? blocked("HANDOFF_REQUIRES_CALLER:" + returned->port) : fail("ROOT_RETURN_WITHOUT_FINISH");
        if (returned->outcome == "failure") return return_business_failure(returned->reason);
        // 事件返回时父输入可仍待新帧确认；只检查即将弹出的子帧。
        if (frame.pending) return blocked("CHILD_INPUT_UNRESOLVED");
        account_event_time();
        const auto ended = std::move(stack_.back());
        stack_.pop_back();
        invalidate_observation();
        auto &parent = stack_.back();
        parent.selected_frame.reset(); parent.selected_observation.reset();
        if (ended.event) {
            const auto &event = ended.event->rule;
            if (!event.on_device_restart)
                parent.event_exits.push_back({event.id, event.detect, Clock::now() + event.exit_budget});
            if (event.resume == workflow::ResumeMode::Replan)
                parent.resume = PendingResume{event, ended.event->owner};
        } else {
            // 普通子调用不消耗父调用节点的选路等待；已排除的事件时间不重复加。
            parent.entered_at += std::max(Clock::duration::zero(),
                Clock::now() - ended.invoked_at - ended.paused_event_time);
            const auto *call = std::get_if<workflow::Call>(&step().data);
            require(call != nullptr, "RETURN_CALL_FRAME_MISSING");
            if (returned->outcome == "handoff") {
                require(call->handoffs.contains(returned->port), "RETURN_HANDOFF_UNBOUND");
                parent.returned_targets = call->handoffs.at(returned->port);
            }
            advance(parent, false);
        }
        return progress();
    }
    const auto *business = std::get_if<workflow::BusinessConfirm>(&current.data);
    const auto *operation = std::get_if<workflow::RegisteredOperation>(&current.data);
    if (operation && operation->binding == "BeginObservationPhase") {
        const auto phase = operation->parameters.at("phase").get<std::string>();
        const auto budget = std::chrono::milliseconds{operation->parameters.at("budget_ms").get<std::int64_t>()};
        require(!phase.empty() && budget.count() > 0 && budget <= 30min, "OBSERVATION_PHASE_INVALID");
        frame.phase_deadlines.try_emplace(phase, Clock::now() + budget);
        advance(frame, false); return progress();
    }
    if (operation && operation->binding == "EndObservationPhase") {
        require(frame.phase_deadlines.erase(operation->parameters.at("phase").get<std::string>()) == 1,
                "OBSERVATION_PHASE_NOT_ACTIVE");
        advance(frame, false); return progress();
    }
    if (business || operation) {
        const auto &binding = business ? business->binding : operation->binding;
        // 只调用声明的业务绑定，不在这里解释 event/strategy 等游戏参数。
        const auto &parameters = business ? business->parameters : operation->parameters;
        const auto image = observation_frame();
        if (auto event = check_events(frame, current, image, workflow::EventClass::Overlay)) return *event;
        const auto result = ports_.operate(binding, parameters, image, frame.selected_observation, current.source_path);
        if (result.state == OperationState::Done) { advance(frame); return progress(); }
        if (result.state == OperationState::Waiting) {
            frame.selected_frame.reset(); frame.selected_observation.reset();
            if (auto event = check_unexpected(frame, current, image, "business_confirmation_pending")) return *event;
            invalidate_observation();
            return {TickState::Waiting, std::min(result.wake_at, deadline_), {}, current.source_path};
        }
        if (result.state == OperationState::ExternalBlocked) return blocked(result.detail);
        return route_error(frame, current, result.detail);
    }
    if (std::holds_alternative<workflow::Finish>(current.data)) {
        if (stack_.size() != 1 || has_unresolved_input()) return fail("ROOT_FINISH_INVALID");
        terminal_ = {TickState::Completed, {}, {}, current_source_path()};
        return terminal_;
    }
    if (const auto *value = std::get_if<workflow::BusinessFail>(&current.data)) {
        if (stack_.size() != 1) return fail("CHILD_BUSINESS_FAILURE_NOT_RETURNED");
        if (has_unresolved_input()) return blocked("BUSINESS_FAILURE_INPUT_UNRESOLVED");
        return business_fail(value->reason, current.source_path);
    }
    if (const auto *value = std::get_if<workflow::ExternalBlocked>(&current.data)) return blocked(value->reason);
    if (const auto *value = std::get_if<workflow::Fail>(&current.data)) {
        // 业务显式转交恢复时先辨认现场；配置/资源/识别异常仍从其原错误入口终止。
        const auto image = observation_frame();
        if (auto event = check_unexpected(frame, current, image, value->reason, true)) return *event;
        return fail(value->reason);
    }
    return fail("FLOW_STEP_UNKNOWN");
}
nlohmann::json FlowExecutor::observation_recovery_snapshot() const {
    if (!read_recovery_) return last_read_recovery_;
    const auto &read = *read_recovery_;
    return {{"active", true}, {"state", read.suspended ? "retrying_observation" : "awaiting_input_validation"},
        {"failures", read.failures}, {"code", read.last.code}, {"operation", read.last.operation},
        {"command_timeout_ms", read.last.timeout.count()}, {"command_elapsed_ms", read.last.elapsed.count()},
        {"outage_limit_ms", observation_policy_.outage_limit.count()},
        {"started_at_ns", std::chrono::duration_cast<std::chrono::nanoseconds>(read.started.time_since_epoch()).count()},
        {"command_details", read.last.details},
        {"reconnect_count", reconnects_.size()},
        {"source_path", current_source_path()}, {"input_replayed", false}};
}
void FlowExecutor::begin_observation_recovery(const contracts::ReadFault &fault) {
    const auto now = Clock::now();
    if (!read_recovery_) {
        // accounted_at_ 在本次读取前设置；第一次阻塞读取的耗时也属于故障区间。
        read_recovery_ = ReadRecovery{std::min(accounted_at_, now), now, 0, fault};
        read_recovery_->source_path = current_source_path();
        read_recovery_->stack_depth = stack_.size();
    }
    auto &read = *read_recovery_;
    read.suspended = true;
    read.awaiting_input_validation = read.awaiting_input_validation ||
        fault.stage == contracts::ReadFaultStage::InputContext;
    read.last = fault;
    if (fault.kind == contracts::ReadFaultKind::TransportUnavailable) read.device_checked = false;
    ++read.failures;
    auto delay = observation_policy_.first_delay;
    for (unsigned i = 1; i < read.failures && delay < observation_policy_.max_delay; ++i)
        delay = std::min(observation_policy_.max_delay, delay * 2);
    read.next_attempt = now + delay;
    // account_event_time 按事件/读取暂停的并集记账；不改原输入的 submitted_at。
    account_event_time();
    invalidate_observation();
}
void FlowExecutor::finish_observation_recovery(const char *outcome) {
    if (!read_recovery_) return;
    if (read_recovery_->awaiting_input_validation &&
        read_recovery_->source_path != current_source_path()) return;
    account_event_time();
    last_read_recovery_ = observation_recovery_snapshot();
    last_read_recovery_["active"] = false;
    last_read_recovery_["state"] = outcome;
    last_read_recovery_["duration_ms"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        Clock::now() - read_recovery_->started).count();
    read_recovery_.reset();
}
TickResult FlowExecutor::observation_retry_wait() {
    require(read_recovery_.has_value(), "OBSERVATION_RECOVERY_NOT_ACTIVE");
    const auto &read = *read_recovery_;
    const auto limit = std::min(deadline_, read.started + observation_policy_.outage_limit);
    if (Clock::now() >= limit)
        return fail("OBSERVATION_RECOVERY_EXHAUSTED:" + read.last.code);
    invalidate_observation();
    return {TickState::Waiting, std::min(read.next_attempt, limit),
            "OBSERVATION_RETRY", current_source_path()};
}
TickResult FlowExecutor::retry_observation() {
    require(read_recovery_ && read_recovery_->suspended, "OBSERVATION_RECOVERY_NOT_ACTIVE");
    if (Clock::now() < read_recovery_->next_attempt) return observation_retry_wait();
    ports_.observation_window(std::min(deadline_, read_recovery_->started + observation_policy_.outage_limit));
    if (!read_recovery_->device_checked) {
        try {
            if (const auto proof = ports_.recover_observation()) {
                require(proof->after > proof->before && !proof->created_identity.empty(), "RECONNECT_PROOF_INVALID");
                reconnects_.push_back(*proof);
                restart_handler_pending_ = restart_handler_pending_ || proof->application_restarted;
            }
            read_recovery_->device_checked = true;
        } catch (const contracts::ObservationUnavailable &error) {
            begin_observation_recovery(error.fault());
            return observation_retry_wait();
        }
    }
    invalidate_observation();
    (void)observation_frame(); // 只读；不派发剧情、补给或任何游戏输入。
    account_event_time();
    if (Clock::now() >= std::min(deadline_, read_recovery_->started + observation_policy_.outage_limit))
        return fail("OBSERVATION_RECOVERY_EXHAUSTED:" + read_recovery_->last.code);
    if (restart_handler_pending_) {
        const auto &root = program_.definitions.at(program_.root_definition);
        const auto rule = std::find_if(root.events.begin(), root.events.end(),
            [](const workflow::EventRule &event) { return event.on_device_restart; });
        require(rule != root.events.end(), "RESTART_OBSERVATION_HANDLER_MISSING");
        require(stack_.size() < 8, "EVENT_DEPTH_LIMIT");
        const auto &definition = program_.definitions.at(rule->handler_definition);
        Frame handler;
        handler.definition = definition.id; handler.current = definition.entry;
        handler.invoked_at = handler.entered_at = Clock::now();
        if (definition.cumulative_budget) handler.invocation_deadline = handler.invoked_at + *definition.cumulative_budget;
        handler.event = ScopedEvent{*rule, 0};
        read_recovery_->awaiting_input_validation = false;
        finish_observation_recovery("device_reconnected");
        invalidate_observation();
        stack_.push_back(std::move(handler));
        restart_handler_pending_ = false;
    } else if (read_recovery_->awaiting_input_validation) {
        // 读到画面后回到原节点复核真正的输入上下文；再次超时仍是同一故障区间。
        read_recovery_->suspended = false;
    } else finish_observation_recovery("observation_restored");
    return progress(); // 不 advance、不清 pending、不把恢复成功当作业务成功。
}
bool FlowExecutor::result_identity_matches(const contracts::FrameIdentity &current,
                                         const contracts::FrameIdentity &before) const {
    if (same_device(current, before)) return true;
    auto linked_generation = before.connection_generation;
    for (const auto &proof : reconnects_)
        if (proof.device_id == before.device_id && proof.before == linked_generation)
            linked_generation = proof.after;
    if (linked_generation != current.connection_generation) return false;
    // 仅用于观察结果比对；不修改pending.before，输入门禁仍要求当前代次和新帧。
    auto comparison = current;
    comparison.connection_generation = before.connection_generation;
    return same_device(comparison, before);
}
} // namespace wvd::runtime
