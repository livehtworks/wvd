#include "flow_executor.hpp"
#include "platform/execution_timing.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace wvd::runtime {
namespace {
using namespace std::chrono_literals;
nlohmann::json request_key(const recognition::Request &request) {
    nlohmann::json key{{"id", request.recognizer_id}, {"revision", request.parameter_revision},
        {"roi", {request.roi.x, request.roi.y, request.roi.width, request.roi.height}}};
    if (const auto *custom = std::get_if<recognition::CustomParameters>(&request.parameters))
        key["parameters"] = {{"binding", custom->binding}, {"value", custom->parameters}};
    else if (const auto *image = std::get_if<recognition::TemplateParameters>(&request.parameters))
        key["parameters"] = {{"image", image->image}, {"threshold", image->threshold}};
    else key["parameters"] = std::get<recognition::OcrParameters>(request.parameters).expected_text;
    return key;
}
}

void FlowExecutor::account_event_time() {
    const auto now = Clock::now();
    const auto elapsed = now - accounted_at_;
    accounted_at_ = now;
    for (std::size_t i = 0; i < stack_.size(); ++i) {
        auto &frame = stack_[i];
        const bool descendant_event = std::any_of(stack_.begin() + i + 1, stack_.end(),
            [](const Frame &child) { return child.event.has_value() || !child.event_exits.empty(); });
        if (!descendant_event && frame.event_exits.empty()) continue;
        // 使用布尔“处于暂停区间”，不按事件深度乘 elapsed。
        frame.entered_at += elapsed;
        if (frame.input_selection) frame.input_selection->entered_at += elapsed;
        frame.paused_event_time += elapsed;
        if (frame.invocation_deadline) *frame.invocation_deadline += elapsed;
        if (frame.pending) frame.pending->event_pause += elapsed;
        if (frame.delay_until) *frame.delay_until += elapsed;
        for (auto &[name, deadline] : frame.phase_deadlines) { (void)name; deadline += elapsed; }
        if (descendant_event)
            for (auto &exit : frame.event_exits) exit.deadline += elapsed;
    }
}
std::vector<FlowExecutor::ScopedEvent> FlowExecutor::effective_events(const workflow::Step &current) const {
    std::map<std::string, ScopedEvent> inherited;
    const auto merge = [&](const workflow::Step &scope, std::size_t owner) {
        for (const auto &id : scope.disabled_events) inherited.erase(id);
        for (const auto &rule : scope.event_policy) inherited.insert_or_assign(rule.id, ScopedEvent{rule, owner});
    };
    for (std::size_t i = 0; i + 1 < stack_.size(); ++i)
        merge(program_.definitions.at(stack_[i].definition).steps.at(stack_[i].current), i);
    merge(current, stack_.size() - 1);
    std::vector<ScopedEvent> result;
    for (const auto &[id, rule] : inherited) { (void)id; result.push_back(rule); }
    std::stable_sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
        return a.rule.priority > b.rule.priority;
    });
    return result;
}
std::optional<TickResult> FlowExecutor::check_unexpected(Frame &frame, const workflow::Step &current,
    const contracts::FrameEnvelope &image, const std::string &reason, bool force) {
    if (!frame.no_progress_since) frame.no_progress_since = Clock::now();
    // 一帧 NoHit 常是动画/加载，不是异常。这里只延迟诊断，不缩短原业务等待预算。
    const auto now = Clock::now();
    const bool diagnose = force || (now - *frame.no_progress_since >= 1s && now >= frame.next_diagnostic_poll);
    frame.diagnostic_checked = diagnose;
    if (diagnose) frame.next_diagnostic_poll = now + 1s;
    last_diagnostic_ = {{"source_path", current.source_path}, {"reason", reason},
        {"business_group", current.check_group}, {"checked_groups", nlohmann::json::array()}};
    // 异常先处理，特殊剧情其次；遭遇事件仍是原作用域的正常交接，不能提前全图扫描。
    const auto rules = effective_events(current);
    for (const auto &[category, name] : {std::pair{workflow::EventClass::Exception, "exception"},
                                       std::pair{workflow::EventClass::Special, "special"},
                                       std::pair{workflow::EventClass::Encounter, "encounter"}}) {
        if (!diagnose && category != workflow::EventClass::Encounter) continue;
        if (std::none_of(rules.begin(), rules.end(),
            [category](const ScopedEvent &event) { return event.rule.category == category; })) continue;
        last_diagnostic_["checked_groups"].push_back(name);
        if (auto result = check_events(frame, current, image, category)) return result;
    }
    last_diagnostic_["outcome"] = "waiting_for_business_result";
    return std::nullopt;
}
std::optional<TickResult> FlowExecutor::poll_wait_events(Frame &frame, const workflow::Step &current) {
    if (effective_events(current).empty() || Clock::now() < frame.next_event_poll) return std::nullopt;
    frame.next_event_poll = Clock::now() + 100ms; // 轮询节奏，不是页面变化的期限。
    invalidate_observation();
    const auto image = observation_frame();
    frame.selected_frame.reset(); frame.selected_observation.reset();
    if (auto result = check_events(frame, current, image, workflow::EventClass::Overlay)) return result;
    // 显式延时没有“结果不符”，不能顺手全扫异常/特殊页面。
    return check_events(frame, current, image, workflow::EventClass::Encounter);
}
std::optional<TickResult> FlowExecutor::check_events(Frame &frame, const workflow::Step &current,
    const contracts::FrameEnvelope &image, workflow::EventClass category) {
    const auto rules = effective_events(current);
    std::string scope_key;
    if (category == workflow::EventClass::Overlay && frame.event_exits.empty() && observation_cycle_) {
        nlohmann::json key = nlohmann::json::array();
        for (const auto &scoped : rules) {
            const auto &rule = scoped.rule;
            if (rule.category != category) continue;
            key.push_back({{"owner", scoped.owner}, {"id", rule.id}, {"detect", request_key(rule.detect)},
                {"priority", rule.priority}, {"disposition", static_cast<int>(rule.disposition)},
                {"handler", rule.handler_definition}, {"resume", static_cast<int>(rule.resume)},
                {"replan", rule.replan_step}, {"reason", rule.reason},
                {"exit_ms", rule.exit_budget.count()}, {"ambiguity_ms", rule.ambiguity_budget.count()},
                {"resume_guard", rule.resume_guard ? request_key(*rule.resume_guard) : nlohmann::json(nullptr)}});
        }
        for (const auto &active : stack_)
            if (active.event) key.push_back({{"active", active.event->rule.id}});
        scope_key = key.dump();
        if (observation_cycle_->frame.identity == image.identity &&
            observation_cycle_->clear_overlay_scope == scope_key) {
            platform::timing::count(platform::timing::Counter::OverlayReuse);
            return std::nullopt;
        }
    }
    bool exiting = false;
    if (category == workflow::EventClass::Overlay && !frame.event_exits.empty()) {
        for (auto it = frame.event_exits.begin(); it != frame.event_exits.end();) {
            const auto observed = ports_.recognize(image, it->detect);
            account_event_time(); // 退出判断本身的耗时也属于仍挂起的父流程。
            if (observed.outcome == contracts::RecognitionOutcome::Error)
                return fail(observed.error_code.empty() ? "EVENT_EXIT_RECOGNITION_ERROR" : observed.error_code);
            if (observed.outcome == contracts::RecognitionOutcome::Hit) {
                if (Clock::now() >= it->deadline) return fail("EVENT_HANDLER_NO_PROGRESS:" + it->id);
                exiting = true; ++it;
            } else it = frame.event_exits.erase(it);
        }
    }
    std::vector<const ScopedEvent *> hits;
    int priority = std::numeric_limits<int>::min();
    for (const auto &scoped : rules) {
        const auto &rule = scoped.rule;
        if (rule.category != category) continue;
        // 同优先级仍检查歧义；已命中高优先级时不再计算任何低优先级候选。
        if (!hits.empty() && rule.priority < priority) break;
        if (std::any_of(frame.event_exits.begin(), frame.event_exits.end(),
            [&](const EventExit &exit) { return exit.id == rule.id; })) continue;
        if (std::any_of(stack_.begin(), stack_.end(), [&](const Frame &active) {
            return active.event && active.event->rule.id == rule.id;
        })) continue;
        const auto observation = ports_.recognize(image, rule.detect);
        platform::timing::count(platform::timing::Counter::OverlayChecks);
        if (observation.outcome == contracts::RecognitionOutcome::Error)
            return fail(observation.error_code.empty() ? "EVENT_RECOGNITION_ERROR" : observation.error_code);
        if (observation.outcome == contracts::RecognitionOutcome::NoHit) continue;
        if (rule.priority > priority) { priority = rule.priority; hits.clear(); }
        if (rule.priority == priority) hits.push_back(&scoped);
    }
    if (hits.size() > 1) {
        const auto now = Clock::now();
        if (!frame.ambiguity_since || frame.ambiguity_category != category || frame.ambiguity_priority != priority) {
            frame.ambiguity_since = now; frame.ambiguity_category = category; frame.ambiguity_priority = priority;
        }
        const auto budget = (*std::min_element(hits.begin(), hits.end(), [](const auto *a, const auto *b) {
            return a->rule.ambiguity_budget < b->rule.ambiguity_budget;
        }))->rule.ambiguity_budget;
        frame.selected_frame.reset(); frame.selected_observation.reset();
        if (now - *frame.ambiguity_since >= budget) return fail("EVENT_AMBIGUOUS");
        return waiting(50ms);
    }
    if (frame.ambiguity_category == category) frame.ambiguity_since.reset();
    if (hits.empty()) {
        if (exiting) {
            frame.selected_frame.reset(); frame.selected_observation.reset();
            return waiting(50ms);
        }
        // 只复用完全检查过的 NoHit；Error、歧义和退出等待永不缓存为“安全”。
        if (!scope_key.empty() && observation_cycle_ && observation_cycle_->frame.identity == image.identity)
            observation_cycle_->clear_overlay_scope = std::move(scope_key);
        return std::nullopt;
    }
    const auto selected = *hits.front();
    if (category == workflow::EventClass::Exception || category == workflow::EventClass::Special) {
        last_diagnostic_["selected_event"] = selected.rule.id;
        last_diagnostic_["outcome"] = "handling";
    }
    if (selected.rule.disposition == workflow::EventDisposition::ExternalBlocked) return blocked(selected.rule.reason);
    if (stack_.size() >= 8) return fail("EVENT_DEPTH_LIMIT");
    const auto &definition = program_.definitions.at(selected.rule.handler_definition);
    Frame handler;
    handler.definition = definition.id; handler.current = definition.entry;
    handler.invoked_at = handler.entered_at = Clock::now();
    if (definition.cumulative_budget) handler.invocation_deadline = handler.invoked_at + *definition.cumulative_budget;
    handler.hits[definition.entry] = 1; handler.event = selected;
    frame.selected_frame.reset(); frame.selected_observation.reset();
    account_event_time();
    invalidate_observation();
    stack_.push_back(std::move(handler));
    return progress();
}
TickResult FlowExecutor::resume_event(Frame &frame, const workflow::Step &current) {
    const auto resume = *frame.resume;
    if (resume.owner >= stack_.size()) return fail("EVENT_RESUME_OWNER_MISSING");
    if (!resume.rule.resume_guard) return fail("EVENT_RESUME_GUARD_MISSING");
    if (frame.event_exits.empty() && Clock::now() - frame.entered_at >= current.time_limit)
        return route_error(frame, current, "EVENT_RESUME_UNCONFIRMED");
    const auto image = observation_frame();
    if (auto event = check_events(frame, current, image, workflow::EventClass::Overlay)) return *event;
    for (std::size_t i = resume.owner; i < stack_.size(); ++i) {
        const auto &pending = stack_[i].pending;
        if (pending && Clock::now() >= pending->submitted_at + pending->result_budget + pending->event_pause)
            return blocked("EVENT_PARENT_RESULT_TIMEOUT");
    }
    const auto guard = ports_.recognize(image, *resume.rule.resume_guard);
    if (guard.outcome == contracts::RecognitionOutcome::Error)
        return fail(guard.error_code.empty() ? "EVENT_RESUME_RECOGNITION_ERROR" : guard.error_code);
    if (guard.outcome == contracts::RecognitionOutcome::NoHit) {
        if (auto event = check_unexpected(frame, current, image, "event_resume_not_confirmed")) return *event;
        return waiting(50ms);
    }
    // 不把“处理器结束/回到地图”当作原动作成功。只有原后置条件的新证据才结清回执。
    // 专用导航的重新规划策略必须由游戏层显式表达，不能在通用内核猜幂等性。
    for (std::size_t i = resume.owner; i < stack_.size(); ++i) {
        const auto &item = stack_[i];
        if (i > resume.owner && item.event) return blocked("EVENT_REPLAN_CROSSES_ACTIVE_HANDLER");
        if (!item.pending) continue;
        const auto &pending = *item.pending;
        if (pending.delivery_unknown || !pending.expected_result) return blocked("EVENT_REPLAN_INPUT_UNKNOWN");
        const auto result = ports_.recognize(image, *pending.expected_result);
        if (result.outcome == contracts::RecognitionOutcome::Error)
            return fail(result.error_code.empty() ? "EVENT_PARENT_RESULT_ERROR" : result.error_code);
        if (result.outcome == contracts::RecognitionOutcome::NoHit) {
            if (auto event = check_unexpected(frame, current, image, "event_parent_result_not_confirmed")) return *event;
            return waiting(50ms);
        }
        const auto &a = result.basis; const auto &b = pending.before;
        if (a.device_id != b.device_id || a.game_id != b.game_id || a.pack_revision != b.pack_revision ||
            a.generation != b.generation || a.connection_generation != b.connection_generation ||
            a.viewport_id != b.viewport_id || a.raw_size != b.raw_size || a.recognition_size != b.recognition_size ||
            a.display_rotation != b.display_rotation || a.frame_id <= b.frame_id || a.action_epoch < pending.action_epoch)
            return fail("EVENT_PARENT_RESULT_STALE");
    }
    auto &owner = stack_[resume.owner];
    const auto &definition = program_.definitions.at(owner.definition);
    const auto found = definition.steps.find(resume.rule.replan_step);
    if (found == definition.steps.end()) return fail("EVENT_REPLAN_TARGET_OUTSIDE_OWNER");
    if (owner.hits[found->first] >= found->second.max_hit) return fail("EVENT_REPLAN_HIT_LIMIT");
    account_event_time();
    for (std::size_t i = resume.owner; i < stack_.size(); ++i) stack_[i].pending.reset();
    // 索引指向实际调用帧；同名节点不会误跳入触发事件的内层定义。
    stack_.resize(resume.owner + 1);
    auto &target = stack_.back();
    target.current = found->first;
    ++target.hits[found->first];
    target.next_pending = target.error_pending = false;
    target.selected_frame.reset(); target.selected_observation.reset();
    target.input_selection.reset();
    target.resume.reset(); target.delay_until.reset(); target.event_exits.clear();
    target.entered_at = Clock::now(); // 仅新步骤起点，phase_deadlines 与全任务期限不重置。
    invalidate_observation();
    return progress();
}
} // namespace wvd::runtime
