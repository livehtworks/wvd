#pragma once
#include <cstddef>
#include <stdexcept>
#include <json.hpp>

namespace wvd::games::quests {
class BullCaveCycle {
  public:
    enum class Phase { Leap, Fortress, RoyalCity, Request, EnterFirst, FirstRoute, FirstExit, Rest, EnterSecond, SecondRoute, SecondExit, Completed };
    void start(std::size_t unit, std::size_t visits, bool rest) {
        if (active_ || pending_) throw std::runtime_error("BULL_CAVE_ALREADY_ACTIVE");
        active_ = true; rest_ = rest; phase_ = Phase::Leap; unit_ = unit; visits_ = visits; ++sequence_;
    }
    void prepare_leap(std::size_t unit) {
        require(Phase::Leap, unit);
        if (pending_) throw std::runtime_error("BULL_CAVE_ALREADY_PENDING");
        pending_ = true;
    }
    void advance(Phase phase, std::size_t unit, std::size_t visits, std::size_t points, bool rested) {
        require(phase, unit);
        if (phase == Phase::Leap && !pending_) throw std::runtime_error("BULL_CAVE_LEAP_NOT_PREPARED");
        if (phase == Phase::Request && visits != visits_ + 1) throw std::runtime_error("BULL_CAVE_REQUEST_MISSING");
        if ((phase == Phase::FirstRoute && points != (rest_ ? 1u : 3u)) || (phase == Phase::SecondRoute && points != 2))
            throw std::runtime_error("BULL_CAVE_ROUTE_INCOMPLETE");
        if (phase == Phase::Rest && !rested) throw std::runtime_error("BULL_CAVE_REST_MISSING");
        pending_ = false;
        phase_ = phase == Phase::FirstExit && !rest_ ? Phase::Completed : static_cast<Phase>(static_cast<int>(phase) + 1);
        if (phase_ == Phase::Completed) { active_ = false; ++completed_; }
    }
    std::size_t sequence(bool start = false) const { return sequence_ + (start && !active_ ? 1 : 0); }
    nlohmann::json summary(std::size_t unit) const {
        return {{"active", active_}, {"phase", static_cast<int>(phase_)}, {"rest_enabled", rest_},
            {"leap_pending", pending_}, {"unit_matches", active_ && unit == expected_unit()}, {"completed_cycles", completed_}};
    }
  private:
    std::size_t expected_unit() const { return unit_ + (phase_ >= Phase::EnterSecond ? 2 : phase_ >= Phase::EnterFirst ? 1 : 0); }
    void require(Phase phase, std::size_t unit) const {
        if (!active_ || phase_ != phase || unit != expected_unit()) throw std::runtime_error("BULL_CAVE_PHASE_INVALID");
    }
    bool active_{}, pending_{}, rest_{};
    Phase phase_{Phase::Completed};
    std::size_t unit_{}, visits_{}, sequence_{}, completed_{};
};
}
