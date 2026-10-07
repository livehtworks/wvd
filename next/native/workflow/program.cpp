#include "program.hpp"
#include <functional>
#include <set>
#include <stdexcept>

namespace wvd::workflow {
void FlowProgram::validate() const {
    if (engine_kind != "wvd_native" || revision.empty() || root_definition.empty() ||
        !definitions.contains(root_definition))
        throw std::runtime_error("FLOW_PROGRAM_IDENTITY_INVALID");
    for (const auto &[definition_id, definition] : definitions) {
        if (definition_id != definition.id || !definition.steps.contains(definition.entry))
            throw std::runtime_error("FLOW_DEFINITION_INVALID");
        if (definition.cumulative_budget && (definition.cumulative_budget->count() <= 0 ||
            *definition.cumulative_budget > std::chrono::minutes{30}))
            throw std::runtime_error("FLOW_DEFINITION_BUDGET_INVALID");
        for (const auto &[step_id, step] : definition.steps) {
            if (step.check_group != "business" && step.check_group != "combat" &&
                step.check_group != "chest" && step.check_group != "special" && step.check_group != "exception" &&
                step.check_group != "navigation" && step.check_group != "supply")
                throw std::runtime_error("FLOW_CHECK_GROUP_INVALID");
            // 诊断候选必须先有只读视觉/业务判据。仅允许观察或已注册的业务确认；
            // Input/Call/Wait/终态不能借 unexpected_only 绕过正常输入与调用契约。
            if (step.unexpected_only && (!step.guard ||
                (!std::holds_alternative<Observe>(step.data) &&
                 !std::holds_alternative<RegisteredOperation>(step.data))))
                throw std::runtime_error("FLOW_UNEXPECTED_STEP_INVALID");
            if (step.marks_known_scene && (!step.guard || step.unexpected_only ||
                (!std::holds_alternative<Observe>(step.data) &&
                 !std::holds_alternative<BusinessConfirm>(step.data) &&
                 !std::holds_alternative<RegisteredOperation>(step.data))))
                throw std::runtime_error("FLOW_KNOWN_SCENE_INVALID");
            if (step.business_guard && (step.guard || !step.business_guard->is_object() ||
                !std::holds_alternative<Route>(step.data)))
                throw std::runtime_error("FLOW_BUSINESS_GUARD_INVALID");
            if (step_id != step.id || step.source_path.empty() || step.max_hit < 0 ||
                (step.max_hit == 0 && (std::holds_alternative<Input>(step.data) ||
                    std::holds_alternative<AwaitResult>(step.data))) ||
                (step.consecutive_input_limit && !std::holds_alternative<Input>(step.data)) ||
                step.time_limit.count() < 1 || step.delay_after.count() < 0)
                throw std::runtime_error("FLOW_STEP_INVALID");
            for (const auto &successor : step.next)
                if (!definition.steps.contains(successor))
                    throw std::runtime_error("FLOW_SUCCESSOR_MISSING");
            for (const auto &successor : step.on_error)
                if (!definition.steps.contains(successor))
                    throw std::runtime_error("FLOW_ERROR_SUCCESSOR_MISSING");
            if (std::holds_alternative<Fail>(step.data) &&
                (step.guard.has_value() == step.next.empty()))
                throw std::runtime_error("FLOW_RECOVERY_RECHECK_INVALID");
            if (const auto *call = std::get_if<Call>(&step.data)) {
                if (!definitions.contains(call->definition))
                    throw std::runtime_error("FLOW_CALL_TARGET_MISSING");
                const auto &ports = definitions.at(call->definition).handoffs;
                for (const auto &port : ports)
                    if (!call->handoffs.contains(port))
                        throw std::runtime_error("FLOW_HANDOFF_BINDING_MISSING:" + definition_id + ":" + step_id + ":" + port);
                for (const auto &[port, targets] : call->handoffs) {
                    if (!ports.contains(port) || targets.empty()) throw std::runtime_error("FLOW_HANDOFF_BINDING_INVALID");
                    for (const auto &target : targets)
                        if (!definition.steps.contains(target)) throw std::runtime_error("FLOW_HANDOFF_TARGET_MISSING");
                }
            }
            if (step.handles_business_failure &&
                (!std::holds_alternative<Call>(step.data) || step.on_error.empty()))
                throw std::runtime_error("FLOW_BUSINESS_FAILURE_ROUTE_INVALID");
            if (const auto *returned = std::get_if<Return>(&step.data))
                if ((returned->outcome != "completed" && returned->outcome != "failure" && returned->outcome != "handoff") ||
                    (returned->outcome == "handoff" ? !definition.handoffs.contains(returned->port) : !returned->port.empty()) ||
                    (returned->outcome == "failure") != !returned->reason.empty())
                    throw std::runtime_error("FLOW_RETURN_OUTCOME_INVALID");
            if (const auto *business = std::get_if<BusinessFail>(&step.data))
                if (definition_id != root_definition || business->reason.empty())
                    throw std::runtime_error("FLOW_BUSINESS_FAILURE_INVALID");
            if (const auto *await = std::get_if<AwaitResult>(&step.data))
                if (await->budget.count() < 1 || await->initial_delay.count() < 0 ||
                    await->poll_interval.count() < 1 ||
                    await->initial_delay >= await->budget)
                    throw std::runtime_error("FLOW_AWAIT_BUDGET_INVALID");
            if (const auto *operation = std::get_if<RegisteredOperation>(&step.data))
                if (operation->binding.empty() || !operation->parameters.is_object())
                    throw std::runtime_error("FLOW_OPERATION_INVALID");
            if (const auto *input = std::get_if<Input>(&step.data)) {
                const auto pair_ms = input->command.value("click_pair_interval_ms", 0);
                if (pair_ms && (input->command.value("kind", "") != "Click" || pair_ms != 100 || input->retry))
                    throw std::runtime_error("FLOW_CLICK_PAIR_INVALID");
                if (input->retry && (input->command.value("kind", "") != "Click" ||
                    input->retry->interval < std::chrono::seconds{1} ||
                    input->retry->interval > std::chrono::minutes{1}))
                    throw std::runtime_error("FLOW_INPUT_RETRY_INVALID");
                if (input->retry && !input->retry->restart_from.empty() &&
                    (!definition.steps.contains(input->retry->restart_from) ||
                     !std::holds_alternative<Route>(definition.steps.at(input->retry->restart_from).data)))
                    throw std::runtime_error("FLOW_INPUT_RESTART_ROUTE_INVALID");
                if (!input->command.is_object() || input->allowed_area.width <= 0 ||
                    input->allowed_area.height <= 0)
                    throw std::runtime_error("FLOW_INPUT_INVALID");
                if (step.next.size() != 1 || !std::holds_alternative<AwaitResult>(
                    definition.steps.at(step.next.front()).data))
                    throw std::runtime_error("FLOW_INPUT_OBSERVER_MISSING");
                const auto &observer = definition.steps.at(step.next.front());
                if (observer.max_hit < step.max_hit ||
                    observer.disabled_events != step.disabled_events)
                    throw std::runtime_error("FLOW_INPUT_OBSERVER_CONTRACT_MISMATCH");
            }
            if (const auto *confirm = std::get_if<BusinessConfirm>(&step.data))
                if (confirm->binding.empty() || !confirm->parameters.is_object())
                    throw std::runtime_error("FLOW_BUSINESS_BINDING_MISSING");
            if (const auto *wait = std::get_if<Wait>(&step.data))
                if (wait->duration.count() < 0 || wait->duration > std::chrono::minutes{5})
                    throw std::runtime_error("FLOW_WAIT_INVALID");
            if (const auto *poll = std::get_if<Poll>(&step.data))
                if (poll->interval.count() < 1 || poll->interval > std::chrono::seconds{10} ||
                    step.guard || step.business_guard || step.unexpected_only ||
                    step.next.empty() || step.delay_after.count() != 0 || (poll->progress && !poll->ongoing))
                    throw std::runtime_error("FLOW_POLL_INVALID");
            // 同一候选列表只能有一个无条件等待去向，不能按名称猜哪个是轮询。
            std::set<std::string> polling;
            for (const auto &id : step.next)
                if (std::holds_alternative<Poll>(definition.steps.at(id).data)) polling.insert(id);
            if (polling.size() > 1)
                throw std::runtime_error("FLOW_MULTIPLE_POLL_FALLBACKS");
            if (std::holds_alternative<Finish>(step.data) && definition_id != root_definition)
                throw std::runtime_error("FLOW_CHILD_FINISH_INVALID");
            for (const auto &event : step.event_policy) {
                if (step.disabled_events.contains(event.id))
                    throw std::runtime_error("FLOW_EVENT_CONFLICT");
                if (event.id.empty() || event.priority < 0 || event.priority > 1000 ||
                    event.exit_budget.count() < 1 || event.exit_budget > std::chrono::minutes{1} ||
                    event.ambiguity_budget.count() < 1 ||
                    event.ambiguity_budget > std::chrono::minutes{1} ||
                    (event.disposition == EventDisposition::Handle &&
                     !definitions.contains(event.handler_definition)) ||
                    (event.resume == ResumeMode::Replan &&
                     (!definition.steps.contains(event.replan_step) || !event.resume_guard)))
                    throw std::runtime_error("FLOW_EVENT_INVALID");
            }
            if (step.disabled_events.contains(""))
                throw std::runtime_error("FLOW_EVENT_DISABLE_INVALID");
        }
        if (definition.checks && (definition.checks->phase.empty() || definition.checks->debounce.count() < 0 ||
            definition.checks->interval.count() <= 0)) throw std::runtime_error("FLOW_CHECK_POLICY_INVALID");
        std::set<std::string> event_ids;
        for (const auto &event : definition.events)
            if (event.id.empty() || !event_ids.insert(event.id).second ||
                event.priority < 0 || event.priority > 1000 || event.exit_budget.count() < 1 ||
                event.exit_budget > std::chrono::minutes{1} || event.ambiguity_budget.count() < 1 ||
                event.ambiguity_budget > std::chrono::minutes{1} ||
                (event.disposition == EventDisposition::Handle && !definitions.contains(event.handler_definition)) ||
                (event.resume == ResumeMode::Replan && (!definition.steps.contains(event.replan_step) || !event.resume_guard)))
                throw std::runtime_error("FLOW_DEFINITION_EVENT_INVALID");
    }
    std::set<std::string> active, visited;
    std::function<void(const std::string &)> visit = [&](const std::string &id) {
        if (visited.contains(id)) return;
        if (!active.insert(id).second) throw std::runtime_error("FLOW_CALL_CYCLE");
        const auto &definition = definitions.at(id);
        for (const auto &event : definition.events)
            if (event.disposition == EventDisposition::Handle) visit(event.handler_definition);
        for (const auto &[step_id, step] : definition.steps) {
            (void)step_id;
            if (const auto *call = std::get_if<Call>(&step.data)) visit(call->definition);
            for (const auto &event : step.event_policy)
                if (event.disposition == EventDisposition::Handle) visit(event.handler_definition);
        }
        active.erase(id);
        visited.insert(id);
    };
    visit(root_definition);
}
} // namespace wvd::workflow
