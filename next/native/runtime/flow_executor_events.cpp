#include "flow_executor.hpp"
#include "platform/execution_timing.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace wvd::runtime {
namespace {
using namespace std::chrono_literals;
}

void FlowExecutor::account_event_time() {
    const auto now = Clock::now();
    const auto elapsed = now - accounted_at_;
    accounted_at_ = now;
    for (std::size_t i = 0; i < stack_.size(); ++i) {
        auto &frame = stack_[i];
        const bool descendant_event = std::any_of(stack_.begin() + i + 1, stack_.end(),
            [](const Frame &child) { return child.event.has_value() || !child.event_exits.empty(); });
        const bool read_suspended = read_recovery_ && read_recovery_->suspended;
        if (!descendant_event && frame.event_exits.empty() && !read_suspended) continue;
        // 使用事件/观察故障暂停区间的并集，不按嵌套深度或原因数乘 elapsed。
        const bool event_suspended = descendant_event || !frame.event_exits.empty();
        const auto &current = program_.definitions.at(frame.definition).steps.at(frame.current);
        // 动画/纯Sleep的现实时间继续流逝；只有有效观察预算扣除读故障区间。
        if (event_suspended || !std::holds_alternative<workflow::Wait>(current.data)) frame.entered_at += elapsed;
        if (frame.selection_origin) frame.selection_origin->entered_at += elapsed;
        frame.paused_event_time += elapsed;
        if (frame.invocation_deadline) *frame.invocation_deadline += elapsed;
        if (frame.pending) {
            frame.pending->event_pause += elapsed;
            if (frame.pending->selection_origin) frame.pending->selection_origin->entered_at += elapsed;
            if (event_suspended) frame.pending->animation_pause += elapsed;
        }
        if (event_suspended && frame.delay_until) *frame.delay_until += elapsed;
        if (event_suspended && frame.poll_until) *frame.poll_until += elapsed;
        if (frame.no_progress_since) *frame.no_progress_since += elapsed;
        if (frame.next_diagnostic_poll != Clock::time_point{}) frame.next_diagnostic_poll += elapsed;
        for (auto &[name, deadline] : frame.phase_deadlines) { (void)name; deadline += elapsed; }
        if (descendant_event || read_suspended)
            for (auto &exit : frame.event_exits) exit.deadline += elapsed;
        if (read_suspended && frame.ambiguity_since) *frame.ambiguity_since += elapsed;
    }
}
const std::vector<FlowExecutor::ScopedEvent> &FlowExecutor::effective_events(const workflow::Step &current) const {
    bool unchanged = event_scope_keys_.size() == stack_.size();
    for (std::size_t i = 0; unchanged && i < stack_.size(); ++i) {
        const auto &active = stack_[i];
        const auto *scope = i + 1 == stack_.size() ? &current :
            &program_.definitions.at(active.definition).steps.at(active.current);
        unchanged = event_scope_keys_[i].step == scope &&
            (active.event ? event_scope_keys_[i].active == active.event->rule.id : event_scope_keys_[i].active.empty());
    }
    if (unchanged) return event_scope_rules_;
    std::vector<EventScopeKey> keys;
    keys.reserve(stack_.size());
    for (std::size_t i = 0; i < stack_.size(); ++i) {
        const auto &active = stack_[i];
        keys.push_back({i + 1 == stack_.size() ? &current :
            &program_.definitions.at(active.definition).steps.at(active.current),
            active.event ? active.event->rule.id : std::string{}});
    }
    std::map<std::string, ScopedEvent> inherited;
    const auto merge = [&](const workflow::Step &scope, std::size_t owner) {
        for (const auto &id : scope.disabled_events) inherited.erase(id);
        for (const auto &rule : scope.event_policy) inherited.insert_or_assign(rule.id, ScopedEvent{rule, owner});
    };
    for (std::size_t i = 0; i < stack_.size(); ++i) {
        const auto &definition = program_.definitions.at(stack_[i].definition);
        if (i && definition.checks) {
            for (auto it = inherited.begin(); it != inherited.end();) {
                if (!definition.checks->inherit.contains(it->first)) it = inherited.erase(it);
                else ++it;
            }
        }
        for (const auto &rule : definition.events) inherited.insert_or_assign(rule.id, ScopedEvent{rule, i});
        merge(i + 1 == stack_.size() ? current : definition.steps.at(stack_[i].current), i);
    }
    std::vector<ScopedEvent> result;
    for (const auto &[id, rule] : inherited) { (void)id; result.push_back(rule); }
    std::stable_sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
        return a.rule.priority > b.rule.priority;
    });
    // Publish only a completely built scope. Immutable program pointers and
    // active-handler identity invalidate it; elapsed time and pixels do not.
    event_scope_rules_ = std::move(result);
    event_scope_keys_ = std::move(keys);
    ++event_scope_version_;
    return event_scope_rules_;
}
std::optional<TickResult> FlowExecutor::check_unexpected(Frame &frame, const workflow::Step &current,
    const contracts::FrameEnvelope &image, const std::string &reason, bool force) {
    if (!frame.no_progress_since) frame.no_progress_since = Clock::now();
    if (exception_restart_supported_ && !exception_since_) exception_since_ = Clock::now();
    // 一帧 NoHit 常是动画/加载，不是异常。这里只延迟诊断，不缩短原业务等待预算。
    const auto now = Clock::now();
    const auto &policy = program_.definitions.at(frame.definition).checks;
    const auto debounce = policy ? policy->debounce : 1s;
    const auto interval = policy ? policy->interval : 1s;
    const bool diagnose = force || (now - *frame.no_progress_since >= debounce && now >= frame.next_diagnostic_poll);
    frame.diagnostic_checked = diagnose;
    if (diagnose) frame.next_diagnostic_poll = now + interval;
    last_diagnostic_ = {{"source_path", current.source_path}, {"reason", reason},
        {"business_group", current.check_group}, {"checked_groups", nlohmann::json::array()}};
    // 异常先处理，特殊剧情其次；遭遇事件仍是原作用域的正常交接，不能提前全图扫描。
    const auto &rules = effective_events(current);
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
    const auto &rules = effective_events(current);
    const auto eligible = [&](const ScopedEvent &scoped) {
        const auto &rule = scoped.rule;
        if (rule.category != workflow::EventClass::Overlay &&
            rule.category != workflow::EventClass::Encounter) return false;
        return std::none_of(stack_.begin(), stack_.end(), [&](const Frame &active) {
            return active.event && active.event->rule.id == rule.id;
        });
    };
    // 只有 Exception/Special 时，显式延迟没有到期的主动检查，不采“空转帧”。
    // 事件退出仍须观察，不能因当前没有注册 Overlay 而丢失返回确认。
    if (frame.event_exits.empty() && std::none_of(rules.begin(), rules.end(), eligible))
        return std::nullopt;
    if (Clock::now() < frame.next_event_poll) return std::nullopt;
    invalidate_observation();
    const auto image = observation_frame();
    frame.selected_frame.reset(); frame.selected_observation.reset();
    auto result = check_events(frame, current, image, workflow::EventClass::Overlay);
    if (result) return result; // 处理器可能入栈，返回后不再访问旧 Frame 引用。
    result = check_events(frame, current, image, workflow::EventClass::Encounter);
    if (result) return result;
    frame.next_event_poll = Clock::now() + 100ms; // 完成后计轮询节奏，不是页面响应期限。
    return std::nullopt;
}
std::optional<TickResult> FlowExecutor::check_events(Frame &frame, const workflow::Step &current,
    const contracts::FrameEnvelope &image, workflow::EventClass category) {
    // 未确认旧输入已静止前，只核对原结果，不能让普通事件发送第二个输入。
    if (frame.pending && frame.pending->delivery_unknown) return std::nullopt;
    const auto &rules = effective_events(current);
    std::string scope_key;
    if (category == workflow::EventClass::Overlay && frame.event_exits.empty() && observation_cycle_) {
        scope_key = std::to_string(event_scope_version_);
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
        if (rule.on_device_restart || rule.category != category) continue;
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
    const auto image = observation_frame();
    if (auto event = check_events(frame, current, image, workflow::EventClass::Overlay)) return *event;
    const auto guard = ports_.recognize(image, *resume.rule.resume_guard);
    if (guard.outcome == contracts::RecognitionOutcome::Error)
        return fail(guard.error_code.empty() ? "EVENT_RESUME_RECOGNITION_ERROR" : guard.error_code);
    if (guard.outcome == contracts::RecognitionOutcome::NoHit) {
        // 先看恢复后的新画面；旧节点已超时不能否定刚完成的启动恢复。
        if (frame.event_exits.empty() && Clock::now() - frame.entered_at >= current.time_limit)
            return route_error(frame, current, "EVENT_RESUME_UNCONFIRMED");
        if (auto event = check_unexpected(frame, current, image, "event_resume_not_confirmed")) return *event;
        return waiting(50ms);
    }
    // 不把“处理器结束/回到地图”当作原动作成功。只有原后置条件的新证据才结清回执。
    // 专用导航的重新规划策略必须由游戏层显式表达，不能在通用内核猜幂等性。
    for (std::size_t i = resume.owner; i < stack_.size(); ++i) {
        const auto &item = stack_[i];
        if (i > resume.owner && item.event) {
            const auto &active = *item.event;
            // Another restart may finish Boot while the previous Boot still owns
            // a pending title input. Only the same restart chain can be unwound;
            // its pending results must still pass the checks below.
            const bool same_restart = resume.rule.on_device_restart && active.rule.on_device_restart &&
                active.owner == resume.owner && active.rule.id == resume.rule.id &&
                active.rule.handler_definition == resume.rule.handler_definition &&
                active.rule.resume == workflow::ResumeMode::Replan &&
                active.rule.replan_step == resume.rule.replan_step;
            if (!same_restart) return blocked("EVENT_REPLAN_CROSSES_ACTIVE_HANDLER");
        }
        if (!item.pending) continue;
        const auto &pending = *item.pending;
        if (!pending.expected_result) return blocked("EVENT_REPLAN_INPUT_UNKNOWN");
        const auto result = ports_.recognize(image, *pending.expected_result);
        if (result.outcome == contracts::RecognitionOutcome::Error)
            return fail(result.error_code.empty() ? "EVENT_PARENT_RESULT_ERROR" : result.error_code);
        if (result.outcome == contracts::RecognitionOutcome::NoHit) {
            if (auto event = check_unexpected(frame, current, image, "event_parent_result_not_confirmed")) return *event;
            if (Clock::now() >= pending.result_started_at + pending.result_budget + pending.event_pause)
                return blocked("EVENT_PARENT_RESULT_TIMEOUT");
            return waiting(50ms);
        }
        const auto &a = result.basis; const auto &b = pending.before;
        if (!result_identity_matches(a, b) || a.frame_id <= b.frame_id || a.action_epoch < pending.action_epoch)
            return fail("EVENT_PARENT_RESULT_STALE");
        if (pending.delivery_unknown && !ports_.settle_observed_input())
            return blocked("OBSERVED_RESULT_INPUT_CLEANUP_UNCONFIRMED");
    }
    auto &owner = stack_[resume.owner];
    const auto &definition = program_.definitions.at(owner.definition);
    const auto found = definition.steps.find(resume.rule.replan_step);
    if (found == definition.steps.end()) return fail("EVENT_REPLAN_TARGET_OUTSIDE_OWNER");
    if (found->second.max_hit > 0 && owner.hits[found->first] >= found->second.max_hit)
        return fail("EVENT_REPLAN_HIT_LIMIT");
    account_event_time();
    for (std::size_t i = resume.owner; i < stack_.size(); ++i) {
        if (stack_[i].pending) report_input_result(*stack_[i].pending, "confirmed", image.identity.frame_id);
        stack_[i].pending.reset();
    }
    // 索引指向实际调用帧；同名节点不会误跳入触发事件的内层定义。
    stack_.resize(resume.owner + 1);
    auto &target = stack_.back();
    target.current = found->first;
    if (found->second.max_hit > 0) ++target.hits[found->first];
    target.next_pending = target.error_pending = false;
    target.selected_frame.reset(); target.selected_observation.reset();
    target.selection_origin.reset();
    target.operation_started = false;
    target.resume.reset(); target.delay_until.reset(); target.event_exits.clear();
    target.entered_at = Clock::now(); // 仅新步骤起点，phase_deadlines 与全任务期限不重置。
    invalidate_observation();
    return progress();
}
} // namespace wvd::runtime
