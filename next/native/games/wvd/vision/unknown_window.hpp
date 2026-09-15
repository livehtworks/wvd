#pragma once
#include <array>
#include <chrono>
#include <cstdint>
#include <opencv2/core.hpp>

namespace wvd::games::vision {
struct UnknownSample {
    bool sampled{}, evaluated{}, frozen{};
    unsigned window_size{};
    double total_difference{};
    std::uint64_t samples{};
};
// 单个识别会话的未知页窗口，不拥有设备或运行对象，也不执行恢复。
// 只保存上一张灰度图和九个相邻差值，等价于旧版十张图的相邻差之和。
class UnknownWindow {
  public:
    UnknownSample observe(const cv::Mat &bgr, std::chrono::steady_clock::time_point now);
    void clear();

  private:
    cv::Mat previous_;
    std::array<double, 9> differences_{};
    unsigned window_size_{}, next_difference_{}, batch_count_{};
    std::uint64_t samples_{};
    std::chrono::steady_clock::time_point sampled_at_{};
    UnknownSample last_;
};
}
