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
// 发起自动兜底不等于成功；只有后置画面已确认，才按旧逻辑消费已选条目。
enum class SkillOutcome { Succeeded, TargetFailed, Cancelled, AutoFallback, AutoFallbackConfirmed };
class CombatStrategy {
  public:
    explicit CombatStrategy(nlohmann::json profile);
    void reload(std::size_t task_step);
    void begin_encounter(bool special);
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
