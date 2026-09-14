#include "guarded_action.hpp"
#include <algorithm>
#include <limits>
#include <thread>

namespace wvd::runtime {
using namespace contracts;
namespace {
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
    if (observation.outcome != RecognitionOutcome::Hit)
        throw std::runtime_error(no_hit);
}
} // namespace
bool GuardedAction::execute(maafw::Context &context, devices::InputGate &gate,
                            storage::EventJournal &events, const nlohmann::json &p) {
    auto frame = context.capture();
    auto scene =
        context.recognize(frame, maafw::parse_recognition_request(p.at("scene_recognition")));
    require_hit(scene, "SCENE_NOT_FOUND");
    auto target =
        context.recognize(frame, maafw::parse_recognition_request(p.at("target_recognition")));
    require_hit(target, "TARGET_NOT_FOUND");
    if (!target.action_eligible)
        throw std::runtime_error("TARGET_REQUIRES_CONFIRMATION");
    const auto scene_name = p.at("scene").get<std::string>();
    gate.confirm_scene(scene, scene_name);
    auto input = command(p.at("command"));
    if (p.value("use_target_center", true) &&
        (input.kind == ActionKind::Click || input.kind == ActionKind::Swipe ||
         input.kind == ActionKind::TouchDown || input.kind == ActionKind::TouchMove)) {
        if (!target.center)
            throw std::runtime_error("TARGET_NOT_POSITIONAL");
        input.x = target.center->x;
        input.y = target.center->y;
        if (p.contains("target_offset")) {
            const auto &value = p.at("target_offset");
            if (!value.is_array() || value.size() != 2 || !value[0].is_number_integer() ||
                !value[1].is_number_integer())
                throw std::runtime_error("TARGET_OFFSET_INVALID");
            for (const auto &part : value) {
                if ((part.is_number_unsigned() &&
                     part.get<std::uint64_t>() > std::numeric_limits<int>::max()) ||
                    (!part.is_number_unsigned() &&
                     (part.get<std::int64_t>() < std::numeric_limits<int>::min() ||
                      part.get<std::int64_t>() > std::numeric_limits<int>::max())))
                    throw std::runtime_error("TARGET_OFFSET_INVALID");
            }
            const auto offset = p.at("target_offset").get<std::vector<int>>();
            if (offset.size() != 2)
                throw std::runtime_error("TARGET_OFFSET_INVALID");
            auto x = static_cast<std::int64_t>(input.x) + offset[0];
            auto y = static_cast<std::int64_t>(input.y) + offset[1];
            if (p.value("clip_target_to_area", false)) {
                const auto bounds = p.at("allowed_area").get<std::vector<int>>();
                if (bounds.size() != 4 || bounds[0] < 0 || bounds[1] < 0 || bounds[2] <= 0 ||
                    bounds[3] <= 0 ||
                    bounds[0] > frame.identity.recognition_size.width - bounds[2] ||
                    bounds[1] > frame.identity.recognition_size.height - bounds[3])
                    throw std::runtime_error("TARGET_CLIP_AREA_INVALID");
                x = std::clamp(x, std::int64_t(bounds[0]), std::int64_t(bounds[0]) + bounds[2] - 1);
                y = std::clamp(y, std::int64_t(bounds[1]), std::int64_t(bounds[1]) + bounds[3] - 1);
            }
            if (x < std::numeric_limits<int>::min() || x > std::numeric_limits<int>::max() ||
                y < std::numeric_limits<int>::min() || y > std::numeric_limits<int>::max())
                throw std::runtime_error("TARGET_OFFSET_OVERFLOW");
            input.x = static_cast<int>(x);
            input.y = static_cast<int>(y);
        }
    }
    auto area = p.at("allowed_area").get<std::vector<int>>();
    if (area.size() != 4)
        throw std::runtime_error("ACTION_AREA_INVALID");
    auto post = maafw::parse_recognition_request(p.at("postcondition"));
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
        ~Permit() { gate.revoke(); }
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
            if (observed.basis.frame_id <= frame.identity.frame_id ||
                observed.basis.generation != frame.identity.generation)
                throw std::runtime_error("POSTCONDITION_FRAME_INVALID");
            gate.confirm_scene(observed, scene_name);
            events.emit(gate.generation(), "postcondition.confirmed",
                        {{"intent", id}, {"frame_id", fresh.identity.frame_id}});
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (std::chrono::steady_clock::now() < deadline);
    throw std::runtime_error("POSTCONDITION_TIMEOUT");
}
} // namespace wvd::runtime
