#include "flow_executor.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace wvd::runtime {
namespace {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

void require(bool condition, const char *code) {
    if (!condition) throw std::runtime_error(code);
}
bool same_device(const contracts::FrameIdentity &left,
                 const contracts::FrameIdentity &right) {
    return left.device_id == right.device_id && left.game_id == right.game_id &&
        left.pack_revision == right.pack_revision &&
        left.generation == right.generation &&
        left.connection_generation == right.connection_generation &&
        left.viewport_id == right.viewport_id &&
        left.recognition_size == right.recognition_size;
}
void require_hit(const contracts::Observation &observed, const char *code) {
    if (observed.outcome == contracts::RecognitionOutcome::Error)
        throw std::runtime_error(observed.error_code.empty() ?
            "RECOGNITION_ERROR" : observed.error_code);
    require(observed.outcome == contracts::RecognitionOutcome::Hit, code);
}
} // namespace

FlowExecutor::FlowExecutor(const workflow::FlowProgram &program, FlowPorts &ports,
                           std::chrono::milliseconds total_budget)
    : program_(program), ports_(ports), deadline_(Clock::now() + total_budget) {
    program_.validate();
    require(total_budget.count() > 0 && total_budget <= std::chrono::hours{24},
            "FLOW_TOTAL_BUDGET_INVALID");
    const auto &root = program_.definitions.at(program_.root_definition);
    Frame frame;
    frame.definition = root.id;
    frame.current = root.entry;
    frame.invoked_at = Clock::now();
    frame.entered_at = Clock::now();
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

bool FlowExecutor::has_unresolved_input() const {
    return std::any_of(stack_.begin(), stack_.end(),
                       [](const Frame &frame) { return frame.pending.has_value(); });
}

TickResult FlowExecutor::progress() const {
    return {TickState::Progress, {}, {}, current_source_path()};
}

TickResult FlowExecutor::waiting(std::chrono::milliseconds delay) const {
    return {TickState::Waiting, std::min(Clock::now() + delay, deadline_), {},
            current_source_path()};
}

TickResult FlowExecutor::fail(std::string code) {
    terminal_ = {TickState::Failed, {}, std::move(code), current_source_path()};
    return terminal_;
}

TickResult FlowExecutor::blocked(std::string code) {
    terminal_ = {TickState::ExternalBlocked, {}, std::move(code), current_source_path()};
    return terminal_;
}

TickResult FlowExecutor::route_error(Frame &frame, const workflow::Step &current,
                                     std::string code) {
    if (frame.pending) return blocked("INPUT_RESULT_UNCONFIRMED:" + code);
    if (frame.error_pending || current.on_error.empty()) return fail(std::move(code));
    frame.next_pending = true;
    frame.error_pending = true;
    frame.selected_frame.reset();
    frame.selected_observation.reset();
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
        for (std::size_t i = 0; i < stack_.size(); ++i) {
            const bool paused_by_event = std::any_of(stack_.begin() + i + 1,
                stack_.end(), [](const Frame &child) { return child.event.has_value(); });
            if (paused_by_event || !stack_[i].event_exits.empty()) continue;
            for (const auto &[name, deadline] : stack_[i].phase_deadlines)
                if (Clock::now() >= deadline)
                    return fail("OBSERVATION_PHASE_TIMEOUT:" + name);
        }
        auto &frame = stack_.back();
        const auto &current = step();
        if (frame.next_pending) return select_next(frame, current);
        return execute_step(frame, current);
    } catch (const std::exception &error) {
        return fail(error.what());
    } catch (...) {
        return fail("FLOW_UNEXPECTED_EXCEPTION");
    }
}

void FlowExecutor::advance(Frame &frame) {
    frame.selected_frame.reset();
    frame.selected_observation.reset();
    frame.next_pending = true;
    frame.error_pending = false;
}

