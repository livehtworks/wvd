#pragma once
#include "behavior_registry.hpp"
#include "contracts/run.hpp"
#include "maafw/gateway.hpp"
#include <condition_variable>
#include <thread>

namespace wvd::runtime {
class FlowEvents;
class ExecutionSession {
  public:
    ExecutionSession(SessionDefinition definition, devices::DeviceBackend &backend,
                     contracts::InputPolicy policy, std::uint64_t run, std::uint64_t generation,
                     storage::EventJournal &events,
                     std::shared_ptr<const BehaviorRegistry> registry,
                     contracts::BusinessRunState *business = nullptr,
                     contracts::SegmentBoundary boundary = contracts::SegmentBoundary::Initial,
                     storage::RunStore *diagnostic_store = nullptr, std::size_t unit_index = 0);
    ~ExecutionSession();
    void start();
    void request_stop();
    bool wait_for(std::chrono::milliseconds duration);
    bool running() const { return running_.load(); }
    contracts::SessionResult join();
    contracts::InputCounts counts() const { return gate_.counts(); }
    nlohmann::json event_status() const;

  private:
    void execute() noexcept;
    void fail(const std::string &reason);
    bool cancelled() const;
    void prepare_lifecycle();
    SessionDefinition definition_;
    std::shared_ptr<const BehaviorRegistry> registry_;
    contracts::BusinessRunState *business_;
    storage::EventJournal &events_;
    storage::RunStore *diagnostic_store_;
    std::size_t unit_index_{};
    devices::InputGate gate_;
    devices::DeviceBackend &backend_;
    std::atomic<bool> started_{false}, user_stop_{false}, abort_{false}, recovery_{false}, external_blocked_{false},
        running_{false};
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool done_{};
    contracts::SessionResult result_;
    std::shared_ptr<FlowEvents> flow_events_;
    std::thread worker_;
};
} // namespace wvd::runtime
