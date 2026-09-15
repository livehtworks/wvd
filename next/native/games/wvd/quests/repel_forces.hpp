#pragma once
#include <cstddef>
#include <stdexcept>
#include <json.hpp>

namespace wvd::games::quests {
// 每个正常段只承接入场、一组双战或返城；段终点不是整个任务完成。
class RepelForces {
  public:
    enum class Phase { Rest, Route, Pair, Exit, Return, Completed };
    static std::size_t rounds(const nlohmann::json &profile) {
        const auto value = profile.at("REST_INTERVEL").get<std::int64_t>();
        if (value < 0) throw std::runtime_error("REPEL_REST_INTERVAL_INVALID");
        const auto effective = !profile.at("ACTIVE_REST").get<bool>() || !value ? 1 : value;
        // 单Run最多256段，两段留给入场和返城；不静默截短用户设定。
        if (effective > 254) throw std::runtime_error("REPEL_UNIT_BUDGET_EXCEEDED");
        return static_cast<std::size_t>(effective);
    }
    std::size_t sequence(bool start = false) const { return cycles_started_ + (start && !active_ ? 1 : 0); }
    std::size_t fight_sequence(bool prepare = false) const { return fights_ + (prepare && !pending_ ? 1 : 0); }
    std::size_t pair_sequence() const { return pairs_; }
    void start(std::size_t unit, std::size_t rounds) {
        if (active_ || pending_ || !rounds || rounds > 254) throw std::runtime_error("REPEL_START_INVALID");
        base_ = unit; rounds_ = rounds; pairs_ = 0; in_pair_ = 0;
        active_ = true; phase_ = Phase::Rest; ++cycles_started_;
    }
    void rested(std::size_t unit, bool rested) {
        require(unit, Phase::Rest);
        if (!rested) throw std::runtime_error("REPEL_REST_NOT_CONFIRMED");
        phase_ = Phase::Route;
    }
    void arrived(std::size_t unit, std::size_t points) {
        require(unit, Phase::Route);
        if (points != 2) throw std::runtime_error("REPEL_ENTRY_ROUTE_INCOMPLETE");
        phase_ = Phase::Pair;
    }
    void prepare(std::size_t unit) {
        require(unit, Phase::Pair);
        if (pending_ || in_pair_ >= 2) throw std::runtime_error("REPEL_BATTLE_ALREADY_PENDING");
        pending_ = true; observed_ = false; ++fights_;
    }
    void observed(std::size_t unit) {
        require(unit, Phase::Pair);
        if (!pending_) throw std::runtime_error("REPEL_BATTLE_NOT_PREPARED");
        observed_ = true;
    }
    void fought(std::size_t unit) {
        require(unit, Phase::Pair);
        if (!pending_ || !observed_) throw std::runtime_error("REPEL_BATTLE_NOT_OBSERVED");
        pending_ = false; observed_ = false; ++in_pair_; ++confirmed_fights_;
    }
    void withdrawn(std::size_t unit) {
        require(unit, Phase::Pair);
        if (pending_ || in_pair_ != 2) throw std::runtime_error("REPEL_PAIR_INCOMPLETE");
        in_pair_ = 0; ++pairs_;
        if (pairs_ == rounds_) phase_ = Phase::Exit;
    }
    void exited(std::size_t unit, std::size_t points) {
        require(unit, Phase::Exit);
        if (points != 1) throw std::runtime_error("REPEL_EXIT_ROUTE_INCOMPLETE");
        phase_ = Phase::Return;
    }
    void complete(std::size_t unit) {
        require(unit, Phase::Return);
        if (pending_ || pairs_ != rounds_) throw std::runtime_error("REPEL_CYCLE_INCOMPLETE");
        active_ = false; phase_ = Phase::Completed; ++completed_;
    }
    nlohmann::json summary(std::size_t unit) const {
        return {{"phase", static_cast<int>(phase_)}, {"active", active_}, {"pending", pending_},
            {"battle_observed", observed_}, {"unit_matches", unit == expected_unit()},
            {"rounds", rounds_}, {"pairs_completed", pairs_}, {"battles_in_pair", in_pair_},
            {"battle_intents", fights_}, {"confirmed_battles", confirmed_fights_}, {"completed_cycles", completed_}};
    }
  private:
    std::size_t expected_unit() const {
        if (phase_ == Phase::Pair) return base_ + 1 + pairs_;
        if (phase_ == Phase::Exit || phase_ == Phase::Return || phase_ == Phase::Completed) return base_ + 1 + rounds_;
        return base_;
    }
    void require(std::size_t unit, Phase phase) const {
        if (!active_ || unit != expected_unit() || phase != phase_) throw std::runtime_error("REPEL_PHASE_INVALID");
    }
    Phase phase_{Phase::Completed};
    bool active_{}, pending_{}, observed_{};
    std::size_t base_{}, rounds_{}, pairs_{}, in_pair_{}, fights_{}, confirmed_fights_{}, cycles_started_{}, completed_{};
};
}
