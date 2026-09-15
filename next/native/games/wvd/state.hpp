#pragma once
#include "combat/strategy.hpp"
#include "chest/selection.hpp"
#include "contracts/business_state.hpp"
#include "runtime/behavior_registry.hpp"
#include "supply/policy.hpp"
#include "karma.hpp"
#include "mining/progress.hpp"
#include "quests/manual_separation.hpp"
#include "quests/sleep_visits.hpp"
#include "quests/bounty_cycle.hpp"
#include "quests/featured_visit.hpp"
#include "quests/golden_chest.hpp"
#include "quests/sandman.hpp"
#include "quests/gold_income.hpp"
#include "quests/bull_cave.hpp"
#include "quests/steel_trial.hpp"
#include "fishing/progress.hpp"
#include <map>

namespace wvd::games {
// 游戏层仅存业务事实，不保存 Controller、图像、坐标或识别结果。
class WvdRunState final : public contracts::BusinessRunState {
  public:
    WvdRunState(nlohmann::json profile, const contracts::StateCreationContext &creation,
                std::unique_ptr<KarmaCommitPort> karma_writer = {});
    void enter_dungeon();
    void target_point_completed();
    void observe_combat();
    void observe_chest();
    void prepare_chest_character(const std::array<bool, 6> &fear, int preferred, std::uint32_t seed);
    void resume_dungeon();
    bool healing_required() const;
    void resurrected();
    void restart_game();
    void dungeon_completed();
    void bag_clear_completed();
    supply::RestDecision rest_decision(bool pickaxes_exhausted = false) const;
    std::optional<SkillSelection> select_skill(const std::vector<PortraitScore> &scores) const;
    bool confirm_skill(const SkillSelection &, SkillOutcome);
    void prepare_skill(const std::vector<PortraitScore> &, const nlohmann::json &catalog);
    bool finish_prepared_skill(std::size_t index, SkillOutcome);
    std::string confirmation_id(const std::string &operation, const std::string &event) const;
    // 回放同一操作不重复修改业务；相同 ID 的不同效果拒绝。观察仍须来自当前代次。
    bool confirm_event(const std::string &operation, const std::string &event,
                       std::uint64_t generation, std::uint64_t frame_id,
                       std::optional<std::size_t> expected_step = {},
                       std::optional<std::size_t> reward_index = {});

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
        met_encounter_{}, combat_speed_{}, zoom_world_map_{};
    std::size_t wall_bypass_step_{3}, wall_bypass_sequence_{};
    std::size_t trap_cycles_completed_{};
    std::optional<std::size_t> trap_unit_;
    std::optional<std::size_t> giant_unit_;
    bool giant_route_completed_{};
    std::size_t giant_cycles_completed_{};
    bool giant_rest_due() const;
    bool encounter_timed_out() const;
    bool dark_light_active_{};
    mining::Progress mining_;
    fishing::Progress fishing_;
    quests::ManualSeparation manual_separation_;
    quests::SleepVisits sleep_;
    quests::BountyCycle bounty_cycle_;
    std::size_t bounty_reports_{};
    std::size_t bounty_reveals_{};
    bool bounty_report_pending_{};
    bool special_dialogue_pending_{};
    std::size_t special_dialogue_sequence_{}, special_dialogues_completed_{};
    quests::FeaturedVisit featured_visit_;
    quests::GoldenChestCycle golden_chest_;
    quests::SandmanCycle sandman_;
    quests::GoldIncomeCycle gold_income_;
    quests::BullCaveCycle bull_cave_;
    quests::SteelTrial steel_trial_;
    bool setting_is(const char *name, const char *zh, const char *en) const;
    std::map<std::string, nlohmann::json> confirmations_;
    nlohmann::json last_confirmation_;
    std::optional<SkillSelection> prepared_;
    std::size_t prepared_index_{};
    std::string prepared_portrait_;
    bool lifecycle_recovery_active_{};
    std::size_t lifecycle_recovery_sequence_{};
    bool healing_pending_{}, healing_active_{};
    std::size_t healing_sequence_{};
    bool inn_rest_completed_{}, inn_payment_pending_{};
    std::size_t supply_cycle_{}, inn_rests_{};
    enum class Encounter { None, Combat, Chest } last_encounter_{Encounter::None};
    // 遭遇编号不等于成功次数：复活取消一次待计数事件后仍不能重用其幂等 ID。
    std::size_t combat_sequence_{}, chest_sequence_{}, revival_sequence_{}, revivals_{};
    bool revival_pending_{};
    // 队友死亡提示不等于全队复活：只重置策略，不撤销遭遇计数或设置战后恢复。
    std::size_t death_prompt_sequence_{};
    bool death_prompt_pending_{};
    bool suicide_requested_{};
    std::size_t party_defeat_sequence_{};
    chest::Selection chest_selection_;
    std::unique_ptr<KarmaCommitPort> karma_writer_;
    std::string karma_value_;
    std::optional<KarmaChoice> karma_choice_;
    std::size_t karma_sequence_{};
    nlohmann::json karma_effect_;
};
void register_wvd_state(runtime::BehaviorRegistry &registry);
contracts::BehaviorBinding wvd_state_binding(const nlohmann::json &profile);
} // namespace wvd::games