std::optional<TickResult> FlowExecutor::check_events(
    Frame &frame, const workflow::Step &current,
    const contracts::FrameEnvelope &image, workflow::EventClass category) {
    std::vector<const workflow::EventRule *> hits;
    int priority = std::numeric_limits<int>::min();
    bool exiting = false;
    if (category == workflow::EventClass::Overlay && !frame.event_exits.empty()) {
        const auto now = Clock::now();
        for (auto it = frame.event_exits.begin(); it != frame.event_exits.end();) {
            const auto observed = ports_.recognize(image, it->detect);
            if (observed.outcome == contracts::RecognitionOutcome::Error)
                return fail(observed.error_code.empty() ? "EVENT_EXIT_RECOGNITION_ERROR" :
                    observed.error_code);
            if (observed.outcome == contracts::RecognitionOutcome::Hit) {
                if (now >= it->deadline) return fail("EVENT_HANDLER_NO_PROGRESS:" + it->id);
                exiting = true;
                ++it;
            } else {
                it = frame.event_exits.erase(it);
            }
        }
        if (frame.event_exits.empty()) {
            require(frame.exit_pause_started.has_value(), "EVENT_EXIT_PAUSE_MISSING");
            const auto elapsed = now - *frame.exit_pause_started;
            frame.entered_at += elapsed;
            if (frame.pending) frame.pending->event_pause += elapsed;
            for (auto &[name, deadline] : frame.phase_deadlines) {
                (void)name;
                deadline += elapsed;
            }
            frame.exit_pause_started.reset();
        }
    }
    const auto rules = effective_events(current);
    for (const auto &rule : rules) {
        if (rule.category != category) continue;
        if (std::any_of(frame.event_exits.begin(), frame.event_exits.end(),
            [&](const EventExit &exit) { return exit.id == rule.id; })) continue;
        const bool recursive = std::any_of(stack_.begin(), stack_.end(),
            [&](const Frame &active) { return active.event && active.event->id == rule.id; });
        if (recursive) continue;
        const auto observation = ports_.recognize(image, rule.detect);
        if (observation.outcome == contracts::RecognitionOutcome::Error)
            return fail(observation.error_code.empty() ? "EVENT_RECOGNITION_ERROR" :
                        observation.error_code);
        if (observation.outcome == contracts::RecognitionOutcome::NoHit) continue;
        if (rule.priority > priority) {
            priority = rule.priority;
            hits.clear();
        }
        if (rule.priority == priority) hits.push_back(&rule);
    }
    if (hits.size() > 1) {
        const auto now = Clock::now();
        if (!frame.ambiguity_since || frame.ambiguity_category != category ||
            frame.ambiguity_priority != priority) {
            frame.ambiguity_since = now;
            frame.ambiguity_category = category;
            frame.ambiguity_priority = priority;
        }
        const auto budget = (*std::min_element(hits.begin(), hits.end(),
            [](const auto *left, const auto *right) {
                return left->ambiguity_budget < right->ambiguity_budget;
            }))->ambiguity_budget;
        if (now - *frame.ambiguity_since >= budget)
            return fail("EVENT_AMBIGUOUS");
        return waiting(50ms);
    }
    if (frame.ambiguity_category == category) frame.ambiguity_since.reset();
    if (hits.empty()) {
        if (exiting) return waiting(50ms);
        return std::nullopt;
    }
    const auto &rule = *hits.front();
    if (rule.disposition == workflow::EventDisposition::ExternalBlocked)
        return blocked(rule.reason);
    require(stack_.size() < 8, "EVENT_DEPTH_LIMIT");
    const auto &child = program_.definitions.at(rule.handler_definition);
    Frame handler;
    handler.definition = child.id;
    handler.current = child.entry;
    handler.invoked_at = Clock::now();
    handler.entered_at = Clock::now();
    handler.hits[child.entry] = 1;
    handler.event = rule;
    stack_.push_back(std::move(handler));
    return progress();
}

std::vector<workflow::EventRule> FlowExecutor::effective_events(
    const workflow::Step &current) const {
    std::map<std::string, workflow::EventRule> inherited;
    const auto merge = [&](const workflow::Step &scope) {
        for (const auto &id : scope.disabled_events) inherited.erase(id);
        for (const auto &rule : scope.event_policy) inherited.insert_or_assign(rule.id, rule);
    };
    // 调用帧携带父步骤的事件范围；内层按ID覆盖，显式禁用优先于继承。
    for (std::size_t i = 0; i + 1 < stack_.size(); ++i) {
        const auto &frame = stack_[i];
        merge(program_.definitions.at(frame.definition).steps.at(frame.current));
    }
    merge(current);
    std::vector<workflow::EventRule> result;
    result.reserve(inherited.size());
    for (const auto &[id, rule] : inherited) {
        (void)id;
        result.push_back(rule);
    }
    return result;
}

