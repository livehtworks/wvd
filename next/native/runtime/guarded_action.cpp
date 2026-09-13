#include "guarded_action.hpp"
#include <thread>

namespace wvd::runtime {
using namespace contracts;
namespace {
maafw::RecognitionRequest recognition(const nlohmann::json &value) {
    auto roi = value.at("roi").get<std::vector<int>>();
    if (roi.size() != 4)
        throw std::runtime_error("ROI_INVALID");
    maafw::RecognitionRequest result{
        value.at("id"), value.at("revision"), {roi[0], roi[1], roi[2], roi[3]}, {}};
    if (value.value("type", "template") == "template")
        result.parameters =
            maafw::TemplateParameters{value.at("image"), value.value("threshold", 0.8)};
    else if (value.at("type") == "ocr")
        result.parameters =
            maafw::OcrParameters{value.at("expected").get<std::vector<std::string>>()};
    else
        throw std::runtime_error("RECO_TYPE_NOT_SUPPORTED");
    return result;
}
Command command(const nlohmann::json &value) {
    static const std::map<std::string, ActionKind> kinds{{"Click", ActionKind::Click},
                                                         {"Swipe", ActionKind::Swipe},
                                                         {"TouchDown", ActionKind::TouchDown},
                                                         {"TouchMove", ActionKind::TouchMove},
                                                         {"TouchUp", ActionKind::TouchUp},
                                                         {"ClickKey", ActionKind::ClickKey},
                                                         {"KeyDown", ActionKind::KeyDown},
                                                         {"KeyUp", ActionKind::KeyUp},
                                                         {"Text", ActionKind::Text},
                                                         {"Scroll", ActionKind::Scroll},
                                                         {"RelativeMove", ActionKind::RelativeMove},
                                                         {"StartApp", ActionKind::StartApp},
                                                         {"StopApp", ActionKind::StopApp},
                                                         {"Shell", ActionKind::Shell},
                                                         {"Inactive", ActionKind::Inactive}};
    auto kind = kinds.find(value.at("kind"));
    if (kind == kinds.end())
        throw std::runtime_error("ACTION_KIND_INVALID");
    return {kind->second,
            value.value("x", 0),
            value.value("y", 0),
            value.value("x2", 0),
            value.value("y2", 0),
            value.value("duration", 0),
            value.value("contact", 0),
            value.value("pressure", 0),
            value.value("key", 0),
            value.value("text", std::string{})};
}
void require_hit(const Observation &observation, const char *no_hit) {
    if (observation.outcome == RecognitionOutcome::Error)
        throw std::runtime_error(observation.error_code);
    if (observation.outcome != RecognitionOutcome::Hit || !observation.center)
        throw std::runtime_error(no_hit);
}
} // namespace
bool GuardedAction::execute(maafw::Context &context, devices::InputGate &gate,
                            storage::EventJournal &events, const nlohmann::json &p) {
    auto frame = context.capture();
    auto scene = context.recognize(frame, recognition(p.at("scene_recognition")));
    require_hit(scene, "SCENE_NOT_FOUND");
    auto target = context.recognize(frame, recognition(p.at("target_recognition")));
    require_hit(target, "TARGET_NOT_FOUND");
    const auto scene_name = p.at("scene").get<std::string>();
    gate.confirm_scene(scene, scene_name);
    auto input = command(p.at("command"));
    if (p.value("use_target_center", true) &&
        (input.kind == ActionKind::Click || input.kind == ActionKind::Swipe ||
         input.kind == ActionKind::TouchDown || input.kind == ActionKind::TouchMove)) {
        input.x = target.center->x;
        input.y = target.center->y;
    }
    auto area = p.at("allowed_area").get<std::vector<int>>();
    if (area.size() != 4)
        throw std::runtime_error("ACTION_AREA_INVALID");
    auto post = recognition(p.at("postcondition"));
    auto budget = p.value("postcondition_timeout_ms", 1000);
    if (budget < 1 || budget > 60000)
        throw std::runtime_error("POSTCONDITION_BUDGET_INVALID");
    const auto id = events.emit(gate.generation(), "intent.requested",
                                {{"frame_id", frame.identity.frame_id}, {"kind", int(input.kind)}});
    ActionIntent intent{
        id,    gate.run_id(), gate.generation(),  target,
        input, scene_name,    post.recognizer_id, {area[0], area[1], area[2], area[3]}};
    gate.authorize(intent);
    struct Permit {
        devices::InputGate &gate;
        ~Permit() {
            gate.revoke();
        }
    } permit{gate};
    bool sent = false;
    // Click/Swipe 使用真实 Maa 内置动作；其他入口经同一 Controller 队列，均由最终门禁再次检查。
    if (input.kind == ActionKind::Click)
        sent = context.native_action("Click", {{"target", {input.x, input.y, 1, 1}}});
    else if (input.kind == ActionKind::Swipe)
        sent = context.native_action("Swipe", {{"begin", {input.x, input.y, 1, 1}},
                                               {"end", {input.x2, input.y2, 1, 1}},
                                               {"duration", input.duration}});
    else
        sent = context.controller_action(input);
    if (!sent)
        return false;
    gate.revoke();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(budget);
    do {
        if (context.cancelled())
            return false;
        auto fresh = context.capture();
        auto observed = context.recognize(fresh, post);
        if (observed.outcome == RecognitionOutcome::Error)
            throw std::runtime_error(observed.error_code);
        if (observed.outcome == RecognitionOutcome::Hit) {
            events.emit(gate.generation(), "postcondition.confirmed",
                        {{"intent", id}, {"frame_id", fresh.identity.frame_id}});
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (std::chrono::steady_clock::now() < deadline);
    throw std::runtime_error("POSTCONDITION_TIMEOUT");
}
} // namespace wvd::runtime
