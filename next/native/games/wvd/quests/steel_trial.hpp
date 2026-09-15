#pragma once
#include <cstddef>
#include <stdexcept>
#include <json.hpp>

namespace wvd::games::quests {
// 源码扩展steeltrail的业务回执；不保存画面，不把开始试炼当成试炼完成。
class SteelTrial {
  public:
    enum class Phase { Request, Route, Return, Rest, Completed };
    std::size_t sequence(bool starting = false) const { return attempts_ + (starting && !active_ ? 1 : 0); }
    void start(std::size_t unit, bool rest_due) {
        if (active_ || pending_) throw std::runtime_error("STEEL_TRIAL_ALREADY_ACTIVE");
        unit_ = unit; rest_due_ = rest_due; active_ = true; phase_ = Phase::Request; ++attempts_;
    }
    void prepare(std::size_t unit) {
        require(unit, Phase::Request);
        if (pending_) throw std::runtime_error("STEEL_TRIAL_ALREADY_PENDING");
        pending_ = true;
    }
    void entered(std::size_t unit) {
        require(unit, Phase::Request);
        if (!pending_) throw std::runtime_error("STEEL_TRIAL_NOT_PREPARED");
        pending_ = false; phase_ = Phase::Route;
    }
    void routed(std::size_t unit, std::size_t points) {
        require(unit, Phase::Route);
        if (points != 4) throw std::runtime_error("STEEL_TRIAL_ROUTE_INCOMPLETE");
        phase_ = Phase::Return;
    }
    void returned(std::size_t unit) { require(unit, Phase::Return); phase_ = Phase::Rest; }
    void complete(std::size_t unit, bool rested) {
        require(unit, Phase::Rest);
        if (rest_due_ && !rested) throw std::runtime_error("STEEL_TRIAL_REST_REQUIRED");
        phase_ = Phase::Completed; active_ = false; ++completed_;
    }
    nlohmann::json summary(std::size_t unit) const {
        return {{"phase", static_cast<int>(phase_)}, {"active", active_}, {"pending", pending_},
            {"rest_due", rest_due_}, {"unit_matches", unit == unit_}, {"attempts", attempts_}, {"completed_cycles", completed_}};
    }
  private:
    void require(std::size_t unit, Phase phase) const {
        if (!active_ || unit != unit_ || phase != phase_) throw std::runtime_error("STEEL_TRIAL_PHASE_INVALID");
    }
    bool active_{}, pending_{}, rest_due_{};
    std::size_t unit_{}, attempts_{}, completed_{};
    Phase phase_{Phase::Completed};
};
}
