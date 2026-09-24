#include "flow_events.hpp"
#include <algorithm>
#include <stdexcept>

namespace wvd::runtime {
using namespace contracts;
using J = nlohmann::json;

FlowEvents::FlowEvents(J scopes, devices::InputGate &gate, storage::EventJournal &journal,
                       std::function<void(const std::string &)> external_blocked)
    : scopes_(std::move(scopes)), gate_(gate), journal_(journal),
      external_blocked_(std::move(external_blocked)) {
    if (!scopes_.is_object()) throw std::runtime_error("EVENT_SCOPES_INVALID");
}

J FlowEvents::status() const {
    std::lock_guard lock(mutex_);
    if (active_stack_.empty()) {
        if (!pending_route_) return nullptr;
        return {{"event_id", pending_route_->event_id}, {"source_node", pending_route_->source_node},
                {"resume", {{"mode", "replan"}, {"node_id", pending_route_->target}}},
                {"phase", "replan_pending"}, {"depth", 0}};
    }
    auto active = active_stack_.back();
    active["path"] = active_stack_;
    return active;
}
bool FlowEvents::route_pending(const std::string &source_node, const std::string &event_id,
                               const std::string &target) const {
    std::lock_guard lock(mutex_);
    return pending_route_ && pending_route_->generation == gate_.generation() &&
        pending_route_->source_node == source_node && pending_route_->event_id == event_id &&
        pending_route_->target == target;
}
void FlowEvents::consume_route(const std::string &source_node, const std::string &event_id,
                               const std::string &target) {
    std::lock_guard lock(mutex_);
    if (!pending_route_ || pending_route_->generation != gate_.generation() ||
        pending_route_->source_node != source_node || pending_route_->event_id != event_id ||
        pending_route_->target != target)
        throw std::runtime_error("EVENT_ROUTE_STALE");
    pending_route_.reset();
}
bool FlowEvents::has_pending_route() const {
    std::lock_guard lock(mutex_);
    return pending_route_.has_value();
}
bool FlowEvents::route_pending_for_source(const std::string &source_node) const {
    std::lock_guard lock(mutex_);
    return pending_route_ && pending_route_->generation == gate_.generation() &&
        pending_route_->source_node == source_node;
}
bool FlowEvents::scope_enabled(const std::string &source_node) const {
    return scopes_.contains(source_node) && !scopes_.at(source_node).empty();
}

FlowEventResult FlowEvents::check(maafw::Context &context, const FrameEnvelope &frame,
    FlowEventPhase phase, const std::string &source_node, const std::string &expected_event) {
    if (context.cancelled()) return {FlowEventOutcome::Cancelled};
    if (!scopes_.contains(source_node)) return {};
    const auto &rules = scopes_.at(source_node);
    if (!rules.is_array()) throw std::runtime_error("EVENT_SCOPE_INVALID:" + source_node);
    std::vector<const J *> ordered;
    for (const auto &rule : rules)
        if (rule.at("class") == (phase == FlowEventPhase::Overlay ? "overlay" : "encounter"))
            ordered.push_back(&rule);
    std::stable_sort(ordered.begin(), ordered.end(), [](const J *a, const J *b) {
        return a->at("priority").get<int>() > b->at("priority").get<int>();
    });
    std::vector<const J *> hits;
    int highest = -1;
    for (const auto *rule : ordered) {
        const auto priority = rule->at("priority").get<int>();
        if (highest >= 0 && priority < highest) break;
        const J request{{"id", "event." + rule->at("id").get<std::string>()}, {"revision", "1"},
                        {"type", "custom"}, {"binding", "WvdVision"}, {"roi", {0, 0, 900, 1600}},
                        {"parameters", rule->at("detect")}};
        const auto observed = context.recognize(frame, maafw::parse_recognition_request(request));
        if (observed.outcome == RecognitionOutcome::Error)
            throw std::runtime_error(observed.error_code.empty() ? "EVENT_RECOGNITION_ERROR" : observed.error_code);
        if (observed.outcome == RecognitionOutcome::Hit) {
            highest = priority;
            hits.push_back(rule);
        }
    }
    const auto ambiguity_key = source_node + ":" + (phase == FlowEventPhase::Overlay ? "overlay" : "encounter");
    if (hits.empty()) {
        std::lock_guard lock(mutex_);
        ambiguous_since_.erase(ambiguity_key);
        return {};
    }
    if (hits.size() > 1) {
        const auto now = std::chrono::steady_clock::now();
        std::lock_guard lock(mutex_);
        auto [it, inserted] = ambiguous_since_.try_emplace(ambiguity_key, now);
        if (inserted) journal_.emit(gate_.generation(), "flow_event.ambiguous",
                                   {{"source_node", source_node}, {"priority", highest}});
        if (now - it->second > std::chrono::seconds(30))
            throw std::runtime_error("EVENT_AMBIGUOUS_TIMEOUT:" + source_node);
        return {FlowEventOutcome::Reobserve};
    }
    {
        std::lock_guard lock(mutex_);
        ambiguous_since_.erase(ambiguity_key);
    }
    const auto &rule = *hits.front();
    const auto event_id = rule.at("id").get<std::string>();
    if (!expected_event.empty() && expected_event != event_id)
        return {FlowEventOutcome::Reobserve};
    if (rule.value("disposition", "handled") == "external_blocked") {
        const auto reason = rule.at("reason").get<std::string>();
        journal_.emit(gate_.generation(), "flow_event.external_blocked",
                      {{"event_id", event_id}, {"source_node", source_node}, {"reason", reason}}, true);
        external_blocked_(reason);
        return {FlowEventOutcome::ExternalBlocked, event_id};
    }
    const auto start = std::chrono::steady_clock::now();
    const auto handler_nodes = rule.at("reset_hit_counts").get<std::set<std::string>>();
    const auto token = gate_.begin_event_scope(context.task_id(), event_id, handler_nodes);
    {
        std::lock_guard lock(mutex_);
        active_stack_.push_back({{"event_id", event_id}, {"class", rule.at("class")},
                   {"source_node", source_node}, {"handler_entry", rule.at("entry")},
                   {"resume", rule.at("resume")}, {"depth", gate_.event_depth()}});
    }
    journal_.emit(gate_.generation(), "flow_event.enter", status(), true);
    const auto child = context.run_child(rule.at("entry").get<std::string>(), J::object(), false,
        rule.at("reset_hit_counts").get<std::vector<std::string>>());
    if (context.cancelled()) return {FlowEventOutcome::Cancelled, event_id};
    if (!child.valid || child.status != MaaStatus_Succeeded)
        throw std::runtime_error("EVENT_HANDLER_FAILED:" + event_id);
    gate_.end_event_scope(token);
    const auto after = context.capture();
    const J request{{"id", "event.after." + event_id}, {"revision", "1"}, {"type", "custom"},
                    {"binding", "WvdVision"}, {"roi", {0, 0, 900, 1600}},
                    {"parameters", rule.at("detect")}};
    const auto remaining = context.recognize(after, maafw::parse_recognition_request(request));
    if (remaining.outcome == RecognitionOutcome::Error)
        throw std::runtime_error(remaining.error_code.empty() ? "EVENT_RETURN_RECOGNITION_ERROR" : remaining.error_code);
    if (remaining.outcome == RecognitionOutcome::Hit)
        throw std::runtime_error("EVENT_HANDLER_NO_PROGRESS:" + event_id);
    const auto mode = rule.at("resume").at("mode").get<std::string>();
    std::string target;
    if (mode == "replan") {
        target = rule.at("resume").at("node_id").get<std::string>();
        const J guard_request{{"id", "event.guard." + event_id}, {"revision", "1"}, {"type", "custom"},
                              {"binding", "WvdVision"}, {"roi", {0, 0, 900, 1600}},
                              {"parameters", rule.at("resume").at("guard")}};
        const auto guard = context.recognize(after, maafw::parse_recognition_request(guard_request));
        if (guard.outcome == RecognitionOutcome::Error)
            throw std::runtime_error(guard.error_code.empty() ? "EVENT_REPLAN_GUARD_ERROR" : guard.error_code);
        if (guard.outcome != RecognitionOutcome::Hit)
            throw std::runtime_error("EVENT_REPLAN_GUARD_MISSING:" + event_id);
        const auto receipt = gate_.settle_event_replan(context.task_id(), guard);
        if (receipt)
            journal_.emit(gate_.generation(), "flow_event.replan_receipt",
                {{"event_id", event_id}, {"intent", receipt->intent_id},
                 {"source_node", receipt->source_node}, {"result", "replanned_not_confirmed"}}, true);
        std::lock_guard lock(mutex_);
        if (pending_route_) throw std::runtime_error("EVENT_ROUTE_ALREADY_PENDING");
        pending_route_ = PendingRoute{source_node, event_id, target, gate_.generation()};
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    journal_.emit(gate_.generation(), "flow_event.exit",
        {{"event_id", event_id}, {"source_node", source_node}, {"outcome", mode},
         {"elapsed_ms", std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()}}, true);
    {
        std::lock_guard lock(mutex_);
        active_stack_.pop_back();
    }
    return {mode == "replan" ? FlowEventOutcome::Replan : FlowEventOutcome::Handled,
            event_id, target, elapsed};
}
} // namespace wvd::runtime
