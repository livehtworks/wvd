#pragma once

#include "contracts/flow_event.hpp"
#include "devices/input_gate.hpp"
#include "maafw/gateway.hpp"
#include <functional>
#include <mutex>

namespace wvd::runtime {
// 只管理当前 Maa Context 的事件调用与返回事实；业务图仍由 Maa 执行。
class FlowEvents {
  public:
    using J = nlohmann::json;
    FlowEvents(J scopes, devices::InputGate &gate, storage::EventJournal &journal,
               std::function<void(const std::string &)> external_blocked);
    contracts::FlowEventResult check(maafw::Context &context, const contracts::FrameEnvelope &frame,
        contracts::FlowEventPhase phase, const std::string &source_node,
        const std::string &expected_event = {});
    J status() const;
    bool route_pending(const std::string &source_node, const std::string &event_id,
                       const std::string &target) const;
    void consume_route(const std::string &source_node, const std::string &event_id,
                       const std::string &target);
    bool has_pending_route() const;
    bool route_pending_for_source(const std::string &source_node) const;
    bool scope_enabled(const std::string &source_node) const;

  private:
    J scopes_;
    devices::InputGate &gate_;
    storage::EventJournal &journal_;
    std::function<void(const std::string &)> external_blocked_;
    mutable std::mutex mutex_;
    std::vector<J> active_stack_;
    struct PendingRoute { std::string source_node, event_id, target; std::uint64_t generation{}; };
    std::optional<PendingRoute> pending_route_;
    std::map<std::string, std::chrono::steady_clock::time_point> ambiguous_since_;
};
} // namespace wvd::runtime
