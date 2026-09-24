#include "serialization.hpp"

namespace wvd::workflow {
namespace {
using J = nlohmann::json;
J box(const contracts::Box &value) {
    return J::array({value.x, value.y, value.width, value.height});
}
J request(const recognition::Request &value) {
    J result{{"id", value.recognizer_id}, {"revision", value.parameter_revision},
             {"roi", box(value.roi)}};
    if (const auto *image = std::get_if<recognition::TemplateParameters>(&value.parameters)) {
        result["kind"] = "template";
        result["image"] = image->image;
        result["threshold"] = image->threshold;
    } else if (const auto *ocr = std::get_if<recognition::OcrParameters>(&value.parameters)) {
        result["kind"] = "ocr";
        result["expected"] = ocr->expected_text;
    } else {
        const auto &custom = std::get<recognition::CustomParameters>(value.parameters);
        result["kind"] = "registered";
        result["binding"] = custom.binding;
        result["parameters"] = custom.parameters;
    }
    return result;
}
J event(const EventRule &value) {
    return {{"id", value.id},
            {"class", value.category == EventClass::Overlay ? "overlay" : "encounter"},
            {"priority", value.priority}, {"detect", request(value.detect)},
            {"disposition", value.disposition == EventDisposition::Handle ? "handle" : "external_blocked"},
            {"handler", value.handler_definition},
            {"resume", value.resume == ResumeMode::Replan ? "replan" : "reobserve"},
            {"replan_step", value.replan_step}, {"reason", value.reason},
            {"exit_budget_ms", value.exit_budget.count()}};
}
J data(const StepData &value) {
    if (const auto *observe = std::get_if<Observe>(&value))
        return {{"kind", "observe"}, {"request", request(observe->request)}};
    if (const auto *route = std::get_if<Route>(&value))
        return {{"kind", "route"}, {"candidates", route->candidates}};
    if (const auto *input = std::get_if<Input>(&value)) {
        J result{{"kind", "input"}, {"scene", request(input->scene)},
                 {"target", request(input->target)}, {"command", input->command},
                 {"allowed_area", box(input->allowed_area)},
                 {"use_target_center", input->use_target_center},
                 {"clip_target_to_area", input->clip_target_to_area}};
        if (input->target_offset)
            result["target_offset"] = J::array({input->target_offset->x, input->target_offset->y});
        return result;
    }
    if (const auto *await = std::get_if<AwaitResult>(&value))
        return {{"kind", "await_result"}, {"condition", request(await->condition)},
                {"budget_ms", await->budget.count()},
                {"initial_delay_ms", await->initial_delay.count()},
                {"poll_interval_ms", await->poll_interval.count()}};
    if (const auto *wait = std::get_if<Wait>(&value))
        return {{"kind", "wait"}, {"duration_ms", wait->duration.count()}};
    if (const auto *call = std::get_if<Call>(&value))
        return {{"kind", "call"}, {"definition", call->definition}};
    if (const auto *ret = std::get_if<Return>(&value))
        return {{"kind", "return"}, {"outcome", ret->outcome}};
    if (const auto *confirm = std::get_if<BusinessConfirm>(&value))
        return {{"kind", "business_confirm"}, {"operation", confirm->operation},
                {"condition", request(confirm->condition)}, {"parameters", confirm->parameters}};
    if (const auto *operation = std::get_if<RegisteredOperation>(&value))
        return {{"kind", "registered_operation"}, {"binding", operation->binding},
                {"parameters", operation->parameters}};
    if (std::holds_alternative<Finish>(value)) return {{"kind", "finish"}};
    if (const auto *blocked = std::get_if<ExternalBlocked>(&value))
        return {{"kind", "external_blocked"}, {"reason", blocked->reason}};
    return {{"kind", "fail"}, {"reason", std::get<Fail>(value).reason}};
}
} // namespace

nlohmann::json serialize(const FlowProgram &program) {
    program.validate();
    J definitions = J::object();
    for (const auto &[id, definition] : program.definitions) {
        J steps = J::object();
        for (const auto &[step_id, step] : definition.steps) {
            J entry{{"id", step.id}, {"source_path", step.source_path},
                    {"data", data(step.data)}, {"next", step.next},
                    {"on_error", step.on_error}, {"time_limit_ms", step.time_limit.count()},
                    {"delay_after_ms", step.delay_after.count()}, {"max_hit", step.max_hit}};
            if (step.guard) entry["guard"] = request(*step.guard);
            entry["events"] = J::array();
            for (const auto &rule : step.event_policy)
                entry["events"].push_back(event(rule));
            steps[step_id] = std::move(entry);
        }
        definitions[id] = {{"id", definition.id}, {"entry", definition.entry},
                           {"steps", std::move(steps)}};
    }
    return {{"program_schema", FlowProgram::schema}, {"engine_kind", program.engine_kind},
            {"revision", program.revision}, {"root_definition", program.root_definition},
            {"definitions", std::move(definitions)}};
}
} // namespace wvd::workflow
