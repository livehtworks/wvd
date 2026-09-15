#pragma once
#include <cstddef>
#include <stdexcept>
#include <json.hpp>

namespace wvd::games::quests {
// 一次公会访问的持久业务事实；不保存图片位置，也不把点击回执当奖励到账。
class FeaturedVisit {
  public:
    void start() {
        if (active_ || pending_) throw std::runtime_error("FEATURED_VISIT_ALREADY_ACTIVE");
        active_ = true;
        ++sequence_;
    }
    void prepare() {
        if (!active_ || pending_) throw std::runtime_error("FEATURED_REQUEST_PREPARE_INVALID");
        pending_ = true;
    }
    void selected() {
        if (!active_ || !pending_) throw std::runtime_error("FEATURED_REQUEST_NOT_PREPARED");
        pending_ = false;
        ++selections_;
    }
    void finish(bool rested) {
        if (!active_ || pending_ || !rested) throw std::runtime_error("FEATURED_VISIT_INCOMPLETE");
        active_ = false;
        ++visits_;
    }
    std::size_t sequence(bool starting = false) const { return sequence_ + (starting && !active_ ? 1 : 0); }
    nlohmann::json summary() const {
        return {{"active", active_}, {"pending", pending_}, {"sequence", sequence_},
            {"selections_confirmed", selections_}, {"visits_completed", visits_}};
    }
  private:
    bool active_{}, pending_{};
    std::size_t sequence_{}, selections_{}, visits_{};
};
}
