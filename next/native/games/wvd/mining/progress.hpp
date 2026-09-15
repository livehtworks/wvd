#pragma once
#include <array>
#include <optional>
#include <string_view>
#include <json.hpp>

namespace wvd::games::mining {
// 顺序来自旧CheckState的最高分候选；未知为最后一项，不强猜矿石种类。
inline constexpr std::array<std::string_view, 11> reward_names{
    "fine", "high", "mid", "low", "refine", "alter", "sliver", "ouro", "lesser_full", "full", "unknown"};
class Progress {
  public:
    bool observe_reward(std::size_t index);
    void reward_dismissed();
    void require_pickaxes();
    void party_assembled();
    void rest_completed();
    void complete_cycle();
    nlohmann::json summary() const;
    bool refill_pending() const { return refill_pending_; }
    bool party_ready() const { return party_ready_; }
    std::size_t reward_sequence(bool next = false) const { return reward_sequence_ + (next && !visible_reward_ ? 1 : 0); }
  private:
    std::array<std::size_t, reward_names.size()> counts_{};
    std::optional<std::size_t> visible_reward_;
    std::size_t reward_sequence_{}, completed_cycles_{};
    bool refill_pending_{}, party_ready_{};
};
}
