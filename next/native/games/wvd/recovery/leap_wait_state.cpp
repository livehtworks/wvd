#include "leap_wait.hpp"
#include <algorithm>
#include <stdexcept>

namespace wvd::games::recovery {
using J = nlohmann::json;
using namespace std::chrono_literals;

std::chrono::milliseconds LeapWait::elapsed(TimePoint now) const {
    if (!started_) return 0ms;
    if (now < *started_ || (last_poll_ && now < *last_poll_))
        throw std::runtime_error("WVD_CLOCK_MOVED_BACKWARD");
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - *started_);
}
void LeapWait::begin(TimePoint now) {
    if (active_) throw std::runtime_error("LEAP_WAIT_ALREADY_ACTIVE");
    started_ = now; // 零时刻也是有效起点，不能用 time_since_epoch()==0 判空。
    last_poll_ = now;
    generation_ = 0;
    slices_ = 0;
    deadline_ = 0s;
    active_ = true;
}
bool LeapWait::poll(TimePoint now, std::uint64_t generation) {
    if (!active_ || !generation)
        throw std::runtime_error("LEAP_WAIT_NOT_ACTIVE");
    const auto spent = elapsed(now);
    if (generation != generation_) {
        if (generation < generation_ || slices_ >= max_slices ||
            (generation_ && spent < deadline_))
            throw std::runtime_error("LEAP_WAIT_SEGMENT_INVALID");
        generation_ = generation;
        ++slices_;
        // 截止点相对于原意图，段间清理和连接耗时不能让7300秒重新起算。
        deadline_ = std::min(duration,
            slice * static_cast<std::chrono::seconds::rep>(slices_));
    }
    last_poll_ = now;
    return spent >= deadline_;
}
void LeapWait::restarted(TimePoint now) {
    if (!active_ || elapsed(now) < duration)
        throw std::runtime_error("LEAP_WAIT_RESTART_TOO_EARLY");
    active_ = false;
    last_poll_ = now;
}
J LeapWait::summary(TimePoint now) const {
    const auto spent = elapsed(now);
    return {{"active", active_}, {"started", started_.has_value()},
            {"elapsed_ms", spent.count()}, {"ready", active_ && spent >= duration},
            {"slices", slices_}, {"generation", generation_},
            {"deadline_ms", std::chrono::duration_cast<std::chrono::milliseconds>(deadline_).count()},
            {"duration_ms", 7300000}, {"slice_ms", 1460000}};
}
} // namespace wvd::games::recovery
