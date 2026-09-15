#pragma once
#include "contracts/business_state.hpp"
#include "runtime/run_coordinator.hpp"

namespace wvd::games::recovery {
// 旧 Sleep(7300) 属于恢复等待，不算任务完成，也不消耗正常业务续段。
class LeapWait {
  public:
    using TimePoint = contracts::MonotonicClock::TimePoint;
    static constexpr std::chrono::seconds duration{7300};
    static constexpr std::chrono::seconds slice{1460};
    static constexpr std::size_t max_slices = 5;
    void begin(TimePoint now);
    bool poll(TimePoint now, std::uint64_t generation);
    void restarted(TimePoint now);
    nlohmann::json summary(TimePoint now) const;

  private:
    std::optional<TimePoint> started_, last_poll_;
    std::uint64_t generation_{};
    std::size_t slices_{};
    std::chrono::seconds deadline_{};
    bool active_{};
    std::chrono::milliseconds elapsed(TimePoint now) const;
};

void register_leap_wait(runtime::BehaviorRegistry &registry);
// 在发布之前合入同一个 Pipeline；节点名固定，不能放进可重复的原生子任务。
nlohmann::json leap_wait_nodes();
// 在 Run 冻结之前调用；五个等待代次另占恢复预算，总预算仍不超过核心的 16。
void bind_leap_wait(runtime::RunDefinition &definition);
runtime::SessionDefinition leap_wait_session(const runtime::SessionDefinition &previous);
void restore_leap_session_budget(runtime::SessionDefinition &session);
} // namespace wvd::games::recovery
