#pragma once
#include "combat/strategy.hpp"
#include "contracts/business_state.hpp"
#include "runtime/behavior_registry.hpp"
#include "supply/policy.hpp"

namespace wvd::games {
// 游戏层仅存业务事实，不保存 Controller、图像、坐标或识别结果。
class WvdRunState final : public contracts::BusinessRunState {
  public:
    WvdRunState(nlohmann::json profile, const contracts::StateCreationContext &creation);
    void enter_dungeon();
    void target_point_completed();
    void observe_combat();
    void observe_chest();
    void resume_dungeon();
    void resurrected();
    void restart_game();
    void dungeon_completed();
    void bag_clear_completed();
    supply::RestDecision rest_decision(bool pickaxes_exhausted = false) const;
    std::optional<SkillSelection> select_skill(const std::vector<PortraitScore> &scores) const;
    bool confirm_skill(const SkillSelection &, SkillOutcome);

  protected:
    void on_segment(contracts::SegmentBoundary, std::uint64_t, std::size_t) override;
    nlohmann::json summarize() const override;

  private:
    using TimePoint = contracts::MonotonicClock::TimePoint;
    const nlohmann::json profile_;
    const std::string identity_;
    const std::shared_ptr<const contracts::MonotonicClock> clock_;
    CombatStrategy strategy_;
    std::uint64_t generation_{};
    std::size_t unit_index_{}, task_step_{}, dungeons_{}, combats_{}, chests_{}, crashes_{},
        last_bag_clear_{};
    TimePoint started_;
    std::optional<TimePoint> combat_started_, chest_started_, lap_started_;
    double combat_seconds_{}, chest_seconds_{}, total_seconds_{};
    bool pending_combat_{}, pending_chest_{}, need_initial_recover_{true}, recover_after_rez_{},
        met_encounter_{}, combat_speed_{}, zoom_world_map_{}, bypass_after_restart_{true};
    bool setting_is(const char *name, const char *zh, const char *en) const;
};
void register_wvd_state(runtime::BehaviorRegistry &registry);
contracts::BehaviorBinding wvd_state_binding(const nlohmann::json &profile);
} // namespace wvd::games
