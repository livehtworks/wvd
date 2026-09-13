#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <opencv2/core.hpp>
#include <stdexcept>

namespace wvd::games::vision {
// 保留 NumPy clip(...).astype(uint8) 的截断语义。输入只读，通道参数按旧接口 RGB 排列。
inline cv::Mat transform_rgb(const cv::Mat &image, std::array<double, 3> rgb, bool subtract) {
    if (image.empty() || image.type() != CV_8UC3 ||
        !std::all_of(rgb.begin(), rgb.end(), [](double v) { return std::isfinite(v); }))
        throw std::runtime_error("WVD_COLOR_PARAMETERS_INVALID");
    cv::Mat result(image.size(), image.type());
    for (int y = 0; y < image.rows; ++y)
        for (int x = 0; x < image.cols; ++x)
            for (int c = 0; c < 3; ++c) {
                const double value = image.at<cv::Vec3b>(y, x)[c];
                result.at<cv::Vec3b>(y, x)[c] = static_cast<std::uint8_t>(
                    std::clamp(subtract ? value - rgb[2 - c] : value * rgb[2 - c], 0.0, 255.0));
            }
    return result;
}
} // namespace wvd::games::vision
