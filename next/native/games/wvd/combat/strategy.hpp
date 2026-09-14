#pragma once
#include <json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace wvd::games {
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
enum class SkillOutcome { Succeeded, TargetFailed, Cancelled, AutoFallback };
class CombatStrategy {
  public:
    explicit CombatStrategy(nlohmann::json profile);
    void reload(std::size_t task_step);
    bool uses_task_points() const;
    bool automatic() const;
    std::optional<SkillSelection> select(const std::vector<PortraitScore> &scores) const;
    bool consume(const SkillSelection &selection, SkillOutcome outcome);
    nlohmann::json summary() const;

  private:
    const nlohmann::json profile_;
    const bool english_;
    nlohmann::json current_ = nlohmann::json::object();
    std::uint64_t epoch_{};
};
} // namespace wvd::games
