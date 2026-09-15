#pragma once
#include <cstddef>
#include <json.hpp>

namespace wvd::games::quests {
// 源 lovesleep 的9999次是业务目标；40次只是当前有限Session的分批上限。
// 不保存图像或执行器，付款与页面退出的证据由现有住宿链提供。
class SleepVisits {
  public:
    static constexpr std::size_t total = 9999;
    static constexpr std::size_t batch_size = 40;
    static constexpr std::size_t units = (total + batch_size - 1) / batch_size;
    bool active() const { return active_; }
    std::size_t completed() const { return completed_; }
    bool batch_complete(std::size_t unit) const;
    void start(std::size_t unit);
    void finish(bool paid);
    nlohmann::json summary(std::size_t unit) const;
  private:
    std::size_t completed_{};
    bool active_{};
};
}
