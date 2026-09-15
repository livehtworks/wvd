#include "manual_separation.hpp"
#include <stdexcept>

namespace wvd::games::quests {
void ManualSeparation::started_in_city(std::size_t unit) {
    if (pending_ || phase_ != Phase::FirstRoute || unit != 0)
        throw std::runtime_error("MANUAL_SEPARATION_CITY_START_INVALID");
    phase_ = Phase::FirstBack;
}
void ManualSeparation::route_completed(std::size_t points, std::size_t unit) {
    if (pending_ || points != 2 ||
        !((phase_ == Phase::FirstRoute && unit == 0) || (phase_ == Phase::SecondRoute && unit == 1)))
        throw std::runtime_error("MANUAL_SEPARATION_ROUTE_INCOMPLETE");
    phase_ = unit == 0 ? Phase::FirstBack : Phase::Completed;
}
void ManualSeparation::prepare(Phase expected) {
    if (pending_ || phase_ != expected ||
        (expected != Phase::FirstBack && expected != Phase::SecondBack && expected != Phase::Leap))
        throw std::runtime_error("MANUAL_SEPARATION_TRANSFER_INVALID");
    pending_ = expected;
}
void ManualSeparation::transferred(Phase expected) {
    if (pending_ != expected || phase_ != expected)
        throw std::runtime_error("MANUAL_SEPARATION_TRANSFER_NOT_PREPARED");
    pending_.reset();
    phase_ = expected == Phase::FirstBack ? Phase::SecondBack :
             expected == Phase::SecondBack ? Phase::Rest : Phase::SecondRoute;
}
void ManualSeparation::rested(bool confirmed_payment) {
    if (phase_ != Phase::Rest || pending_ || !confirmed_payment)
        throw std::runtime_error("MANUAL_SEPARATION_REST_NOT_CONFIRMED");
    phase_ = Phase::Leap;
}
nlohmann::json ManualSeparation::summary() const {
    return {{"phase", static_cast<int>(phase_)}, {"transfer_pending", pending_.has_value()},
        {"completed", phase_ == Phase::Completed}};
}
}
