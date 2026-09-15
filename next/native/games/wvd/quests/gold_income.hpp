#pragma once
#include <cstddef>
#include <stdexcept>
#include <json.hpp>

namespace wvd::games::quests {
class GoldIncomeCycle {
  public:
    enum class Phase { Leap, Fortress, RoyalCity, Accept, RoyalCapital, FirstPerson, SecondPerson, ThirdPerson, Refuse, Volunteer, Completed };
    void start(std::size_t unit) {
        if (active_ || pending_) throw std::runtime_error("GOLD_INCOME_ALREADY_ACTIVE");
        active_ = true; unit_ = unit; phase_ = Phase::Leap; ++sequence_;
    }
    void prepare(std::size_t unit) {
        require(unit);
        if (pending_) throw std::runtime_error("GOLD_INCOME_ALREADY_PENDING");
        pending_ = true;
    }
    void advance(std::size_t unit) {
        require(unit);
        if (!pending_) throw std::runtime_error("GOLD_INCOME_NOT_PREPARED");
        pending_ = false;
        phase_ = static_cast<Phase>(static_cast<int>(phase_) + 1);
        if (phase_ == Phase::Completed) { active_ = false; ++completed_; }
    }
    std::size_t sequence(bool start = false) const { return sequence_ + (start && !active_ ? 1 : 0); }
    int phase() const { return static_cast<int>(phase_); }
    nlohmann::json summary(std::size_t unit) const {
        return {{"active", active_}, {"pending", pending_}, {"phase", phase()}, {"unit_matches", active_ && unit == unit_},
            {"completed_cycles", completed_}, {"estimated_income", completed_ * 7000}, {"income_is_estimate", true}};
    }
  private:
    void require(std::size_t unit) const {
        if (!active_ || unit_ != unit || phase_ == Phase::Completed) throw std::runtime_error("GOLD_INCOME_PHASE_INVALID");
    }
    bool active_{}, pending_{};
    std::size_t unit_{}, sequence_{}, completed_{};
    Phase phase_{Phase::Completed};
};
}
