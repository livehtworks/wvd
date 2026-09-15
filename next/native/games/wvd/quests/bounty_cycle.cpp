#include "bounty_cycle.hpp"
#include <stdexcept>

namespace wvd::games::quests {
void BountyCycle::require_phase(std::size_t unit, Phase expected) const {
    const auto offset = expected <= Phase::Reveal ? 0u : expected <= Phase::FirstReturn ? 1u :
        expected <= Phase::SecondReturn ? 2u : hands_ ? 3u : 2u;
    if (!active_ || phase_ != expected || unit != start_unit_ + offset)
        throw std::runtime_error("BOUNTY_CYCLE_PHASE_INVALID");
}
void BountyCycle::start(std::size_t unit, bool hands, bool rest_due, std::size_t reports, std::size_t reveals) {
    if (active_ || transfer_pending_ || unit != cycles_ * (hands ? 4 : 3) || (cycles_ && hands != hands_))
        throw std::runtime_error("BOUNTY_CYCLE_START_INVALID");
    active_ = true;
    hands_ = hands;
    rest_due_ = rest_due;
    start_unit_ = unit;
    initial_reports_ = reports;
    initial_reveals_ = reveals;
    phase_ = Phase::Leap;
    ++sequence_;
}
void BountyCycle::prepare_transfer(std::size_t unit, Phase phase) {
    require_phase(unit, phase);
    if (transfer_pending_ || (phase != Phase::Leap && phase != Phase::Travel))
        throw std::runtime_error("BOUNTY_TRANSFER_INVALID");
    transfer_pending_ = true;
}
void BountyCycle::transferred(std::size_t unit, Phase phase) {
    require_phase(unit, phase);
    if (!transfer_pending_) throw std::runtime_error("BOUNTY_TRANSFER_NOT_PREPARED");
    transfer_pending_ = false;
    phase_ = phase == Phase::Leap ? Phase::Travel : Phase::Reveal;
}
void BountyCycle::revealed(std::size_t unit, std::size_t reveals) {
    require_phase(unit, Phase::Reveal);
    if (reveals != initial_reveals_ + 1) throw std::runtime_error("BOUNTY_REVEAL_NOT_CONFIRMED");
    phase_ = Phase::FirstRoute;
}
void BountyCycle::skip_travel(std::size_t unit) {
    require_phase(unit, Phase::Travel);
    if (transfer_pending_) throw std::runtime_error("BOUNTY_TRANSFER_ALREADY_PENDING");
    phase_ = Phase::Reveal;
}
void BountyCycle::route_completed(std::size_t unit, std::size_t points) {
    require_phase(unit, phase_);
    if (points != 2 || (phase_ != Phase::FirstRoute && phase_ != Phase::SecondRoute))
        throw std::runtime_error("BOUNTY_ROUTE_NOT_COMPLETED");
    phase_ = phase_ == Phase::FirstRoute ? Phase::FirstReturn : Phase::SecondReturn;
}
void BountyCycle::returned(std::size_t unit) {
    require_phase(unit, phase_);
    if (phase_ != Phase::FirstReturn && phase_ != Phase::SecondReturn)
        throw std::runtime_error("BOUNTY_RETURN_PHASE_INVALID");
    phase_ = hands_ && phase_ == Phase::FirstReturn ? Phase::SecondRoute : Phase::Reports;
}
void BountyCycle::reported(std::size_t unit, std::size_t reports) {
    require_phase(unit, Phase::Reports);
    if (reports != initial_reports_ + (hands_ ? 2 : 1))
        throw std::runtime_error("BOUNTY_REPORT_COUNT_INVALID");
    phase_ = Phase::Rest;
}
void BountyCycle::complete(std::size_t unit, bool rested) {
    require_phase(unit, Phase::Rest);
    if (transfer_pending_ || (rest_due_ && !rested)) throw std::runtime_error("BOUNTY_REST_NOT_COMPLETED");
    ++cycles_;
    active_ = false;
    phase_ = Phase::Completed;
}
nlohmann::json BountyCycle::summary(std::size_t unit, std::size_t reports) const {
    const auto expected = initial_reports_ + (hands_ ? 2 : 1);
    const auto offset = phase_ <= Phase::Reveal ? 0u : phase_ <= Phase::FirstReturn ? 1u :
        phase_ <= Phase::SecondReturn ? 2u : hands_ ? 3u : 2u;
    return {{"active", active_}, {"phase", static_cast<int>(phase_)}, {"hands", hands_},
        {"unit_matches", active_ && unit == start_unit_ + offset},
        {"rest_due", rest_due_}, {"transfer_pending", transfer_pending_}, {"completed_cycles", cycles_},
        {"reports_remaining", reports < expected ? expected - reports : 0}};
}
}
