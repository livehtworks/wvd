#include "progress.hpp"
#include <stdexcept>

namespace wvd::games::fishing {
void Progress::prepare(std::size_t index) {
    if (index >= counts_.size()) throw std::runtime_error("FISHING_REWARD_INDEX_INVALID");
    if (casting_pending_) throw std::runtime_error("FISHING_CAST_UNCONFIRMED");
    if (pending_) {
        if (*pending_ != index) throw std::runtime_error("FISHING_REWARD_CHANGED_BEFORE_DISMISSAL");
        return;
    }
    pending_ = index;
    ++sequence_;
}
void Progress::complete() {
    if (!pending_) throw std::runtime_error("FISHING_REWARD_NOT_PREPARED");
    ++counts_[*pending_];
    ++caught_;
    pending_.reset();
    cast_started_.reset();
}
void Progress::begin_wait(TimePoint now) {
    if (pending_ || casting_pending_) throw std::runtime_error("FISHING_SIDE_EFFECT_PENDING");
    if (cast_started_) return;
    cast_started_ = now;
    ++cast_sequence_;
}
void Progress::prepare_cast() {
    if (pending_ || casting_pending_) throw std::runtime_error("FISHING_SIDE_EFFECT_PENDING");
    casting_pending_ = true;
    ++cast_sequence_;
}
void Progress::cast_completed(TimePoint now) {
    if (!casting_pending_ || pending_) throw std::runtime_error("FISHING_CAST_NOT_PREPARED");
    casting_pending_ = false;
    cast_started_ = now;
}
bool Progress::timed_out(TimePoint now) const {
    if (cast_started_ && now < *cast_started_) throw std::runtime_error("WVD_CLOCK_MOVED_BACKWARD");
    return cast_started_ && now - *cast_started_ > std::chrono::seconds{300};
}
void Progress::failed(TimePoint now) {
    if (pending_ || casting_pending_ || !timed_out(now)) throw std::runtime_error("FISHING_TIMEOUT_NOT_CONFIRMED");
    ++failed_;
    cast_started_.reset();
}
nlohmann::json Progress::summary(TimePoint now) const {
    nlohmann::json classified = nlohmann::json::object();
    for (std::size_t size = 0; size < sizes.size(); ++size)
        for (std::size_t fish = 0; fish < species.size(); ++fish)
            classified[std::string(sizes[size])][std::string(species[fish])] = counts_[size * species.size() + fish];
    return {{"caught", caught_}, {"reward_pending", pending_.has_value()}, {"reward_sequence", sequence_},
        {"cast_sequence", cast_sequence_}, {"waiting", cast_started_.has_value()}, {"timed_out", timed_out(now)}, {"failed", failed_},
        {"casting_pending", casting_pending_},
        {"pending_index", pending_ ? nlohmann::json(*pending_) : nlohmann::json(nullptr)},
        {"unclassified_size", counts_.back()}, {"fishinfo", classified}};
}
}
