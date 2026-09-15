#include "unknown_window.hpp"
#include <algorithm>
#include <numeric>
#include <limits>
#include <stdexcept>
#include <opencv2/imgproc.hpp>

namespace wvd::games::vision {
UnknownSample UnknownWindow::observe(const cv::Mat &bgr, std::chrono::steady_clock::time_point now) {
    if (bgr.empty() || bgr.type() != CV_8UC3 || bgr.cols != 900 || bgr.rows != 1600)
        throw std::runtime_error("WVD_UNKNOWN_FRAME_INVALID");
    if (!previous_.empty()) {
        if (now < sampled_at_)
            throw std::runtime_error("WVD_UNKNOWN_CLOCK_REVERSED");
        // 高速截图不能在几十毫秒内凑齐十帧而把动画间隙当冻结。
        if (now - sampled_at_ < std::chrono::seconds{1}) {
            auto result = last_;
            result.sampled = false;
            result.evaluated = false;
            return result;
        }
    }
    cv::Mat gray;
    cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
    if (!previous_.empty()) {
        cv::Mat difference;
        cv::absdiff(gray, previous_, difference);
        differences_[next_difference_] = cv::mean(difference)[0] / 255;
        next_difference_ = (next_difference_ + 1) % differences_.size();
    }
    previous_ = std::move(gray);
    sampled_at_ = now;
    window_size_ = std::min(window_size_ + 1, 10u);
    batch_count_ = (batch_count_ + 1) % 10;
    const bool evaluated = window_size_ == 10 && batch_count_ == 0;
    const auto total = std::accumulate(differences_.begin(), differences_.end(), 0.0);
    if (samples_ < std::numeric_limits<std::uint64_t>::max()) ++samples_;
    last_ = {true, evaluated, evaluated && total <= .15, window_size_, total, samples_};
    return last_;
}
void UnknownWindow::clear() {
    previous_.release();
    differences_.fill(0);
    window_size_ = next_difference_ = batch_count_ = 0;
    samples_ = 0;
    sampled_at_ = {};
    last_ = {};
}
}