TickResult FlowExecutor::select_next(Frame &frame, const workflow::Step &current) {
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
    if (auto event = check_events(frame, current, image, workflow::EventClass::Overlay))
        return *event;
    for (const auto &candidate_id : candidates) {
        const auto &candidate = definition.steps.at(candidate_id);
        if (frame.hits[candidate_id] >= candidate.max_hit) continue;
        std::optional<contracts::Observation> observed;
        if (candidate.guard) {
            observed = ports_.recognize(image, *candidate.guard);
            if (observed->outcome == contracts::RecognitionOutcome::Error)
                return fail(observed->error_code.empty() ? "RECOGNITION_ERROR" :
                            observed->error_code);
            if (observed->outcome == contracts::RecognitionOutcome::NoHit) continue;
        }
        frame.current = candidate_id;
        ++frame.hits[candidate_id];
        frame.entered_at = Clock::now();
        frame.next_pending = false;
        frame.error_pending = false;
        frame.selected_frame = image;
        frame.selected_observation = std::move(observed);
        return progress();
    }
    if (auto event = check_events(frame, current, image, workflow::EventClass::Encounter))
        return *event;
    return waiting(50ms);
}

contracts::Command FlowExecutor::command(const workflow::Input &input,
                                          const contracts::Observation &target) const {
    static const std::map<std::string, contracts::ActionKind> kinds{
        {"Click", contracts::ActionKind::Click},
        {"Swipe", contracts::ActionKind::Swipe},
        {"ClickKey", contracts::ActionKind::ClickKey}};
    const auto kind = input.command.at("kind").get<std::string>();
    const auto found = kinds.find(kind);
    require(found != kinds.end(), "FLOW_INPUT_KIND_INVALID");
    contracts::Command value;
    value.kind = found->second;
    value.x = input.command.value("x", 0);
    value.y = input.command.value("y", 0);
    value.x2 = input.command.value("x2", 0);
    value.y2 = input.command.value("y2", 0);
    value.duration = input.command.value("duration", 0);
    value.key = input.command.value("key", 0);
    if (input.use_target_center &&
        (value.kind == contracts::ActionKind::Click ||
         value.kind == contracts::ActionKind::Swipe)) {
        require(target.center.has_value(), "FLOW_TARGET_NOT_POSITIONAL");
        std::int64_t x = target.center->x;
        std::int64_t y = target.center->y;
        if (input.target_offset) {
            x += input.target_offset->x;
            y += input.target_offset->y;
            if (input.clip_target_to_area) {
                x = std::clamp(x, std::int64_t(input.allowed_area.x),
                    std::int64_t(input.allowed_area.x + input.allowed_area.width - 1));
                y = std::clamp(y, std::int64_t(input.allowed_area.y),
                    std::int64_t(input.allowed_area.y + input.allowed_area.height - 1));
            }
        }
        require(x >= std::numeric_limits<int>::min() && x <= std::numeric_limits<int>::max() &&
                y >= std::numeric_limits<int>::min() && y <= std::numeric_limits<int>::max(),
                "FLOW_TARGET_OVERFLOW");
        value.x = static_cast<int>(x);
        value.y = static_cast<int>(y);
    }
    return value;
}

