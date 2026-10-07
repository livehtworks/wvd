#include "flow_executor.hpp"
#include "platform/execution_timing.hpp"
#include <algorithm>
#include <limits>
#include <set>
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
    exception_restart_supported_ = std::any_of(root.events.begin(), root.events.end(),
        [](const workflow::EventRule &event) { return event.on_device_restart; });
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
            {"continuous_exception", {{"active", exception_since_.has_value()},
                {"elapsed_ms", exception_since_ ? std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - *exception_since_).count() : 0},
                {"restart_after_ms", observation_policy_.exception_timeout.count()}}},
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
            read_recovery_->started + read_recovery_->outage_limit) : deadline_);
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
contracts::Observation FlowExecutor::recognize_result(const contracts::FrameEnvelope &image,
    const recognition::Request &request) {
    const auto *custom = std::get_if<recognition::CustomParameters>(&request.parameters);
    if (!custom || custom->binding != "ConfirmedInputResult") return ports_.recognize(image, request);
    const auto &parameters = custom->parameters;
    require(parameters.value("mode", "") == "confirmed_input_result" &&
        parameters.contains("classification"), "CONFIRMED_RESULT_QUERY_INVALID");
    contracts::Observation result;
    result.basis = image.identity;
    result.recognizer_id = request.recognizer_id;
    result.parameter_revision = request.parameter_revision;
    result.action_eligible = false;
    result.outcome = contracts::RecognitionOutcome::NoHit;
    const auto &saved = stack_.back().confirmed_result;
    if (saved && !has_unresolved_input() && result_identity_matches(image.identity, saved->basis) &&
        image.identity.action_epoch == saved->action_epoch &&
        saved->classification == parameters.at("classification").get<std::string>() &&
        (!parameters.contains("source_path") || parameters.at("source_path") == saved->source_path)) {
        result.outcome = contracts::RecognitionOutcome::Hit;
        result.evidence = {{"historical_result", true}, {"classification", saved->classification},
            {"source_path", saved->source_path}, {"source_definition", saved->source_definition},
            {"confirmed_frame", saved->basis.frame_id}, {"action_epoch", saved->action_epoch}};
    }
    return result;
}
void FlowExecutor::consume_result(const recognition::Request &request) {
    const auto *custom = std::get_if<recognition::CustomParameters>(&request.parameters);
    if (custom && custom->binding == "ConfirmedInputResult" && custom->parameters.value("consume", false))
        stack_.back().confirmed_result.reset();
}
TickResult FlowExecutor::route_error(Frame &frame, const workflow::Step &current, std::string code) {
    if (code == "FLOW_STAGE_TIMEOUT" && std::holds_alternative<workflow::Poll>(current.data) &&
        !frame.error_pending && !frame.resume && frame.event_exits.empty() && !has_unresolved_input()) {
        invalidate_observation();
        const auto image = observation_frame();
        if (auto result = recheck_expired_poll(frame, current, image)) return *result;
    }
    // 有界等待耗尽前再给已知现场一次诊断机会；Error/取消/送达未知不走盲目恢复。
    if (!frame.error_pending && (code == "FLOW_STAGE_TIMEOUT" ||
        code == "FLOW_HIT_LIMIT_EXHAUSTED" || code == "EVENT_RESUME_UNCONFIRMED")) {
        const auto image = observation_frame();
        if (auto event = check_unexpected(frame, current, image, code, true)) return *event;
        if (auto scene = reclassify_timeout(frame, current, code)) return *scene;
    }
    if (!current.on_error.empty() && frame.pending && !frame.pending->delivery_unknown && frame.pending->attempts > 1 &&
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
                exception_since_.reset(); // An explicit guarded business failure edge now owns this known menu.
            }
        }
    }
    // Unknown scenes and unconfirmed effects retain the continuous recovery path.
    // Only the explicit, fresh same-menu failure route above can terminate its input.
    if (exception_since_ && (code == "FLOW_STAGE_TIMEOUT" || code == "AWAIT_RESULT_TIMEOUT" ||
        code == "EVENT_RESUME_UNCONFIRMED" || code == "FLOW_HIT_LIMIT_EXHAUSTED"))
        return waiting(250ms);
    if (frame.pending) return blocked("INPUT_RESULT_UNCONFIRMED:" + code);
    if (frame.error_pending || current.on_error.empty()) return fail(std::move(code));
    frame.next_pending = frame.error_pending = true;
    frame.selection_origin.reset();
    frame.selected_frame.reset();
    frame.selected_observation.reset();
    frame.delay_until.reset();
    frame.entered_at = Clock::now();
    return progress();
}
std::optional<TickResult> FlowExecutor::reclassify_timeout(Frame &frame,
    const workflow::Step &current, const std::string &code) {
    // 只读场景过期后按本子图声明的业务场景复核；不重走输入或动作节点。
    if (has_unresolved_input() || frame.error_pending || frame.resume || !frame.event_exits.empty() ||
        !(std::holds_alternative<workflow::Observe>(current.data) ||
          std::holds_alternative<workflow::Route>(current.data) ||
          std::holds_alternative<workflow::Poll>(current.data) ||
          std::holds_alternative<workflow::Wait>(current.data))) return std::nullopt;
    const auto &definition = program_.definitions.at(frame.definition);
    std::vector<const workflow::Step *> candidates;
    const auto include = [&](const workflow::Step &candidate) {
        if (candidate.id == current.id || !candidate.marks_known_scene ||
            frame.timeout_reclassifications.contains(candidate.id) ||
            candidate.unexpected_only || !candidate.guard ||
            candidate.business_guard || !std::holds_alternative<workflow::Observe>(candidate.data) ||
            (candidate.max_hit && frame.hits.contains(candidate.id) &&
                frame.hits.at(candidate.id) >= candidate.max_hit)) return;
        candidates.push_back(&candidate);
    };
    // Known-scene labels classify observations; only declared edges authorize recovery.
    std::vector<std::string> reachable = current.next;
    if (frame.returned_targets) reachable = *frame.returned_targets;
    else if (frame.selection_origin) {
        const auto &origin = *frame.selection_origin;
        const auto &predecessor = definition.steps.at(origin.predecessor);
        reachable = origin.returned_targets ? *origin.returned_targets :
            (origin.error_pending ? predecessor.on_error : predecessor.next);
    }
    for (const auto &id : current.on_error)
        if (std::find(reachable.begin(), reachable.end(), id) == reachable.end()) reachable.push_back(id);
    for (const auto &id : reachable) include(definition.steps.at(id));
    if (candidates.empty()) return std::nullopt;
    invalidate_observation();
    const auto image = observation_frame();
    for (const auto *candidate : candidates) {
        auto observed = recognize_result(image, *candidate->guard);
        if (observed.outcome == contracts::RecognitionOutcome::Error)
            return fail(observed.error_code.empty() ? "RECOGNITION_ERROR" : observed.error_code);
        if (observed.outcome != contracts::RecognitionOutcome::Hit) continue;
        if (candidate->marks_known_scene) record_known_scene(frame, observed);
        last_diagnostic_ = {{"reason", "timeout_scene_reclassified"},
            {"original_reason", code}, {"source_path", current.source_path},
            {"target", candidate->source_path}, {"frame_id", image.identity.frame_id}};
        const SelectionOrigin origin{current.id, frame.entered_at, frame.error_pending, reachable};
        frame.current = candidate->id;
        frame.entered_at = Clock::now();
        frame.next_pending = frame.error_pending = false;
        frame.returned_targets.reset();
        frame.selection_origin = origin;
        frame.selected_frame = image;
        frame.selected_observation = std::move(observed);
        frame.timeout_reclassifications.insert(candidate->id);
        frame.delay_until.reset();
        frame.poll_until.reset();
        if (candidate->max_hit > 0) ++frame.hits[candidate->id];
        return progress();
    }
    last_diagnostic_["final_scene_recheck"] = "no_declared_scene_matched";
    last_diagnostic_["original_reason"] = code;
    return std::nullopt;
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
        if (!read_recovery_ && exception_since_ &&
            Clock::now() - *exception_since_ >= observation_policy_.exception_timeout) {
            invalidate_observation();
            const auto image = observation_frame(); // 重启前保存本次真实帧，不能只留旧缓存。
            auto &active = stack_.back();
            const auto &current = step();
            if (!active.next_pending && !active.resume && active.pending &&
                std::holds_alternative<workflow::AwaitResult>(current.data)) {
                if (!active.pending->delivery_unknown)
                    if (auto event = check_events(active, current, image, workflow::EventClass::Overlay)) return *event;
                if (auto settled = settle_await_result(active, current, image)) return *settled;
            } else if (auto resumed = recheck_normal_observation(active, current, image)) return *resumed;
            if (ports_.cancelled()) {
                terminal_ = {TickState::Cancelled, {}, "CANCELLED", current_source_path()};
                return terminal_;
            }
            if (Clock::now() >= deadline_) return fail("FLOW_TOTAL_DEADLINE");
            last_diagnostic_ = {{"reason", "continuous_exception_game_restart"},
                {"source_path", current_source_path()}, {"frame_id", image.identity.frame_id},
                {"elapsed_ms", std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - *exception_since_).count()},
                {"limit_ms", observation_policy_.exception_timeout.count()}};
            exception_since_.reset();
            begin_observation_recovery({contracts::ReadFaultKind::ApplicationUnavailable,
                contracts::ReadFaultStage::Capture, "CONTINUOUS_EXCEPTION_TIMEOUT",
                "bound_game.restart", observation_policy_.exception_timeout, {}, last_diagnostic_});
            read_recovery_->restart_application = true;
            return observation_retry_wait();
        }
        // 恢复窗口只做只读采集；不重执行当前步骤，也不推进或清空父回执。
        if (read_recovery_) {
            if (Clock::now() >= read_recovery_->started + read_recovery_->outage_limit)
                return fail("OBSERVATION_RECOVERY_EXHAUSTED:" + read_recovery_->last.code);
            if (read_recovery_->suspended) return retry_observation();
        }
        ports_.observation_window(read_recovery_ ? std::min(deadline_,
            read_recovery_->started + read_recovery_->outage_limit) : deadline_);
        // A restart handler owns its own budget. Expired suspended calls must not
        // kill Boot, or prevent its explicit replan from discarding stale menus.
        if (stack_.back().resume) return resume_event(stack_.back(), step());
        for (std::size_t i = 0; i < stack_.size(); ++i) {
            const auto &frame = stack_[i];
            if (std::any_of(stack_.begin() + i + 1, stack_.end(),
                    [](const Frame &child) { return child.event.has_value(); })) continue;
            if (!exception_since_ && frame.invocation_deadline && Clock::now() >= *frame.invocation_deadline)
                return fail("FLOW_INVOCATION_TIMEOUT:" + frame.definition);
            for (const auto &[name, deadline] : frame.phase_deadlines)
                if (!exception_since_ && Clock::now() >= deadline) return fail("OBSERVATION_PHASE_TIMEOUT:" + name);
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
    // 异常框内按钮确认/弹窗消失不等于业务恢复，不能每次网络重试都重置60秒。
    const bool exception_scope = std::any_of(stack_.begin(), stack_.end(), [&](const Frame &active) {
        const auto &checks = program_.definitions.at(active.definition).checks;
        return (checks && checks->phase == "exception") ||
            (active.event && active.event->rule.category == workflow::EventClass::Exception &&
             !active.event->rule.on_device_restart);
    });
    if (!exception_scope) exception_since_.reset();
    if (last_diagnostic_.is_object() &&
        last_diagnostic_.value("source_path", "") == current_source_path())
        last_diagnostic_["outcome"] = "business_resumed";
    frame.no_progress_since.reset();
    frame.next_diagnostic_poll = {};
    frame.diagnostic_checked = false;
}
void FlowExecutor::advance(Frame &frame, bool observed_progress) {
    if (observed_progress) clear_unexpected(frame);
    frame.selection_origin.reset();
    frame.selected_frame.reset();
    frame.selected_observation.reset();
    frame.poll_until.reset();
    frame.next_pending = true;
    frame.error_pending = false;
    const auto delay = program_.definitions.at(frame.definition).steps.at(frame.current).delay_after;
    frame.delay_until = delay.count() ? std::optional{Clock::now() + delay} : std::nullopt;
}
TickResult FlowExecutor::reconsider_uncommitted_selection(Frame &frame) {
    // 新帧可能使只读分类或尚未发送的按钮失效；回到同一候选集重新分类。
    // 保留原分派期限和全部业务状态，有输入回执时绝不撤回选择或重发。
    if (frame.selection_origin && !frame.pending) {
        const auto origin = std::move(*frame.selection_origin);
        frame.selection_origin.reset();
        if (auto hit = frame.hits.find(frame.current); hit != frame.hits.end() && hit->second > 0)
            --hit->second;
        last_diagnostic_ = {{"reason", "selected_branch_no_longer_matches"},
            {"source_path", current_source_path()}, {"predecessor", origin.predecessor}};
        frame.current = origin.predecessor;
        frame.entered_at = origin.entered_at;
        frame.next_pending = true;
        frame.error_pending = origin.error_pending;
        frame.returned_targets = origin.returned_targets;
        frame.selected_frame.reset();
        frame.selected_observation.reset();
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
        const bool reconsiderable = std::holds_alternative<workflow::Input>(candidate.data) ||
            std::holds_alternative<workflow::Observe>(candidate.data) ||
            (candidate.guard && std::holds_alternative<workflow::Route>(candidate.data));
        if (reconsiderable && (candidates.size() > 1 || frame.returned_targets.has_value()) && !frame.pending)
            frame.selection_origin = SelectionOrigin{previous, entered, was_error, frame.returned_targets};
        else frame.selection_origin.reset();
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
            observed = recognize_result(acquire_image(), *candidate.guard);
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
                exception_since_.reset(); // 已声明正常进行中的战斗/移动不属于异常。
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
    value.click_pair_interval_ms = input.command.value("click_pair_interval_ms", 0);
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
std::optional<TickResult> FlowExecutor::settle_await_result(Frame &frame,
    const workflow::Step &current, const contracts::FrameEnvelope &image) {
    require(frame.pending.has_value(), "AWAIT_WITHOUT_SUBMISSION");
    const auto &await = std::get<workflow::AwaitResult>(current.data);
    const auto observed = ports_.recognize(image, await.condition);
    if (ports_.cancelled()) {
        terminal_ = {TickState::Cancelled, {}, "CANCELLED", current_source_path()};
        return terminal_;
    }
    if (Clock::now() >= deadline_) return fail("FLOW_TOTAL_DEADLINE");
    if (observed.outcome == contracts::RecognitionOutcome::NoHit) return std::nullopt;
    require_hit(observed, "AWAIT_RECOGNITION_ERROR");
    require(same_device(observed.basis, image.identity) &&
        observed.basis.frame_id == image.identity.frame_id &&
        observed.basis.action_epoch == image.identity.action_epoch &&
        result_identity_matches(observed.basis, frame.pending->before) &&
        observed.basis.frame_id > frame.pending->before.frame_id &&
        observed.basis.action_epoch >= frame.pending->action_epoch, "AWAIT_EVIDENCE_STALE");
    // 通道清理未确认时不修改计数或释放回执；真实结果不能掩盖未知送达状态。
    if (frame.pending->delivery_unknown && !ports_.settle_observed_input())
        return blocked("OBSERVED_RESULT_INPUT_CLEANUP_UNCONFIRMED");
    // 通道回收也可能等待；取消/硬总期限不能在这段等待后被成功结果覆盖。
    if (ports_.cancelled()) {
        terminal_ = {TickState::Cancelled, {}, "CANCELLED", current_source_path()};
        return terminal_;
    }
    if (Clock::now() >= deadline_) return fail("FLOW_TOTAL_DEADLINE");
    const auto &submitted = program_.definitions.at(frame.definition).steps.at(frame.pending->submitted_step);
    if (submitted.consecutive_input_limit) {
        frame.hits.erase(submitted.id);
        frame.hits.erase(current.id);
    }
    report_input_result(*frame.pending, "confirmed", observed.basis.frame_id);
    if (const auto metadata = observed.evidence.find("confirmed_result"); metadata != observed.evidence.end()) {
        require(metadata->is_object() && metadata->contains("classification") &&
            metadata->at("classification").is_string(), "CONFIRMED_RESULT_METADATA_INVALID");
        auto classification = metadata->at("classification").get<std::string>();
        require(!classification.empty() && classification.size() <= 64, "CONFIRMED_RESULT_METADATA_INVALID");
        frame.confirmed_result = ConfirmedInputResult{std::move(classification),
            frame.pending->source_path, frame.definition, observed.basis, frame.pending->action_epoch};
    }
    frame.pending.reset();
    finish_observation_recovery("original_result_confirmed");
    advance(frame);
    // 仅正向结清允许后继开始选路，旧 Await 的局部期限不再截断新阶段。
    frame.entered_at = Clock::now();
    return progress();
}

std::optional<TickResult> FlowExecutor::recheck_expired_poll(Frame &frame,
    const workflow::Step &current, const contracts::FrameEnvelope &image) {
    const auto *poll = std::get_if<workflow::Poll>(&current.data);
    // This is a read-only final dispatch of the CURRENT Poll, not a graph-wide replan.
    if (!poll || frame.error_pending || frame.resume || !frame.event_exits.empty() ||
        has_unresolved_input() || Clock::now() - frame.entered_at < current.time_limit ||
        std::any_of(stack_.begin(), stack_.end(), [&](const Frame &active) {
            const auto &checks = program_.definitions.at(active.definition).checks;
            return (checks && checks->phase == "exception") ||
                (active.event && active.event->rule.category == workflow::EventClass::Exception &&
                 !active.event->rule.on_device_restart);
        })) return std::nullopt;
    const auto interrupted = [&]() -> std::optional<TickResult> {
        if (ports_.cancelled()) {
            terminal_ = {TickState::Cancelled, {}, "CANCELLED", current_source_path()};
            return terminal_;
        }
        if (Clock::now() >= deadline_) return fail("FLOW_TOTAL_DEADLINE");
        return std::nullopt;
    };
    if (auto result = interrupted()) return result;
    const auto verify = [&](const recognition::Request &condition) {
        auto observed = recognize_result(image, condition);
        if (observed.outcome == contracts::RecognitionOutcome::Error)
            require_hit(observed, "EXPIRED_POLL_RECOGNITION_ERROR");
        require(same_device(observed.basis, image.identity) &&
            observed.basis.frame_id == image.identity.frame_id &&
            observed.basis.action_epoch == image.identity.action_epoch,
            "EXPIRED_POLL_EVIDENCE_STALE");
        return observed;
    };
    const auto &definition = program_.definitions.at(frame.definition);
    const auto candidates = frame.returned_targets.value_or(current.next);
    // Do not jump over an unexecuted action, call, business operation or control node.
    // Those shapes keep their existing recovery path and are not covered by this repair.
    for (const auto &id : candidates) {
        const auto &candidate = definition.steps.at(id);
        if (candidate.unexpected_only) continue;
        if (candidate.business_guard ||
            (!std::holds_alternative<workflow::Observe>(candidate.data) &&
             !std::holds_alternative<workflow::Poll>(candidate.data))) return std::nullopt;
    }
    if (auto event = check_events(frame, current, image, workflow::EventClass::Overlay)) return event;
    if (auto result = interrupted()) return result;
    for (const auto &id : candidates) {
        const auto &candidate = definition.steps.at(id);
        const auto *observe = std::get_if<workflow::Observe>(&candidate.data);
        if (!observe || candidate.unexpected_only ||
            (candidate.max_hit && frame.hits.contains(id) && frame.hits.at(id) >= candidate.max_hit)) continue;
        if (candidate.guard) {
            const auto allowed = verify(*candidate.guard);
            if (auto result = interrupted()) return result;
            if (allowed.outcome != contracts::RecognitionOutcome::Hit) continue;
        }
        auto observed = verify(observe->request);
        if (auto result = interrupted()) return result;
        if (observed.outcome != contracts::RecognitionOutcome::Hit) continue;
        if (candidate.marks_known_scene) record_known_scene(frame, observed);
        // If the new read-only selection becomes invalid, restore the expired Poll
        // and its ORIGINAL deadline/candidate source; do not grant a fresh wait window.
        frame.selection_origin = SelectionOrigin{current.id, frame.entered_at,
            frame.error_pending, frame.returned_targets};
        frame.current = id;
        frame.entered_at = Clock::now();
        frame.next_pending = frame.error_pending = false;
        frame.returned_targets.reset();
        frame.delay_until.reset(); frame.poll_until.reset(); frame.known_wait = false;
        frame.selected_frame = image; frame.selected_observation = std::move(observed);
        if (candidate.max_hit) ++frame.hits[id];
        clear_unexpected(frame);
        last_diagnostic_ = {{"reason", "expired_poll_successor_selected"},
            {"source_path", current.source_path}, {"target", candidate.source_path},
            {"frame_id", image.identity.frame_id}};
        return progress();
    }
    // A known background must not suppress registered error/encounter handlers.
    if (auto event = check_unexpected(frame, current, image, "poll_no_progress_timeout", true)) return event;
    if (auto result = interrupted()) return result;
    if (!poll->ongoing) return std::nullopt;
    const auto ongoing = verify(*poll->ongoing);
    if (auto result = interrupted()) return result;
    if (ongoing.outcome != contracts::RecognitionOutcome::Hit) return std::nullopt;
    if (poll->progress) {
        const auto changed = verify(*poll->progress);
        if (auto result = interrupted()) return result;
        if (changed.outcome == contracts::RecognitionOutcome::Hit) {
            // Only the already-declared progress evidence renews the local window.
            // Invocation budgets, total wall-clock limit and input receipts are untouched.
            frame.entered_at = Clock::now();
            frame.known_wait = true;
            clear_unexpected(frame);
            record_known_scene(frame, ongoing);
            last_diagnostic_ = {{"reason", "expired_poll_progress_observed"},
                {"source_path", current.source_path}, {"frame_id", image.identity.frame_id}};
            return waiting(poll->interval);
        }
    }
    if (current.on_error.empty()) return std::nullopt;
    // Dispatch the existing explicit no-progress edge, not a success edge.
    // No input or business confirmation is executed in this helper.
    frame.next_pending = frame.error_pending = true;
    frame.returned_targets.reset(); frame.selection_origin.reset();
    frame.selected_frame.reset(); frame.selected_observation.reset();
    frame.delay_until.reset(); frame.poll_until.reset(); frame.known_wait = false;
    frame.entered_at = Clock::now();
    frame.no_progress_since.reset(); frame.next_diagnostic_poll = {};
    frame.diagnostic_checked = false;
    exception_since_.reset(); // The declared failure edge now owns this known stalled scene.
    last_diagnostic_ = {{"reason", "expired_poll_no_progress_error_route"},
        {"source_path", current.source_path}, {"frame_id", image.identity.frame_id}};
    return progress();
}

std::optional<TickResult> FlowExecutor::recheck_normal_observation(Frame &frame,
    const workflow::Step &current, const contracts::FrameEnvelope &image) {
    if (has_unresolved_input() || frame.resume || frame.error_pending ||
        std::any_of(stack_.begin(), stack_.end(), [&](const Frame &active) {
            const auto &checks = program_.definitions.at(active.definition).checks;
            return (checks && checks->phase == "exception") ||
                (active.event && active.event->rule.category == workflow::EventClass::Exception &&
                 !active.event->rule.on_device_restart);
        })) return std::nullopt;
    if (std::holds_alternative<workflow::Poll>(current.data) &&
        Clock::now() - frame.entered_at >= current.time_limit)
        return recheck_expired_poll(frame, current, image);
    const auto &definition = program_.definitions.at(frame.definition);
    const auto verify = [&](const recognition::Request &condition) {
        auto observed = recognize_result(image, condition);
        if (observed.outcome == contracts::RecognitionOutcome::Error)
            require_hit(observed, "NORMAL_RECHECK_ERROR");
        if (observed.outcome == contracts::RecognitionOutcome::Hit)
            require(same_device(observed.basis, image.identity) &&
                observed.basis.frame_id == image.identity.frame_id &&
                observed.basis.action_epoch == image.identity.action_epoch, "NORMAL_RECHECK_EVIDENCE_STALE");
        return observed;
    };
    const auto check = [&](const workflow::Step &candidate) -> std::optional<TickResult> {
        const bool entering = frame.next_pending || candidate.id != frame.current;
        if (candidate.unexpected_only || candidate.business_guard ||
            (entering && candidate.max_hit && frame.hits.contains(candidate.id) &&
             frame.hits.at(candidate.id) >= candidate.max_hit)) return std::nullopt;
        if (const auto *observe = std::get_if<workflow::Observe>(&candidate.data)) {
            // guard 只证明允许进入，不等于 Observe 本身的业务结果；二者都须成立。
            if (candidate.guard && verify(*candidate.guard).outcome != contracts::RecognitionOutcome::Hit)
                return std::nullopt;
            const auto observed = verify(observe->request);
            if (observed.outcome != contracts::RecognitionOutcome::Hit) return std::nullopt;
            if (ports_.cancelled()) {
                terminal_ = {TickState::Cancelled, {}, "CANCELLED", current_source_path()};
                return terminal_;
            }
            if (Clock::now() >= deadline_) return fail("FLOW_TOTAL_DEADLINE");
            // 只选原阶段声明的只读目标，实际 Observe 仍在下一 tick 复核同帧。
            frame.current = candidate.id;
            frame.next_pending = false;
            frame.returned_targets.reset();
            frame.entered_at = Clock::now();
            frame.delay_until.reset();
            frame.poll_until.reset();
            frame.selection_origin.reset();
            frame.selected_frame = image;
            frame.selected_observation = observed;
            if (entering && candidate.max_hit) ++frame.hits[candidate.id];
            clear_unexpected(frame);
            return progress();
        }
        if (const auto *poll = std::get_if<workflow::Poll>(&candidate.data); poll && poll->ongoing) {
            const auto observed = verify(*poll->ongoing);
            if (observed.outcome == contracts::RecognitionOutcome::Hit) {
                const bool expired = Clock::now() - frame.entered_at >= current.time_limit;
                if (expired) {
                    // A fallback Poll cannot clear a timed-out Route merely because
                    // its background is still visible. Require real declared progress.
                    if (!poll->progress) return std::nullopt;
                    const auto changed = verify(*poll->progress);
                    if (changed.outcome != contracts::RecognitionOutcome::Hit) return std::nullopt;
                }
                if (ports_.cancelled()) {
                    terminal_ = {TickState::Cancelled, {}, "CANCELLED", current_source_path()};
                    return terminal_;
                }
                if (Clock::now() >= deadline_) return fail("FLOW_TOTAL_DEADLINE");
                if (expired) frame.entered_at = Clock::now();
                clear_unexpected(frame);
                frame.known_wait = true;
                record_known_scene(frame, observed);
                return waiting(poll->interval);
            }
        }
        return std::nullopt;
    };
    if (!frame.next_pending) return check(current);
    const auto &candidates = frame.returned_targets ? *frame.returned_targets : current.next;
    for (const auto &id : candidates) {
        const auto &candidate = definition.steps.at(id);
        // 不跨过尚未执行的动作/调用去找远处页面，更不扫全图或父业务。
        if (!std::holds_alternative<workflow::Observe>(candidate.data) &&
            !std::holds_alternative<workflow::Poll>(candidate.data)) break;
        if (auto result = check(candidate)) return result;
    }
    return std::nullopt;
}

TickResult FlowExecutor::execute_step(Frame &frame, const workflow::Step &current) {
    if (frame.event_exits.empty() && Clock::now() - frame.entered_at >= current.time_limit) {
        if (exception_since_ && frame.pending && std::holds_alternative<workflow::AwaitResult>(current.data)) {
            invalidate_observation();
            const auto image = observation_frame();
            if (!frame.pending->delivery_unknown)
                if (auto event = check_events(frame, current, image, workflow::EventClass::Overlay)) return *event;
            if (auto settled = settle_await_result(frame, current, image)) return *settled;
        }
        return route_error(frame, current, "FLOW_STAGE_TIMEOUT");
    }
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
    if (current.guard && !frame.selected_observation && !std::holds_alternative<workflow::Fail>(current.data)) {
        const auto image = observation_frame();
        if (auto event = check_events(frame, current, image, workflow::EventClass::Overlay)) return *event;
        auto observed = recognize_result(image, *current.guard);
        if (observed.outcome == contracts::RecognitionOutcome::Error)
            return fail(observed.error_code.empty() ? "RECOGNITION_ERROR" : observed.error_code);
        if (observed.outcome == contracts::RecognitionOutcome::NoHit) {
            if (auto event = check_unexpected(frame, current, image, "step_guard_not_confirmed")) return *event;
            if (std::holds_alternative<workflow::Input>(current.data) ||
                std::holds_alternative<workflow::Observe>(current.data) ||
                std::holds_alternative<workflow::Route>(current.data))
                return reconsider_uncommitted_selection(frame);
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
        const auto result = recognize_result(image, observe->request);
        if (result.outcome == contracts::RecognitionOutcome::NoHit) {
            frame.selected_frame.reset(); frame.selected_observation.reset();
            if (auto event = check_unexpected(frame, current, image, "observation_not_confirmed")) return *event;
            return reconsider_uncommitted_selection(frame);
        }
        require_hit(result, "OBSERVE_UNCONFIRMED");
        consume_result(observe->request);
        advance(frame); return progress();
    }
    if (const auto *input = std::get_if<workflow::Input>(&current.data)) {
        if (frame.pending) return blocked("INPUT_UNRESOLVED");
        for (const auto *request : {&input->scene, &input->target}) {
            const auto *custom = std::get_if<recognition::CustomParameters>(&request->parameters);
            if (custom && custom->binding == "ConfirmedInputResult")
                return fail("HISTORICAL_RESULT_CANNOT_AUTHORIZE_INPUT");
        }
        if (ports_.prepare_input()) {
            invalidate_observation();
            return reconsider_uncommitted_selection(frame);
        }
        frame.selected_frame = observation_frame();
        // 本轮持有 FrameEnvelope 的副本（像素共享），reset 缓存不会销毁事件识别的载荷。
        const auto image = *frame.selected_frame;
        if (auto event = check_events(frame, current, image, workflow::EventClass::Overlay)) return *event;
        const auto scene = ports_.recognize(image, input->scene);
        if (scene.outcome == contracts::RecognitionOutcome::NoHit) {
            frame.selected_frame.reset(); frame.selected_observation.reset();
            if (auto event = check_unexpected(frame, current, image, "input_scene_not_confirmed")) return *event;
            return reconsider_uncommitted_selection(frame);
        }
        require_hit(scene, "SCENE_NOT_FOUND");
        const auto target = ports_.recognize(image, input->target);
        if (target.outcome == contracts::RecognitionOutcome::NoHit) {
            frame.selected_frame.reset(); frame.selected_observation.reset();
            if (auto event = check_unexpected(frame, current, image, "input_target_not_confirmed")) return *event;
            return reconsider_uncommitted_selection(frame);
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
                return reconsider_uncommitted_selection(frame);
            }
            return fail("INPUT_REJECTED:" + submitted.detail);
        }
        // 先留下实际副作用事实，再验证回执；任何后续失败都不能丢掉它。
        for (auto &scope : stack_) scope.confirmed_result.reset();
        frame.pending = PendingInput{current.source_path, image.identity, submitted.action_epoch,
            submitted.submitted_at, {}, std::move(expected), result_budget,
            submitted.state == SubmissionState::Unresolved, current.id, submitted.submitted_at};
        frame.pending->selection_origin = frame.selection_origin;
        if (!input->effect_binding.empty())
            ports_.operate(input->effect_binding, {{"phase", "submitted"},
                {"delivery_unknown", frame.pending->delivery_unknown}}, std::nullopt, std::nullopt, current.source_path);
        if (submitted.state == SubmissionState::Unresolved) {
            advance(frame, false);
            return waiting(250ms); // 后置条件可以证明完成，未知送达本身不能授权重发。
        }
        require(submitted.action_epoch > image.identity.action_epoch &&
                submitted.submitted_at >= image.identity.captured_at, "INPUT_RECEIPT_INVALID");
        advance(frame, false); return progress(); // 已发送不等于页面已恢复，计时等新帧回执。
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
        if (auto settled = settle_await_result(frame, current, image)) return *settled;
        if (!frame.pending->delivery_unknown)
            if (auto event = check_unexpected(frame, current, image, "input_result_not_confirmed", now >= deadline)) return *event;
        if (frame.event_exits.empty() && now >= deadline) return route_error(frame, current, "AWAIT_RESULT_TIMEOUT");
        if (auto retried = retry_pending_input(frame, current, image)) return *retried;
        return waiting(await->poll_interval);
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
        frame.confirmed_result.reset();
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
        // A normal return sends no input. Preserve the child's result frame so
        // the caller can consume transient outcomes. Reuse still checks device,
        // generation, action epoch and age; event returns must reobserve.
        if (ended.event) invalidate_observation();
        auto &parent = stack_.back();
        parent.selected_frame.reset(); parent.selected_observation.reset();
        if (ended.event) {
            const auto &event = ended.event->rule;
            if (!event.on_device_restart)
                parent.event_exits.push_back({event.id, event.detect, Clock::now() + event.exit_budget});
            if (event.resume == workflow::ResumeMode::Replan)
                parent.resume = PendingResume{event, ended.event->owner};
        } else {
            parent.confirmed_result = std::move(ended.confirmed_result);
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
        if (result.state == OperationState::Done) {
            if (business) consume_result(business->condition);
            advance(frame); return progress();
        }
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
        if (current.guard) {
            // Event return invalidates the old failure evidence. Recheck even
            // when selection had a Hit; only declared edges may resume work.
            const auto observed = recognize_result(image, *current.guard);
            if (observed.outcome == contracts::RecognitionOutcome::Error)
                return fail(observed.error_code.empty() ? "RECOGNITION_ERROR" : observed.error_code);
            if (observed.outcome == contracts::RecognitionOutcome::NoHit) {
                if (has_unresolved_input()) return blocked("RECOVERY_RECHECK_INPUT_UNRESOLVED");
                last_diagnostic_ = {{"reason", "recovery_condition_cleared"},
                    {"original_reason", value->reason}, {"source_path", current.source_path},
                    {"frame_id", image.identity.frame_id}};
                advance(frame, false);
                return progress();
            }
        }
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
        {"outage_limit_ms", read.outage_limit.count()},
        {"started_at_ns", std::chrono::duration_cast<std::chrono::nanoseconds>(read.started.time_since_epoch()).count()},
        {"command_details", read.last.details},
        {"context_recovery", read.context_recovery},
        {"application_restarted_in_window", read.application_restarted_in_window},
        {"reconnect_count", reconnects_.size()},
        {"source_path", current_source_path()}, {"input_replayed", false}};
}
void FlowExecutor::begin_observation_recovery(const contracts::ReadFault &fault) {
    const auto now = Clock::now();
    if (!read_recovery_) {
        // accounted_at_ 在本次读取前设置；第一次阻塞读取的耗时也属于故障区间。
        read_recovery_ = ReadRecovery{std::min(accounted_at_, now), now, 0, fault};
        read_recovery_->outage_limit = observation_policy_.outage_limit;
        read_recovery_->source_path = current_source_path();
        read_recovery_->stack_depth = stack_.size();
    }
    auto &read = *read_recovery_;
    read.suspended = true;
    read.awaiting_input_validation = read.awaiting_input_validation ||
        fault.stage == contracts::ReadFaultStage::InputContext;
    read.last = fault;
    if ((fault.code == "DEVICE_INSTANCE_STARTING" ||
         fault.code == "DEVICE_INSTANCE_RESTART_REQUIRED") &&
        fault.kind == contracts::ReadFaultKind::TransportUnavailable)
        read.outage_limit = std::max(read.outage_limit, std::chrono::milliseconds{180000});
    if (fault.kind == contracts::ReadFaultKind::TransportUnavailable ||
        fault.kind == contracts::ReadFaultKind::ApplicationUnavailable) read.device_checked = false;
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
    const auto limit = std::min(deadline_, read.started + read.outage_limit);
    if (Clock::now() >= limit)
        return fail("OBSERVATION_RECOVERY_EXHAUSTED:" + read.last.code);
    invalidate_observation();
    return {TickState::Waiting, std::min(read.next_attempt, limit),
            "OBSERVATION_RETRY", current_source_path()};
}
TickResult FlowExecutor::retry_observation() {
    require(read_recovery_ && read_recovery_->suspended, "OBSERVATION_RECOVERY_NOT_ACTIVE");
    if (Clock::now() < read_recovery_->next_attempt) return observation_retry_wait();
    ports_.observation_window(std::min(deadline_, read_recovery_->started + read_recovery_->outage_limit));
    if (!read_recovery_->device_checked) {
        try {
            const auto recovery = ports_.recover_observation(read_recovery_->restart_application);
            // 已完成的一次强制重启必须即时消费。首张游戏帧再失焦/断线只恢复观察，
            // 不因设备适配层已释放本轮标志而重复 force-stop 刚刚拉起的应用。
            if (recovery.application_restarted) read_recovery_->restart_application = false;
            read_recovery_->application_restarted_in_window |= recovery.application_restarted;
            if (const auto &proof = recovery.reconnect) {
                require(proof->after > proof->before && !proof->created_identity.empty(), "RECONNECT_PROOF_INVALID");
                reconnects_.push_back(*proof);
            }
            restart_handler_pending_ = restart_handler_pending_ || recovery.application_restarted;
            read_recovery_->context_recovery = {{"application_restarted", recovery.application_restarted},
                {"foreground_restored", recovery.foreground_restored}, {"reconnected", recovery.reconnect.has_value()}};
            read_recovery_->device_checked = true;
        } catch (const contracts::ObservationUnavailable &error) {
            begin_observation_recovery(error.fault());
            return observation_retry_wait();
        }
    }
    invalidate_observation();
    (void)observation_frame(); // 只读；不派发剧情、补给或任何游戏输入。
    account_event_time();
    if (Clock::now() >= std::min(deadline_, read_recovery_->started + read_recovery_->outage_limit))
        return fail("OBSERVATION_RECOVERY_EXHAUSTED:" + read_recovery_->last.code);
    if (restart_handler_pending_) {
        // 技能等输入已有明确的中断保护。重启后的菜单/战斗底图不能证明旧动作成功；
        // 保留全部 pending 与业务账目，沿原保护原因停止，不进入菜单重选或 Boot 输入。
        for (const auto &active : stack_) {
            if (!active.pending) continue;
            const auto &source = program_.definitions.at(active.definition).steps.at(active.pending->submitted_step);
            const auto &input = std::get<workflow::Input>(source.data);
            if (input.interruption_reason.empty()) continue;
            last_diagnostic_ = {{"reason", "application_restart_protected_input"},
                {"source_path", active.pending->source_path}, {"definition", active.definition},
                {"interrupted_step", active.pending->submitted_step},
                {"interruption_reason", input.interruption_reason},
                {"delivery_unknown", active.pending->delivery_unknown},
                {"action_epoch", active.pending->action_epoch}};
            read_recovery_->awaiting_input_validation = false;
            finish_observation_recovery("application_restarted_input_protected");
            restart_handler_pending_ = false;
            return blocked(input.interruption_reason);
        }
        // 普通菜单的旧页面许可随应用重启失效；恢复原候选集而非重启根业务。
        // 根业务可能已记录“准备跳轮”，清掉或重跑都会混淆真实副作用。
        // 实际跳轮/付款、送达未知仍保留原回执，走Boot后核对原后置条件。
        const bool menu_only = std::all_of(stack_.begin(), stack_.end(), [&](const Frame &frame) {
            if (!frame.pending) return true;
            const auto &pending = *frame.pending;
            const auto &source = program_.definitions.at(frame.definition).steps.at(pending.submitted_step);
            const auto &input = std::get<workflow::Input>(source.data);
            return !pending.delivery_unknown && input.retry &&
                (input.effect_binding.empty() || !input.retry->restart_from.empty());
        });
        const auto &root = program_.definitions.at(program_.root_definition);
        const auto rule = std::find_if(root.events.begin(), root.events.end(),
            [](const workflow::EventRule &event) { return event.on_device_restart; });
        require(rule != root.events.end(), "RESTART_OBSERVATION_HANDLER_MISSING");
        if (menu_only) {
            for (auto &frame : stack_) {
                if (!frame.pending) continue;
                report_input_result(*frame.pending, "interrupted_by_application_restart", 0);
                const auto pending = std::move(*frame.pending);
                frame.pending.reset();
                const auto &input = std::get<workflow::Input>(program_.definitions.at(frame.definition).steps.at(pending.submitted_step).data);
                const auto &restart_from = input.retry->restart_from;
                frame.current = !restart_from.empty() ? restart_from :
                    (pending.selection_origin ? pending.selection_origin->predecessor : pending.submitted_step);
                frame.entered_at = restart_from.empty() && pending.selection_origin ? pending.selection_origin->entered_at : Clock::now();
                // next_pending只重新检查前驱的候选，不执行前驱的确认/输入。
                frame.next_pending = restart_from.empty() && pending.selection_origin.has_value();
                frame.error_pending = restart_from.empty() && pending.selection_origin && pending.selection_origin->error_pending;
                frame.returned_targets = restart_from.empty() && pending.selection_origin
                    ? pending.selection_origin->returned_targets : std::nullopt;
                frame.selection_origin.reset();
                frame.selected_frame.reset(); frame.selected_observation.reset();
                frame.delay_until.reset(); frame.poll_until.reset();
                last_diagnostic_ = {{"reason", "application_restart_menu_reclassified"},
                    {"source_path", pending.source_path}, {"definition", frame.definition},
                    {"interrupted_step", pending.submitted_step}, {"resume_step", frame.current}};
            }
        }
        // 游戏重启后旧弹窗处理器已失效。仅回收无受保护回执的事件栈，保留业务调用栈。
        const auto active_event = std::find_if(stack_.begin(), stack_.end(),
            [](const Frame &frame) { return frame.event.has_value(); });
        if (active_event != stack_.end() &&
            std::none_of(active_event, stack_.end(), [](const Frame &frame) { return frame.pending.has_value(); }))
            stack_.erase(active_event, stack_.end());
        require(stack_.size() < 8, "EVENT_DEPTH_LIMIT");
        const auto &definition = program_.definitions.at(rule->handler_definition);
        Frame handler;
        handler.definition = definition.id; handler.current = definition.entry;
        handler.invoked_at = handler.entered_at = Clock::now();
        if (definition.cumulative_budget) handler.invocation_deadline = handler.invoked_at + *definition.cumulative_budget;
        handler.event = ScopedEvent{*rule, 0};
        read_recovery_->awaiting_input_validation = false;
        finish_observation_recovery(menu_only ? "application_restarted_menu_reclassified" : "device_reconnected");
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
