#pragma once
#include <cstddef>
#include <stdexcept>
#include <json.hpp>

namespace wvd::games::quests {
class GoldenChestCycle {
  public:
    enum class Phase { Leap, Travel, Request, Enter, Trap, Route, Exit, Completed };
    void start(std::size_t unit, std::size_t visits) {
        if (active_ || pending_) throw std::runtime_error("GOLDEN_CYCLE_ALREADY_ACTIVE");
        active_ = true;
        phase_ = Phase::Leap;
        unit_ = unit;
        visits_ = visits;
        ++sequence_;
    }
    void prepare_leap(std::size_t unit) {
        require(Phase::Leap, unit);
        if (pending_) throw std::runtime_error("GOLDEN_LEAP_ALREADY_PENDING");
        pending_ = true;
    }
    void advance(Phase phase, std::size_t unit, std::size_t visits, std::size_t points) {
        require(phase, unit);
        if (phase == Phase::Leap && !pending_) throw std::runtime_error("GOLDEN_LEAP_NOT_PREPARED");
        if (phase == Phase::Request && visits != visits_ + 1) throw std::runtime_error("GOLDEN_REQUEST_NOT_CONFIRMED");
        if (phase == Phase::Route && points != 6) throw std::runtime_error("GOLDEN_ROUTE_INCOMPLETE");
        pending_ = false;
        phase_ = static_cast<Phase>(static_cast<int>(phase) + 1);
        if (phase_ == Phase::Completed) { active_ = false; ++completed_; }
    }
    std::size_t sequence(bool starting = false) const { return sequence_ + (starting && !active_ ? 1 : 0); }
    nlohmann::json summary(std::size_t unit) const {
        return {{"active", active_}, {"phase", static_cast<int>(phase_)}, {"leap_pending", pending_},
            {"unit_matches", active_ && unit == expected_unit()}, {"completed_cycles", completed_}};
    }
  private:
    std::size_t expected_unit() const { return unit_ + (phase_ >= Phase::Enter ? 1 : 0); }
    void require(Phase phase, std::size_t unit) const {
        if (!active_ || phase_ != phase || unit != expected_unit()) throw std::runtime_error("GOLDEN_PHASE_INVALID");
    }
    bool active_{}, pending_{};
    Phase phase_{Phase::Completed};
    std::size_t unit_{}, visits_{}, sequence_{}, completed_{};
};
}
