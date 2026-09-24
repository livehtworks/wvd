#pragma once
#include "contracts/business_state.hpp"
#include <cstdint>
#include <optional>

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

} // namespace wvd::games::recovery
