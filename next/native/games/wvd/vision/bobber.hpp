#pragma once
#include <json.hpp>
#include <opencv2/core.hpp>
#include "recognition/custom.hpp"
namespace wvd::games::vision {
nlohmann::json detect_bobber(const cv::Mat &image, const cv::Mat &template_image,
                             recognition::Cache &cache, std::uint64_t asset_id);
}
