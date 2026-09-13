#pragma once
#include "contracts/run.hpp"
#include "maafw/gateway.hpp"
#include <condition_variable>
#include <thread>

namespace wvd::runtime {
struct SessionDefinition {
    maafw::Bundle bundle;
    std::string entry, terminal_node;
    maafw::ActionRegistry actions;
    std::chrono::milliseconds time_limit{60000}, stop_timeout{3000};
};
class ExecutionSession {
  public:
    ExecutionSession(SessionDefinition definition, devices::DeviceBackend &backend,
                     contracts::InputPolicy policy, std::uint64_t run, std::uint64_t generation,
                     storage::EventJournal &events);
    ~ExecutionSession();
    void start();
    void request_stop();
    bool wait_for(std::chrono::milliseconds duration);
    bool running() const {
        return running_.load();
    }
    contracts::SessionResult join();
    contracts::InputCounts counts() const {
        return gate_.counts();
    }

  private:
    void execute() noexcept;
    void fail(const std::string &reason);
    bool cancelled() const;
    SessionDefinition definition_;
    storage::EventJournal &events_;
    devices::InputGate gate_;
    std::atomic<bool> started_{false}, user_stop_{false}, abort_{false}, recovery_{false},
        running_{false};
    std::mutex mutex_;
    std::condition_variable cv_;
    bool done_{};
    contracts::SessionResult result_;
    std::thread worker_;
};
} // namespace wvd::runtime
