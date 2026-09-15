#pragma once
#include <cstddef>
#include <json.hpp>

namespace wvd::games::quests {
// 悬赏阶段只存已确认事实。截图/输入与正常续段分别属于Maa和RunCoordinator。
class BountyCycle {
  public:
    enum class Phase { Leap, Travel, Reveal, FirstRoute, FirstReturn, SecondRoute, SecondReturn, Reports, Rest, Completed };
    bool active() const { return active_; }
    bool transfer_pending() const { return transfer_pending_; }
    std::size_t sequence() const { return sequence_; }
    void start(std::size_t unit, bool hands, bool rest_due, std::size_t reports, std::size_t reveals);
    void prepare_transfer(std::size_t unit, Phase);
    void transferred(std::size_t unit, Phase);
    void skip_travel(std::size_t unit);
    void revealed(std::size_t unit, std::size_t reveals);
    void route_completed(std::size_t unit, std::size_t points);
    void returned(std::size_t unit);
    void reported(std::size_t unit, std::size_t reports);
    void complete(std::size_t unit, bool rested);
    nlohmann::json summary(std::size_t unit, std::size_t reports) const;
  private:
    void require_phase(std::size_t unit, Phase expected) const;
    bool active_{}, hands_{}, rest_due_{}, transfer_pending_{};
    Phase phase_{Phase::Completed};
    std::size_t start_unit_{}, sequence_{}, cycles_{}, initial_reports_{}, initial_reveals_{};
};
}
