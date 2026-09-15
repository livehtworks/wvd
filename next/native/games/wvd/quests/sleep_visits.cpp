#include "sleep_visits.hpp"
#include <algorithm>
#include <stdexcept>

namespace wvd::games::quests {
bool SleepVisits::batch_complete(std::size_t unit) const {
    return unit < units && completed_ >= std::min(total, (unit + 1) * batch_size) && !active_;
}
void SleepVisits::start(std::size_t unit) {
    if (unit >= units || active_ || completed_ < unit * batch_size || batch_complete(unit))
        throw std::runtime_error("SLEEP_VISIT_BOUNDARY_INVALID");
    active_ = true;
}
void SleepVisits::finish(bool paid) {
    if (!active_ || !paid || completed_ >= total)
        throw std::runtime_error("SLEEP_VISIT_NOT_CONFIRMED");
    ++completed_;
    active_ = false;
}
nlohmann::json SleepVisits::summary(std::size_t unit) const {
    return {{"target", total}, {"completed_visits", completed_}, {"visit_active", active_},
        {"batch_complete", batch_complete(unit)}, {"completed", completed_ == total}};
}
}
