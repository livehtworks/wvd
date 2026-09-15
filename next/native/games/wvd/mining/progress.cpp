#include "progress.hpp"
#include <stdexcept>

namespace wvd::games::mining {
bool Progress::observe_reward(std::size_t index) {
    if (index >= counts_.size()) throw std::runtime_error("MINING_REWARD_INVALID");
    if (visible_reward_) {
        if (*visible_reward_ != index) throw std::runtime_error("MINING_REWARD_CHANGED_WITHOUT_DISMISSAL");
        return false;
    }
    visible_reward_ = index;
    ++reward_sequence_;
    ++counts_[index];
    return true;
}
void Progress::reward_dismissed() { visible_reward_.reset(); }
void Progress::require_pickaxes() {
    if (!refill_pending_) party_ready_ = false;
    refill_pending_ = true;
}
void Progress::party_assembled() {
    if (!refill_pending_) throw std::runtime_error("MINING_REFILL_NOT_REQUESTED");
    party_ready_ = true;
}
void Progress::rest_completed() {
    if (!refill_pending_ || !party_ready_) throw std::runtime_error("MINING_REFILL_ORDER_INVALID");
    refill_pending_ = party_ready_ = false;
}
void Progress::complete_cycle() {
    if (refill_pending_ || visible_reward_) throw std::runtime_error("MINING_CYCLE_NOT_READY");
    ++completed_cycles_;
}
nlohmann::json Progress::summary() const {
    nlohmann::json counts = nlohmann::json::object();
    for (std::size_t i = 0; i < counts_.size(); ++i) counts[std::string(reward_names[i])] = counts_[i];
    return {{"rewards", counts}, {"reward_sequence", reward_sequence_}, {"reward_visible", visible_reward_.has_value()},
        {"refill_pending", refill_pending_}, {"party_ready", party_ready_}, {"completed_cycles", completed_cycles_}};
}
}
