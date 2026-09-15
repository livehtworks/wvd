#pragma once
#include <cstddef>
#include <stdexcept>
#include <json.hpp>

namespace wvd::games::quests {
class FordraigCycle {
  public:
    enum class Phase { Leap, Request, Enter, Trap1Route, Trap1Push, Trap2Route, Trap2Push,
                       Trap3, PreBoss, Boss, Exit, Return, Completed };
    static constexpr std::size_t units_per_cycle = 10;

    void start(std::size_t unit, std::size_t visits) {
        if (active_ || pending_ || unit > 256 - units_per_cycle || (sequence_ && unit != base_ + units_per_cycle))
            throw std::runtime_error("FORDRAIG_START_INVALID");
        base_ = unit; visits_ = visits; active_ = true; phase_ = Phase::Leap; ++sequence_;
    }
    void prepare(Phase phase, std::size_t unit) {
        require(phase, unit);
        if (pending_ || (phase != Phase::Leap && phase != Phase::Trap1Push && phase != Phase::Trap2Push))
            throw std::runtime_error("FORDRAIG_PREPARE_INVALID");
        pending_ = true;
    }
    void advance(Phase phase, std::size_t unit, std::size_t visits, std::size_t points) {
        require(phase, unit);
        const bool effect = phase == Phase::Leap || phase == Phase::Trap1Push || phase == Phase::Trap2Push;
        if (pending_ != effect) throw std::runtime_error("FORDRAIG_EFFECT_NOT_CONFIRMED");
        if (phase == Phase::Request && (visits <= visits_ || visits - visits_ != 1))
            throw std::runtime_error("FORDRAIG_REQUEST_MISSING");
        const auto required_points = route_points(phase);
        if (required_points && points != required_points) throw std::runtime_error("FORDRAIG_ROUTE_INCOMPLETE");
        pending_ = false;
        phase_ = static_cast<Phase>(static_cast<int>(phase) + 1);
        if (phase_ == Phase::Completed) { active_ = false; ++completed_; }
    }
    static std::size_t route_points(Phase phase) {
        switch (phase) {
        case Phase::Trap1Route: case Phase::Trap2Route: return 2;
        case Phase::Trap3: return 4;
        case Phase::PreBoss: case Phase::Boss: case Phase::Exit: return 1;
        default: return 0;
        }
    }
    bool force_automatic() const { return active_ && phase_ != Phase::Boss; }
    std::size_t sequence(bool start = false) const { return sequence_ + (start && !active_ ? 1 : 0); }
    bool continuation_ready(std::size_t completed_unit) const {
        if (!sequence_ || pending_) return false;
        if (!active_) return phase_ == Phase::Completed && completed_unit == base_ + units_per_cycle - 1;
        return expected_unit() > base_ && completed_unit == expected_unit() - 1 &&
            phase_ != Phase::Trap1Push && phase_ != Phase::Trap2Push;
    }
    nlohmann::json summary(std::size_t unit) const {
        return {{"active", active_}, {"phase", static_cast<int>(phase_)}, {"pending", pending_},
            {"leap_pending", pending_ && phase_ == Phase::Leap},
            {"trap_pending", pending_ && (phase_ == Phase::Trap1Push || phase_ == Phase::Trap2Push)},
            {"unit_matches", active_ && unit == expected_unit()}, {"expected_unit", expected_unit()},
            {"force_automatic", force_automatic()}, {"completed_cycles", completed_},
            {"sequence", sequence_}, {"continuation_ready", continuation_ready(unit)}};
    }
  private:
    std::size_t expected_unit() const {
        switch (phase_) {
        case Phase::Leap: return base_;
        case Phase::Request: return base_ + 1;
        case Phase::Enter: return base_ + 2;
        case Phase::Trap1Route: case Phase::Trap1Push: return base_ + 3;
        case Phase::Trap2Route: case Phase::Trap2Push: return base_ + 4;
        case Phase::Trap3: return base_ + 5;
        case Phase::PreBoss: return base_ + 6;
        case Phase::Boss: return base_ + 7;
        case Phase::Exit: return base_ + 8;
        case Phase::Return: case Phase::Completed: return base_ + 9;
        }
        throw std::runtime_error("FORDRAIG_PHASE_INVALID");
    }
    void require(Phase phase, std::size_t unit) const {
        if (!active_ || phase != phase_ || unit != expected_unit()) throw std::runtime_error("FORDRAIG_PHASE_INVALID");
    }
    Phase phase_{Phase::Completed};
    bool active_{}, pending_{};
    std::size_t base_{}, visits_{}, sequence_{}, completed_{};
};
}