TickResult FlowExecutor::execute_step(Frame &frame, const workflow::Step &current) {
    if (frame.event_exits.empty() && Clock::now() - frame.entered_at >= current.time_limit)
        return route_error(frame, current, "FLOW_STAGE_TIMEOUT");
    if (!frame.event_exits.empty()) {
        const auto image = ports_.capture();
        if (auto event = check_events(frame, current, image, workflow::EventClass::Overlay))
            return *event;
    }
    if (current.guard && !frame.selected_observation) {
        auto image = ports_.capture();
        if (auto event = check_events(frame, current, image, workflow::EventClass::Overlay))
            return *event;
        if (auto event = check_events(frame, current, image, workflow::EventClass::Encounter))
            return *event;
        auto observation = ports_.recognize(image, *current.guard);
        if (observation.outcome == contracts::RecognitionOutcome::Error)
            return fail(observation.error_code.empty() ? "RECOGNITION_ERROR" :
                        observation.error_code);
        if (observation.outcome == contracts::RecognitionOutcome::NoHit)
            return waiting(50ms);
        frame.selected_frame = std::move(image);
        frame.selected_observation = std::move(observation);
    }
    if (std::holds_alternative<workflow::Route>(current.data) ||
        std::holds_alternative<workflow::Observe>(current.data)) {
        advance(frame);
        return progress();
    }
    if (const auto *input = std::get_if<workflow::Input>(&current.data)) {
        if (frame.pending) return blocked("INPUT_UNRESOLVED");
        if (!frame.selected_frame) frame.selected_frame = ports_.capture();
        const auto &image = *frame.selected_frame;
        if (auto event = check_events(frame, current, image, workflow::EventClass::Overlay))
            return *event;
        const auto scene = ports_.recognize(image, input->scene);
        if (scene.outcome == contracts::RecognitionOutcome::NoHit) {
            frame.selected_frame.reset();
            frame.selected_observation.reset();
            if (auto event = check_events(frame, current, image, workflow::EventClass::Encounter))
                return *event;
            return waiting(50ms);
        }
        require_hit(scene, "SCENE_NOT_FOUND");
        const auto target = ports_.recognize(image, input->target);
        if (target.outcome == contracts::RecognitionOutcome::NoHit) {
            frame.selected_frame.reset();
            frame.selected_observation.reset();
            if (auto event = check_events(frame, current, image, workflow::EventClass::Encounter))
                return *event;
            return waiting(50ms);
        }
        require_hit(target, "TARGET_NOT_FOUND");
        require((!input->use_target_center || target.action_eligible) &&
                    same_device(scene.basis, target.basis) &&
                    scene.basis.frame_id == target.basis.frame_id,
                "INPUT_EVIDENCE_INVALID");
        const auto submitted = ports_.submit(command(*input, target), scene, target,
                                              input->allowed_area, current.source_path);
        if (submitted.state == SubmissionState::Unresolved)
            return blocked("INPUT_SUBMISSION_UNRESOLVED:" + submitted.detail);
        if (submitted.state == SubmissionState::Rejected)
            return fail("INPUT_REJECTED:" + submitted.detail);
        require(submitted.action_epoch > image.identity.action_epoch &&
                    submitted.submitted_at >= image.identity.captured_at,
                "INPUT_RECEIPT_INVALID");
        frame.pending = PendingInput{current.source_path, image.identity,
            submitted.action_epoch, submitted.submitted_at, {}};
        advance(frame);
        return progress();
    }
    if (const auto *await = std::get_if<workflow::AwaitResult>(&current.data)) {
        if (!frame.pending) return fail("AWAIT_WITHOUT_SUBMISSION");
        const auto now = Clock::now();
        const auto deadline = std::min(frame.pending->submitted_at + await->budget +
            frame.pending->event_pause, deadline_);
        if (frame.event_exits.empty() && now >= deadline) return fail("AWAIT_RESULT_TIMEOUT");
        if (now < frame.pending->submitted_at + await->initial_delay)
            return waiting(std::min(await->poll_interval,
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    frame.pending->submitted_at + await->initial_delay - now)));
        const auto image = ports_.capture();
        if (auto event = check_events(frame, current, image, workflow::EventClass::Overlay))
            return *event;
        const auto observed = ports_.recognize(image, await->condition);
        if (observed.outcome == contracts::RecognitionOutcome::Error)
            return fail(observed.error_code.empty() ? "AWAIT_RECOGNITION_ERROR" :
                        observed.error_code);
        if (observed.outcome == contracts::RecognitionOutcome::NoHit) {
            if (auto event = check_events(frame, current, image, workflow::EventClass::Encounter))
                return *event;
            return waiting(await->poll_interval);
        }
        require(same_device(observed.basis, frame.pending->before) &&
                    observed.basis.frame_id > frame.pending->before.frame_id &&
                    observed.basis.action_epoch >= frame.pending->action_epoch,
                "AWAIT_EVIDENCE_STALE");
        frame.pending.reset();
        advance(frame);
        return progress();
    }
    if (const auto *wait = std::get_if<workflow::Wait>(&current.data)) {
        if (Clock::now() < frame.entered_at + wait->duration)
            return waiting(25ms);
        advance(frame);
        return progress();
    }
    if (const auto *call = std::get_if<workflow::Call>(&current.data)) {
        require(stack_.size() < 8, "FLOW_CALL_DEPTH_LIMIT");
        const auto &child = program_.definitions.at(call->definition);
        Frame nested;
        nested.definition = child.id;
        nested.current = child.entry;
        nested.invoked_at = Clock::now();
        nested.entered_at = Clock::now();
        nested.hits[child.entry] = 1;
        stack_.push_back(std::move(nested));
        return progress();
    }
    if (std::holds_alternative<workflow::Return>(current.data)) {
        if (stack_.size() == 1) return fail("ROOT_RETURN_WITHOUT_FINISH");
        if (frame.pending) return blocked("CHILD_INPUT_UNRESOLVED");
        const auto ended = std::move(stack_.back());
        stack_.pop_back();
        auto &parent = stack_.back();
        const auto elapsed = Clock::now() - ended.invoked_at;
        parent.entered_at += elapsed;
        if (ended.event) {
            if (parent.pending) parent.pending->event_pause += elapsed;
            for (auto &[name, deadline] : parent.phase_deadlines) {
                (void)name;
                deadline += elapsed;
            }
            if (parent.exit_pause_started) *parent.exit_pause_started += elapsed;
            for (auto &exit : parent.event_exits) exit.deadline += elapsed;
            parent.selected_frame.reset();
            parent.selected_observation.reset();
            const auto now = Clock::now();
            if (parent.event_exits.empty()) parent.exit_pause_started = now;
            parent.event_exits.push_back(EventExit{ended.event->id, ended.event->detect,
                now + ended.event->exit_budget});
            if (ended.event->resume == workflow::ResumeMode::Replan) {
                if (parent.pending) return blocked("EVENT_REPLAN_WITH_PENDING_INPUT");
                parent.current = ended.event->replan_step;
                parent.next_pending = false;
                parent.selected_frame.reset();
                parent.selected_observation.reset();
            }
        } else advance(parent);
        return progress();
    }
    if (const auto *business = std::get_if<workflow::BusinessConfirm>(&current.data)) {
        const auto operation = ports_.operate("WvdConfirm", business->parameters,
            frame.selected_frame, frame.selected_observation, current.source_path);
        if (operation.state == OperationState::Done) {
            advance(frame);
            return progress();
        }
        if (operation.state == OperationState::Waiting) {
            frame.selected_frame.reset();
            frame.selected_observation.reset();
            return {TickState::Waiting, operation.wake_at, {}, current.source_path};
        }
        if (operation.state == OperationState::ExternalBlocked)
            return blocked(operation.detail);
        return route_error(frame, current, operation.detail);
    }
    if (const auto *operation = std::get_if<workflow::RegisteredOperation>(&current.data)) {
        if (operation->binding == "BeginObservationPhase") {
            const auto phase = operation->parameters.at("phase").get<std::string>();
            const auto budget = std::chrono::milliseconds{
                operation->parameters.at("budget_ms").get<std::int64_t>()};
            require(!phase.empty() && budget.count() > 0 &&
                    budget <= std::chrono::minutes{30}, "OBSERVATION_PHASE_INVALID");
            frame.phase_deadlines.try_emplace(phase, Clock::now() + budget);
            advance(frame);
            return progress();
        }
        if (operation->binding == "EndObservationPhase") {
            const auto phase = operation->parameters.at("phase").get<std::string>();
            require(frame.phase_deadlines.erase(phase) == 1, "OBSERVATION_PHASE_NOT_ACTIVE");
            advance(frame);
            return progress();
        }
        const auto result = ports_.operate(operation->binding, operation->parameters,
            frame.selected_frame, frame.selected_observation, current.source_path);
        if (result.state == OperationState::Done) {
            advance(frame);
            return progress();
        }
        if (result.state == OperationState::Waiting) {
            frame.selected_frame.reset();
            frame.selected_observation.reset();
            return {TickState::Waiting, result.wake_at, {}, current.source_path};
        }
        if (result.state == OperationState::ExternalBlocked)
            return blocked(result.detail);
        return route_error(frame, current, result.detail);
    }
    if (std::holds_alternative<workflow::Finish>(current.data)) {
        if (stack_.size() != 1 || has_unresolved_input()) return fail("ROOT_FINISH_INVALID");
        terminal_ = {TickState::Completed, {}, {}, current.source_path};
        return terminal_;
    }
    if (const auto *blocked_step = std::get_if<workflow::ExternalBlocked>(&current.data))
        return blocked(blocked_step->reason);
    if (const auto *failed = std::get_if<workflow::Fail>(&current.data))
        return fail(failed->reason);
    return fail("FLOW_STEP_UNKNOWN");
}
} // namespace wvd::runtime
