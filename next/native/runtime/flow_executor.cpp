#include "flow_executor.hpp"
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
                           std::chrono::milliseconds total_budget)
    : program_(program), ports_(ports), deadline_(Clock::now() + total_budget),
      accounted_at_(Clock::now()) {
    program_.validate();
    require(total_budget.count() > 0 && total_budget <= std::chrono::hours{24},
            "FLOW_TOTAL_BUDGET_INVALID");
    const auto &root = program_.definitions.at(program_.root_definition);
    Frame frame;
    frame.definition = root.id;
    frame.current = root.entry;
    frame.invoked_at = frame.entered_at = Clock::now();
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
                              {"source_path", nlohmann::json::parse(node.source_path, nullptr, false)}});
        if (frame.pending) {
            const auto &input = *frame.pending;
            pending.push_back({{"source_path", input.source_path},
                               {"basis_frame", input.before.frame_id},
                               {"basis_epoch", input.before.action_epoch},
                               {"action_epoch", input.action_epoch},
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
            {"active_event", std::move(active_event)}, {"suspended_step", std::move(suspended)}};
}
bool FlowExecutor::has_unresolved_input() const {
    return std::any_of(stack_.begin(), stack_.end(),
                       [](const Frame &f) { return f.pending.has_value(); });
}
TickResult FlowExecutor::progress() const {
    return {TickState::Progress, {}, {}, current_source_path()};
}
TickResult FlowExecutor::waiting(std::chrono::milliseconds delay) const {
    return {TickState::Waiting, std::min(Clock::now() + delay, deadline_), {}, current_source_path()};
}
TickResult FlowExecutor::fail(std::string code) {
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
    if (frame.pending) return blocked("INPUT_RESULT_UNCONFIRMED:" + code);
    if (frame.error_pending || current.on_error.empty()) return fail(std::move(code));
    frame.next_pending = frame.error_pending = true;
    frame.selected_frame.reset();
    frame.selected_observation.reset();
    frame.delay_until.reset();
    frame.entered_at = Clock::now();
    return progress();
}
TickResult FlowExecutor::tick() {
    if (terminal_.state != TickState::Progress) return terminal_;
    if (ports_.cancelled()) {
        terminal_ = {TickState::Cancelled, {}, "CANCELLED", current_source_path()};
        return terminal_;
    }
    if (Clock::now() >= deadline_) return fail("FLOW_TOTAL_DEADLINE");
    try {
        require(!stack_.empty() && stack_.size() <= 8, "FLOW_STACK_INVALID");
        account_event_time();
        for (const auto &frame : stack_)
            for (const auto &[name, deadline] : frame.phase_deadlines)
                if (Clock::now() >= deadline) return fail("OBSERVATION_PHASE_TIMEOUT:" + name);
        auto &frame = stack_.back();
        const auto &current = step();
        if (frame.resume) return resume_event(frame, current);
        return frame.next_pending ? select_next(frame, current) : execute_step(frame, current);
    } catch (const std::exception &error) { return fail(error.what()); }
    catch (...) { return fail("FLOW_UNEXPECTED_EXCEPTION"); }
}
void FlowExecutor::advance(Frame &frame) {
    frame.selected_frame.reset();
    frame.selected_observation.reset();
    frame.next_pending = true;
    frame.error_pending = false;
    const auto delay = program_.definitions.at(frame.definition).steps.at(frame.current).delay_after;
    frame.delay_until = delay.count() ? std::optional{Clock::now() + delay} : std::nullopt;
}
TickResult FlowExecutor::select_next(Frame &frame, const workflow::Step &current) {
    if (frame.delay_until && Clock::now() < *frame.delay_until) {
        if (auto event = poll_wait_events(frame, current)) return *event;
        return waiting(25ms);
    }
    frame.delay_until.reset();
    const auto &candidates = frame.error_pending ? current.on_error : current.next;
    if (candidates.empty()) return fail(frame.error_pending ?
        "FLOW_ERROR_ROUTE_MISSING" : "FLOW_SUCCESSOR_MISSING");
    if (frame.event_exits.empty() && Clock::now() - frame.entered_at >= current.time_limit)
        return route_error(frame, current, "FLOW_STAGE_TIMEOUT");
    const auto &definition = program_.definitions.at(frame.definition);
    if (std::none_of(candidates.begin(), candidates.end(), [&](const std::string &id) {
        return frame.hits[id] < definition.steps.at(id).max_hit;
    })) return route_error(frame, current, "FLOW_HIT_LIMIT_EXHAUSTED");
    const auto image = ports_.capture();
    if (auto event = check_events(frame, current, image, workflow::EventClass::Overlay)) return *event;
    for (const auto &id : candidates) {
        const auto &candidate = definition.steps.at(id);
        if (frame.hits[id] >= candidate.max_hit) continue;
        std::optional<contracts::Observation> observed;
        if (candidate.guard) {
            observed = ports_.recognize(image, *candidate.guard);
            if (observed->outcome == contracts::RecognitionOutcome::Error)
                return fail(observed->error_code.empty() ? "RECOGNITION_ERROR" : observed->error_code);
            if (observed->outcome == contracts::RecognitionOutcome::NoHit) continue;
        }
        frame.current = id;
        ++frame.hits[id];
        frame.entered_at = Clock::now();
        frame.next_pending = frame.error_pending = false;
        frame.selected_frame = image;
        frame.selected_observation = std::move(observed);
        return progress();
    }
    if (auto event = check_events(frame, current, image, workflow::EventClass::Encounter)) return *event;
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
TickResult FlowExecutor::execute_step(Frame &frame, const workflow::Step &current) {
    if (frame.event_exits.empty() && Clock::now() - frame.entered_at >= current.time_limit)
        return route_error(frame, current, "FLOW_STAGE_TIMEOUT");
    if (!frame.event_exits.empty()) {
        const auto image = ports_.capture();
        if (auto event = check_events(frame, current, image, workflow::EventClass::Overlay)) return *event;
    }
    if (current.guard && !frame.selected_observation) {
        const auto image = ports_.capture();
        if (auto event = check_events(frame, current, image, workflow::EventClass::Overlay)) return *event;
        auto observed = ports_.recognize(image, *current.guard);
        if (observed.outcome == contracts::RecognitionOutcome::Error)
            return fail(observed.error_code.empty() ? "RECOGNITION_ERROR" : observed.error_code);
        if (observed.outcome == contracts::RecognitionOutcome::NoHit) {
            if (auto event = check_events(frame, current, image, workflow::EventClass::Encounter)) return *event;
            return waiting(50ms);
        }
        frame.selected_frame = image;
        frame.selected_observation = std::move(observed);
    }
    if (std::holds_alternative<workflow::Route>(current.data)) { advance(frame); return progress(); }
    if (const auto *observe = std::get_if<workflow::Observe>(&current.data)) {
        // Observe 自身的请求必须执行；不能靠当前 lowering 恰好又填了 guard。
        const auto image = frame.selected_frame ? *frame.selected_frame : ports_.capture();
        if (auto event = check_events(frame, current, image, workflow::EventClass::Overlay)) return *event;
        const auto result = ports_.recognize(image, observe->request);
        if (result.outcome == contracts::RecognitionOutcome::NoHit) {
            frame.selected_frame.reset(); frame.selected_observation.reset();
            if (auto event = check_events(frame, current, image, workflow::EventClass::Encounter)) return *event;
            return waiting(50ms);
        }
        require_hit(result, "OBSERVE_UNCONFIRMED");
        advance(frame); return progress();
    }
    if (const auto *input = std::get_if<workflow::Input>(&current.data)) {
        if (frame.pending) return blocked("INPUT_UNRESOLVED");
        if (!frame.selected_frame) frame.selected_frame = ports_.capture();
        // 本轮持有 FrameEnvelope 的副本（像素共享），reset 缓存不会销毁事件识别的载荷。
        const auto image = *frame.selected_frame;
        if (auto event = check_events(frame, current, image, workflow::EventClass::Overlay)) return *event;
        const auto scene = ports_.recognize(image, input->scene);
        if (scene.outcome == contracts::RecognitionOutcome::NoHit) {
            frame.selected_frame.reset(); frame.selected_observation.reset();
            if (auto event = check_events(frame, current, image, workflow::EventClass::Encounter)) return *event;
            return waiting(50ms);
        }
        require_hit(scene, "SCENE_NOT_FOUND");
        const auto target = ports_.recognize(image, input->target);
        if (target.outcome == contracts::RecognitionOutcome::NoHit) {
            frame.selected_frame.reset(); frame.selected_observation.reset();
            if (auto event = check_events(frame, current, image, workflow::EventClass::Encounter)) return *event;
            return waiting(50ms);
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
        const auto submitted = ports_.submit(command(*input, target), scene, target,
                                              input->allowed_area, current.source_path);
        if (submitted.state == SubmissionState::Rejected) return fail("INPUT_REJECTED:" + submitted.detail);
        // 先留下实际副作用事实，再验证回执；任何后续失败都不能丢掉它。
        frame.pending = PendingInput{current.source_path, image.identity, submitted.action_epoch,
            submitted.submitted_at, {}, std::move(expected), result_budget,
            submitted.state == SubmissionState::Unresolved};
        if (submitted.state == SubmissionState::Unresolved)
            return blocked("INPUT_SUBMISSION_UNRESOLVED:" + submitted.detail);
        require(submitted.action_epoch > image.identity.action_epoch &&
                submitted.submitted_at >= image.identity.captured_at, "INPUT_RECEIPT_INVALID");
        advance(frame); return progress();
    }
    if (const auto *await = std::get_if<workflow::AwaitResult>(&current.data)) {
        if (!frame.pending) return fail("AWAIT_WITHOUT_SUBMISSION");
        if (frame.pending->delivery_unknown) return blocked("INPUT_DELIVERY_UNKNOWN");
        const auto now = Clock::now();
        const auto deadline = std::min(frame.pending->submitted_at + await->budget +
            frame.pending->event_pause, deadline_);
        if (frame.event_exits.empty() && now >= deadline) return blocked("AWAIT_RESULT_TIMEOUT");
        if (now < frame.pending->submitted_at + await->initial_delay + frame.pending->event_pause) {
            if (auto event = poll_wait_events(frame, current)) return *event;
            return waiting(await->poll_interval);
        }
        const auto image = ports_.capture();
        if (auto event = check_events(frame, current, image, workflow::EventClass::Overlay)) return *event;
        const auto observed = ports_.recognize(image, await->condition);
        if (observed.outcome == contracts::RecognitionOutcome::NoHit) {
            if (auto event = check_events(frame, current, image, workflow::EventClass::Encounter)) return *event;
            return waiting(await->poll_interval);
        }
        require_hit(observed, "AWAIT_RECOGNITION_ERROR");
        require(same_device(observed.basis, frame.pending->before) &&
            observed.basis.frame_id > frame.pending->before.frame_id &&
            observed.basis.action_epoch >= frame.pending->action_epoch, "AWAIT_EVIDENCE_STALE");
        frame.pending.reset();
        advance(frame); return progress();
    }
    if (const auto *wait = std::get_if<workflow::Wait>(&current.data)) {
        if (Clock::now() < frame.entered_at + wait->duration) {
            if (auto event = poll_wait_events(frame, current)) return *event;
            return waiting(25ms);
        }
        advance(frame); return progress();
    }
    if (const auto *call = std::get_if<workflow::Call>(&current.data)) {
        require(stack_.size() < 8, "FLOW_CALL_DEPTH_LIMIT");
        const auto &child = program_.definitions.at(call->definition);
        Frame nested;
        nested.definition = child.id; nested.current = child.entry;
        nested.invoked_at = nested.entered_at = Clock::now();
        nested.hits[child.entry] = 1;
        account_event_time();
        stack_.push_back(std::move(nested));
        return progress();
    }
    if (const auto *returned = std::get_if<workflow::Return>(&current.data)) {
        if (stack_.size() == 1) return fail("ROOT_RETURN_WITHOUT_FINISH");
        if (returned->outcome == "failure") return return_business_failure(returned->reason);
        // 事件返回时父输入可仍待新帧确认；只检查即将弹出的子帧。
        if (frame.pending) return blocked("CHILD_INPUT_UNRESOLVED");
        account_event_time();
        const auto ended = std::move(stack_.back());
        stack_.pop_back();
        auto &parent = stack_.back();
        parent.selected_frame.reset(); parent.selected_observation.reset();
        if (ended.event) {
            const auto &event = ended.event->rule;
            parent.event_exits.push_back({event.id, event.detect, Clock::now() + event.exit_budget});
            if (event.resume == workflow::ResumeMode::Replan)
                parent.resume = PendingResume{event, ended.event->owner};
        } else {
            // 普通子调用不消耗父调用节点的选路等待；已排除的事件时间不重复加。
            parent.entered_at += std::max(Clock::duration::zero(),
                Clock::now() - ended.invoked_at - ended.paused_event_time);
            advance(parent);
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
        advance(frame); return progress();
    }
    if (operation && operation->binding == "EndObservationPhase") {
        require(frame.phase_deadlines.erase(operation->parameters.at("phase").get<std::string>()) == 1,
                "OBSERVATION_PHASE_NOT_ACTIVE");
        advance(frame); return progress();
    }
    if (business || operation) {
        const auto &binding = business ? business->binding : operation->binding;
        // 只调用声明的业务绑定，不在这里解释 event/strategy 等游戏参数。
        const auto &parameters = business ? business->parameters : operation->parameters;
        const auto image = frame.selected_frame ? *frame.selected_frame : ports_.capture();
        if (auto event = check_events(frame, current, image, workflow::EventClass::Overlay)) return *event;
        const auto result = ports_.operate(binding, parameters, image, frame.selected_observation, current.source_path);
        if (result.state == OperationState::Done) { advance(frame); return progress(); }
        if (result.state == OperationState::Waiting) {
            frame.selected_frame.reset(); frame.selected_observation.reset();
            if (auto event = check_events(frame, current, image, workflow::EventClass::Encounter)) return *event;
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
    if (const auto *value = std::get_if<workflow::Fail>(&current.data)) return fail(value->reason);
    return fail("FLOW_STEP_UNKNOWN");
}
} // namespace wvd::runtime
