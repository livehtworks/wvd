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
        for (const auto &[step_id, step] : definition.steps) {
            if (step_id != step.id || step.source_path.empty() || step.max_hit < 1 ||
                step.time_limit.count() < 1 || step.delay_after.count() < 0)
                throw std::runtime_error("FLOW_STEP_INVALID");
            for (const auto &successor : step.next)
                if (!definition.steps.contains(successor))
                    throw std::runtime_error("FLOW_SUCCESSOR_MISSING");
            for (const auto &successor : step.on_error)
                if (!definition.steps.contains(successor))
                    throw std::runtime_error("FLOW_ERROR_SUCCESSOR_MISSING");
            if (const auto *call = std::get_if<Call>(&step.data)) {
                if (!definitions.contains(call->definition))
                    throw std::runtime_error("FLOW_CALL_TARGET_MISSING");
            }
            if (const auto *await = std::get_if<AwaitResult>(&step.data))
                if (await->budget.count() < 1 || await->initial_delay.count() < 0 ||
                    await->poll_interval.count() < 1 ||
                    await->initial_delay >= await->budget)
                    throw std::runtime_error("FLOW_AWAIT_BUDGET_INVALID");
            if (const auto *operation = std::get_if<RegisteredOperation>(&step.data))
                if (operation->binding.empty() || !operation->parameters.is_object())
                    throw std::runtime_error("FLOW_OPERATION_INVALID");
            if (const auto *input = std::get_if<Input>(&step.data))
                if (!input->command.is_object() || input->allowed_area.width <= 0 ||
                    input->allowed_area.height <= 0)
                    throw std::runtime_error("FLOW_INPUT_INVALID");
            if (const auto *wait = std::get_if<Wait>(&step.data))
                if (wait->duration.count() < 0 || wait->duration > std::chrono::minutes{5})
                    throw std::runtime_error("FLOW_WAIT_INVALID");
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
                     !definition.steps.contains(event.replan_step)))
                    throw std::runtime_error("FLOW_EVENT_INVALID");
            }
            if (step.disabled_events.contains(""))
                throw std::runtime_error("FLOW_EVENT_DISABLE_INVALID");
        }
    }
    std::set<std::string> active, visited;
    std::function<void(const std::string &)> visit = [&](const std::string &id) {
        if (visited.contains(id)) return;
        if (!active.insert(id).second) throw std::runtime_error("FLOW_CALL_CYCLE");
        const auto &definition = definitions.at(id);
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
