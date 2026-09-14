#pragma once
#include "recognition.hpp"
#include <any>
#include <functional>
#include <json.hpp>
#include <map>
#include <span>

namespace wvd::maafw {
// SDK 范围不是调用者的业务 ROI。只有 Gateway 能签发本次调用的只读范围。
class CustomRecognitionScope {
  public:
    contracts::Box allowed_roi() const { return allowed_roi_; }
    std::uint64_t invocation_id() const { return invocation_id_; }

  private:
    friend class MaaGateway;
    CustomRecognitionScope(contracts::Box roi, std::uint64_t id)
        : allowed_roi_(roi), invocation_id_(id) {}
    const contracts::Box allowed_roi_;
    const std::uint64_t invocation_id_;
};
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
using BoundRecognition =
    std::function<nlohmann::json(const Bundle &, RecognitionPixels, const nlohmann::json &,
                                 const CustomRecognitionScope &, RecognitionCache &)>;
using RecognitionHandlers = std::map<std::string, BoundRecognition>;
} // namespace wvd::maafw
