#pragma once
#include <array>
#include <chrono>
#include <optional>
#include <string_view>
#include <json.hpp>

namespace wvd::games::fishing {
inline constexpr std::array<std::string_view, 8> species{"鲈鱼", "雅罗", "鲶鱼", "鳟鱼", "鳗鱼", "三文鱼", "杂鱼", "未收录"};
inline constexpr std::array<std::string_view, 3> sizes{"小", "普通", "大"};
// 三种尺寸各八类，24表示未识别尺寸。保存分类事实，不保留图像或识别器句柄。
class Progress {
  public:
    using TimePoint = std::chrono::steady_clock::time_point;
    void prepare(std::size_t index);
    void complete();
    void begin_wait(TimePoint now);
    void prepare_cast();
    void cast_completed(TimePoint now);
    void request_bait();
    void supplies_entered();
    void prepare_transfer();
    void transferred();
    void supplies_finished();
    void supplies_returned();
    void refilled();
    std::size_t refill_sequence(bool next) const { return refill_sequence_ + (next && refill_phase_ == 0 ? 1 : 0); }
    std::size_t transfer_sequence(bool next) const { return transfer_count_ + (next || transfer_pending_ ? 1 : 0); }
    void failed(TimePoint now);
    bool timed_out(TimePoint now) const;
    std::size_t cast_sequence(bool next) const { return cast_sequence_ + (next && !cast_started_ ? 1 : 0); }
    std::size_t cast_intent_sequence(bool next) const { return cast_sequence_ + (next && !casting_pending_ ? 1 : 0); }
    std::size_t sequence(bool next) const { return sequence_ + (next && !pending_ ? 1 : 0); }
    nlohmann::json summary(TimePoint now) const;
  private:
    std::optional<std::size_t> pending_;
    std::size_t sequence_{}, caught_{};
    std::size_t cast_sequence_{}, failed_{};
    std::optional<TimePoint> cast_started_;
    bool casting_pending_{};
    // 0钓鱼、1去物品页、2转交、3退出物品页、4返回钓点。不是另一个执行器。
    unsigned refill_phase_{};
    std::size_t refill_sequence_{}, refill_trips_{}, transfer_count_{};
    bool transfer_pending_{};
    std::array<std::size_t, 25> counts_{};
};
}
