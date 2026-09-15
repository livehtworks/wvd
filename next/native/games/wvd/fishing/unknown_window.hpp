#pragma once
#include <chrono>
#include <optional>
#include <stdexcept>

namespace wvd::games::fishing {
// 与本内卡死窗口分开：钓鱼未知页按90秒计时，不按像素变化推断已钓到鱼。
class UnknownWindow {
  public:
    using TimePoint = std::chrono::steady_clock::time_point;
    bool observe(bool fishing_page, bool suspended, TimePoint now) {
        if (last_ && now < *last_) throw std::runtime_error("WVD_CLOCK_MOVED_BACKWARD");
        if (!last_ || fishing_page) last_ = now;
        return !fishing_page && !suspended && now - *last_ > std::chrono::seconds{90};
    }
  private:
    std::optional<TimePoint> last_;
};
}
