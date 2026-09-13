#pragma once
#include "recognition.hpp"
#include <any>
#include <functional>
#include <json.hpp>
#include <map>
#include <span>

namespace wvd::maafw {
// 回调只借用本次像素；缓存只属于此 Gateway，不可存放 image 指针。
struct RecognitionPixels {
    std::span<const std::uint8_t> bgr;
    contracts::Size size;
};
struct RecognitionCache {
    std::map<std::string, std::any> assets;
    std::string frame_key;
    std::map<std::string, nlohmann::json> results;
};
using BoundRecognition = std::function<nlohmann::json(const Bundle &, RecognitionPixels,
                                                      const nlohmann::json &, RecognitionCache &)>;
using RecognitionHandlers = std::map<std::string, BoundRecognition>;
} // namespace wvd::maafw
