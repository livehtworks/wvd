#pragma once
#include <cstddef>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <json.hpp>

namespace wvd::games::quests {
// 回执归单个 Run 所有；Session 只持有冻结图，不保存可变对话策略。
// 每条路线对应旧版一次完整 StateDungeon 调用。
class CaveOfSeparation {
  public:
    enum class Segment { Preparation, B1, B2, B3, Back, ReturnCity };
    enum class Phase { Leap, Fortress, RoyalCity, Request, Rest, Enter, B1, B2, B3, Back, ReturnGuild, ReturnInn, Completed };
    enum class Pending { None, Leap, Request, Guild };
    static constexpr std::size_t segments_per_cycle = 6;

    static std::size_t segment_index(Segment segment) {
        switch (segment) {
        case Segment::Preparation: return 0;
        case Segment::B1: return 1;
        case Segment::B2: return 2;
        case Segment::B3: return 3;
        case Segment::Back: return 4;
        case Segment::ReturnCity: return 5;
        }
        throw std::runtime_error("COS_SEGMENT_INVALID");
    }
    std::size_t sequence(bool starting = false) const { return started_ + (starting && !active_ ? 1 : 0); }
    void start(std::size_t unit) {
        if (active_ || pending_ != Pending::None || unit > std::numeric_limits<std::size_t>::max() - 5)
            throw std::runtime_error("COS_START_INVALID");
        base_ = unit; active_ = true; phase_ = Phase::Leap; request_seen_ = false;
        last_completed_unit_.reset(); ++started_;
    }
    void prepare_leap(std::size_t unit) { prepare(Phase::Leap, Pending::Leap, unit); }
    void request_observed(std::size_t unit) {
        require(Phase::Request, unit);
        if (request_seen_ || pending_ != Pending::None) throw std::runtime_error("COS_REQUEST_ALREADY_OBSERVED");
        request_seen_ = true;
    }
    void prepare_request(std::size_t unit) {
        if (!request_seen_) throw std::runtime_error("COS_REQUEST_NOT_OBSERVED");
        prepare(Phase::Request, Pending::Request, unit);
    }
    void prepare_guild(std::size_t unit) { prepare(Phase::ReturnGuild, Pending::Guild, unit); }

    // stop_image 只来自已注册事件的新帧确认，不能取自任意调用参数，
    // 也不能由子任务的 Completed 状态推导。
    void advance(Phase phase, std::size_t unit, std::size_t points = 0,
                 bool rested = false, std::string_view stop_image = {}) {
        require(phase, unit);
        const auto expected_pending = phase == Phase::Leap ? Pending::Leap :
            phase == Phase::ReturnGuild ? Pending::Guild : Pending::None;
        if (phase != Phase::Request && pending_ != expected_pending)
            throw std::runtime_error("COS_SIDE_EFFECT_NOT_CONFIRMED");
        if (phase == Phase::Request && (!request_seen_ ||
            (pending_ != Pending::None && pending_ != Pending::Request)))
            throw std::runtime_error("COS_REQUEST_NOT_OBSERVED");
        if (phase == Phase::Rest && !rested) throw std::runtime_error("COS_REST_REQUIRED");
        if ((phase == Phase::B1 && points != 4) || (phase == Phase::Back && points != 7))
            throw std::runtime_error("COS_ROUTE_INCOMPLETE");
        if (phase == Phase::B2) {
            if (stop_image != "COS/EnaTheAdventurer") throw std::runtime_error("COS_ENA_NOT_CONFIRMED");
        } else if (phase == Phase::B3) {
            if (stop_image != "COS/requestwasfor") throw std::runtime_error("COS_REQUEST_STOP_NOT_CONFIRMED");
        } else if (!stop_image.empty()) throw std::runtime_error("COS_UNEXPECTED_STOP");
        pending_ = Pending::None;
        if (phase == Phase::Enter || phase == Phase::B1 || phase == Phase::B2 ||
            phase == Phase::B3 || phase == Phase::Back || phase == Phase::ReturnInn)
            last_completed_unit_ = unit;
        phase_ = static_cast<Phase>(static_cast<int>(phase) + 1);
        if (phase_ == Phase::Completed) { active_ = false; ++completed_; }
    }
    bool segment_complete(std::size_t unit) const {
        return last_completed_unit_ && *last_completed_unit_ == unit && pending_ == Pending::None;
    }
    nlohmann::json summary(std::size_t unit) const {
        return {{"phase", static_cast<int>(phase_)}, {"active", active_},
            {"pending", pending_ != Pending::None}, {"pending_kind", static_cast<int>(pending_)},
            {"request_seen", request_seen_}, {"unit_matches", active_ && unit == expected_unit()},
            {"segment_complete", segment_complete(unit)}, {"started_cycles", started_}, {"completed_cycles", completed_}};
    }
  private:
    std::size_t expected_unit() const {
        if (phase_ <= Phase::Enter) return base_;
        if (phase_ <= Phase::Back) return base_ + 1 + static_cast<std::size_t>(phase_) - static_cast<std::size_t>(Phase::B1);
        return base_ + 5;
    }
    void require(Phase phase, std::size_t unit) const {
        if (!active_ || phase_ != phase || unit != expected_unit()) throw std::runtime_error("COS_PHASE_INVALID");
    }
    void prepare(Phase phase, Pending pending, std::size_t unit) {
        require(phase, unit);
        if (pending_ != Pending::None) throw std::runtime_error("COS_ALREADY_PENDING");
        pending_ = pending;
    }
    Phase phase_{Phase::Completed};
    Pending pending_{Pending::None};
    bool active_{}, request_seen_{};
    std::size_t base_{}, started_{}, completed_{};
    std::optional<std::size_t> last_completed_unit_;
};
}
