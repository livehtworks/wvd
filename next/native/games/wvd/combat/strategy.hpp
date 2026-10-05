#pragma once
#include <json.hpp>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace wvd::games {
// Compile only groups selectable by this frozen run, including enabled encounter overrides.
std::set<std::string> reachable_strategy_groups(const nlohmann::json &profile);
struct PortraitScore {
    std::string portrait;
    double score;
};
struct SkillSelection {
    std::string run_identity;
    std::uint64_t generation{}, strategy_epoch{};
    std::size_t row{};
    nlohmann::json skill;
};
// 发起自动兜底不等于成功；后置确认后按频次结算。consume返回确认成功，不代表删行。
enum class SkillOutcome { Succeeded, TargetFailed, Cancelled, AutoFallback, AutoFallbackConfirmed, DefendFallbackConfirmed };
class CombatStrategy {
  public:
    explicit CombatStrategy(nlohmann::json profile);
    void reload(std::size_t task_step);
    void begin_encounter(bool special, const std::string &enemy_rule = {});
    bool uses_task_points() const;
    bool automatic() const;
    std::optional<SkillSelection> select(const std::vector<PortraitScore> &scores) const;
    bool consume(const SkillSelection &selection, SkillOutcome outcome);
    nlohmann::json summary() const;

  private:
    void load_group(const std::string &key);
    const nlohmann::json profile_;
    const bool english_;
    nlohmann::json current_ = nlohmann::json::object();
    std::uint64_t epoch_{};
};
} // namespace wvd::games
