#pragma once
#include <cstddef>
#include <stdexcept>
#include <json.hpp>

namespace wvd::games::quests {
// 一次寻缘访问不等于一次缘完成。只有指定选项回执及两次住宿/跳跃都完成才计数。
class SandmanCycle {
  public:
    enum class Phase { Enter, Route, Exit, Decide, RestDuke, LeapDuke, RestTriumph, LeapTriumph, Completed };
    void start(std::size_t unit) {
        if (active_ || pending_) throw std::runtime_error("SANDMAN_ALREADY_ACTIVE");
        active_ = true; bond_ = false; unit_ = unit; phase_ = Phase::Enter; ++sequence_; ++attempts_;
    }
    void bondmate() {
        if (!active_ || phase_ != Phase::Route) throw std::runtime_error("SANDMAN_BOND_OUTSIDE_ROUTE");
        bond_ = true;
    }
    void prepare_leap(std::size_t unit) {
        require(phase_, unit);
        if (pending_ || (phase_ != Phase::LeapDuke && phase_ != Phase::LeapTriumph))
            throw std::runtime_error("SANDMAN_LEAP_INVALID");
        pending_ = true;
    }
    void advance(Phase phase, std::size_t unit, bool rested, std::size_t points) {
        require(phase, unit);
        if (phase == Phase::Route && points != 3) throw std::runtime_error("SANDMAN_ROUTE_INCOMPLETE");
        if (phase == Phase::RestDuke || phase == Phase::RestTriumph)
            if (!rested) throw std::runtime_error("SANDMAN_REST_NOT_CONFIRMED");
        if (phase == Phase::LeapDuke || phase == Phase::LeapTriumph) {
            if (!pending_) throw std::runtime_error("SANDMAN_LEAP_NOT_PREPARED");
            pending_ = false;
        }
        phase_ = phase == Phase::Decide && !bond_ ? Phase::Completed : static_cast<Phase>(static_cast<int>(phase) + 1);
        if (phase_ == Phase::Completed) { active_ = false; if (bond_) ++completed_; }
    }
    std::size_t sequence(bool starting = false) const { return sequence_ + (starting && !active_ ? 1 : 0); }
    nlohmann::json summary(std::size_t unit) const {
        return {{"active", active_}, {"phase", static_cast<int>(phase_)}, {"unit_matches", active_ && unit == unit_},
            {"bondmate_confirmed", bond_}, {"leap_pending", pending_}, {"attempts", attempts_}, {"completed_cycles", completed_}};
    }
  private:
    void require(Phase phase, std::size_t unit) const {
        if (!active_ || phase_ != phase || unit != unit_) throw std::runtime_error("SANDMAN_PHASE_INVALID");
    }
    bool active_{}, pending_{}, bond_{};
    Phase phase_{Phase::Completed};
    std::size_t unit_{}, sequence_{}, attempts_{}, completed_{};
};
}
