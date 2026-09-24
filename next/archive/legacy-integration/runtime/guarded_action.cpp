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
    FrameEnvelope frame;
    bool submission_armed = false;
    try {
        if (p.value("input_contract", 0) != 2)
            throw std::runtime_error("INPUT_PIPELINE_REPUBLISH_REQUIRED");
        for (;;) {
        if (context.cancelled()) return false;
        frame = context.capture();
        const auto overlay = context.check_events(frame, FlowEventPhase::Overlay);
        if (overlay.outcome == FlowEventOutcome::ExternalBlocked ||
            overlay.outcome == FlowEventOutcome::Cancelled) return false;
        if (overlay.outcome == FlowEventOutcome::Replan) return true;
        if (overlay.outcome == FlowEventOutcome::Handled || overlay.outcome == FlowEventOutcome::Reobserve) {
            if (overlay.outcome == FlowEventOutcome::Reobserve) std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        auto scene =
            context.recognize(frame, maafw::parse_recognition_request(p.at("scene_recognition")));
        if (scene.outcome == RecognitionOutcome::NoHit) {
            const auto extra = context.check_events(frame, FlowEventPhase::Encounter);
            if (extra.outcome == FlowEventOutcome::ExternalBlocked ||
                extra.outcome == FlowEventOutcome::Cancelled) return false;
            if (extra.outcome == FlowEventOutcome::Replan) return true;
            if (extra.outcome == FlowEventOutcome::Handled || extra.outcome == FlowEventOutcome::Reobserve) {
                if (extra.outcome == FlowEventOutcome::Reobserve) std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
        }
        require_hit(scene, "SCENE_NOT_FOUND");
        auto target = p.at("target_recognition") == p.at("scene_recognition")
            ? scene
            : context.recognize(frame, maafw::parse_recognition_request(p.at("target_recognition")));
        if (target.outcome == RecognitionOutcome::NoHit) {
            const auto extra = context.check_events(frame, FlowEventPhase::Encounter);
            if (extra.outcome == FlowEventOutcome::ExternalBlocked ||
                extra.outcome == FlowEventOutcome::Cancelled) return false;
            if (extra.outcome == FlowEventOutcome::Replan) return true;
            if (extra.outcome == FlowEventOutcome::Handled || extra.outcome == FlowEventOutcome::Reobserve) {
                if (extra.outcome == FlowEventOutcome::Reobserve) std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
        }
        require_hit(target, "TARGET_NOT_FOUND");
        if (!target.action_eligible)
            throw std::runtime_error("TARGET_REQUIRES_CONFIRMATION");
        if (!gate.input_observation_current(scene) || !gate.input_observation_current(target))
            throw std::runtime_error("INPUT_EVIDENCE_EXPIRED");
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
        const auto id = events.emit(gate.generation(), "intent.requested",
                                    {{"frame_id", frame.identity.frame_id}, {"kind", int(input.kind)}});
        ActionIntent intent{
            id,    gate.run_id(), gate.generation(),  target,
            input, scene_name,    post.recognizer_id, {area[0], area[1], area[2], area[3]}};
        gate.begin_submission(context.node(), context.task_id(), intent, p.at("postcondition").dump());
        submission_armed = true;
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
        gate.revoke();
        gate.finish_submission(sent);
        submission_armed = false;
        if (!sent) {
            if (!context.cancelled())
                throw std::runtime_error("INPUT_SUBMISSION_UNCONFIRMED");
            return false;
        }
        const auto receipt = gate.pending_submission(context.node(), context.task_id(), p.at("postcondition").dump());
        events.emit(gate.generation(), "input.submitted",
            {{"intent", id}, {"source_node", context.node()}, {"business_confirmed", false},
             {"frame_id", receipt.before.frame_id}, {"action_epoch", receipt.action_epoch},
             {"submitted_at_ns", std::chrono::duration_cast<std::chrono::nanoseconds>(receipt.submitted_at.time_since_epoch()).count()}});
        // 这里只提交一次输入。转场由后继 AwaitTransition 持有，绝不在此等待网络/页面。
        return !context.cancelled();
        }
    } catch (const std::exception &error) {
        // Permit先随异常展开撤销；关门后才同步写盘，绝不延长输入许可的寿命。
        gate.close();
        if (submission_armed) { try { gate.finish_submission(false); } catch (...) {} }
        const auto *evidence = frame.encoded_image.empty() ? nullptr : &frame;
        context.save_diagnostic(evidence, error.what(), "input_submission");
        throw;
    } catch (...) {
        gate.close();
        if (submission_armed) { try { gate.finish_submission(false); } catch (...) {} }
        const auto *evidence = frame.encoded_image.empty() ? nullptr : &frame;
        context.save_diagnostic(evidence, "CUSTOM_ACTION_EXCEPTION", "input_submission");
        throw;
    }
}
} // namespace wvd::runtime

namespace wvd::runtime {
bool GuardedAction::await_transition(maafw::Context &context, devices::InputGate &gate,
    storage::EventJournal &events, const nlohmann::json &p) {
    using Clock = std::chrono::steady_clock;
    using namespace std::chrono_literals;
    std::optional<contracts::FrameEnvelope> last;
    try {
        if (p.value("input_contract", 0) != 2)
            throw std::runtime_error("TRANSITION_CONTRACT_INVALID");
        const auto source = p.at("source_node").get<std::string>();
        if (context.event_replan_pending(source)) return true;
        const auto request = maafw::parse_recognition_request(p.at("postcondition"));
        const auto receipt = gate.pending_submission(source, context.task_id(), p.at("postcondition").dump());
        const auto budget = p.at("observation_budget_ms").get<std::int64_t>();
        const auto delay = p.value("initial_delay_ms", std::int64_t{0});
        const auto interval = p.value("poll_interval_ms", std::int64_t{50});
        if (budget < 1 || budget > 24LL * 60 * 60 * 1000 || delay < 0 || delay > 10000 ||
            interval < 10 || interval > 10000 || delay >= budget)
            throw std::runtime_error("TRANSITION_BUDGET_INVALID");
        // 转场总预算从输入完成开始。识别已取得的有效结果不再套输入寿命。
        auto deadline = std::min(receipt.submitted_at + std::chrono::milliseconds(budget),
                                 gate.observation_deadline());
        auto cancellable_wait = [&](Clock::time_point until) {
            while (!context.cancelled() && Clock::now() < until)
                std::this_thread::sleep_for(std::min(25ms,
                    std::max(1ms, std::chrono::duration_cast<std::chrono::milliseconds>(until - Clock::now()))));
            return !context.cancelled();
        };
        if (!cancellable_wait(std::min(deadline, receipt.submitted_at + std::chrono::milliseconds(delay)))) return false;
        events.emit(gate.generation(), "transition.waiting",
            {{"intent", receipt.intent_id}, {"source_node", source}, {"budget_ms", budget}});
        while (!context.cancelled() && Clock::now() < deadline) {
            last = context.capture();
            const auto overlay = context.check_events(*last, FlowEventPhase::Overlay, source);
            if (overlay.outcome == FlowEventOutcome::ExternalBlocked ||
                overlay.outcome == FlowEventOutcome::Cancelled) return false;
            if (overlay.outcome == FlowEventOutcome::Replan) return true;
            if (overlay.outcome == FlowEventOutcome::Handled || overlay.outcome == FlowEventOutcome::Reobserve) {
                if (overlay.outcome == FlowEventOutcome::Handled)
                    deadline = std::min(deadline + overlay.elapsed, gate.observation_deadline());
                else std::this_thread::sleep_for(50ms);
                continue;
            }
            const auto observed = context.recognize(*last, request);
            if (context.cancelled()) return false;
            if (observed.outcome == contracts::RecognitionOutcome::Error)
                throw std::runtime_error(observed.error_code);
            if (observed.outcome == contracts::RecognitionOutcome::Hit) {
                if (!gate.confirm_transition(receipt, observed))
                    throw std::runtime_error("TRANSITION_EVIDENCE_MISMATCH");
                events.emit(gate.generation(), "transition.observed",
                    {{"intent", receipt.intent_id}, {"source_node", source},
                     {"frame_id", observed.basis.frame_id}, {"business_confirmed", false}});
                return true;
            }
            const auto extra = context.check_events(*last, FlowEventPhase::Encounter, source);
            if (extra.outcome == FlowEventOutcome::ExternalBlocked ||
                extra.outcome == FlowEventOutcome::Cancelled) return false;
            if (extra.outcome == FlowEventOutcome::Replan) return true;
            if (extra.outcome == FlowEventOutcome::Handled || extra.outcome == FlowEventOutcome::Reobserve) {
                if (extra.outcome == FlowEventOutcome::Handled)
                    deadline = std::min(deadline + extra.elapsed, gate.observation_deadline());
                else std::this_thread::sleep_for(50ms);
                continue;
            }
            if (!cancellable_wait(std::min(deadline, Clock::now() + std::chrono::milliseconds(interval))))
                return false;
        }
        if (context.cancelled()) return false;
        // 输入已经提交，结果仍不确定。禁止在这里重发输入或请求重启；保留原操作证据。
        throw std::runtime_error("TRANSITION_RESULT_UNCONFIRMED");
    } catch (const std::exception &error) {
        gate.close();
        context.save_diagnostic(last ? &*last : nullptr, error.what(), "transition_observation");
        throw;
    } catch (...) {
        gate.close();
        context.save_diagnostic(last ? &*last : nullptr, "TRANSITION_OBSERVATION_EXCEPTION", "transition_observation");
        throw;
    }
}
} // namespace wvd::runtime
