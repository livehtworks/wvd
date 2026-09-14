#pragma once
#include "maafw/custom_recognition.hpp"
#include <opencv2/core.hpp>

namespace wvd::games::vision {
struct ImageSource {
    const maafw::Bundle *bundle;
    std::string relative_path;
};
// 发布和识别共用同一来源规则；这里只选择 manifest 成员，读取仍受 BundleLease 保护。
ImageSource resolve_image_source(const maafw::Bundle &baseline, const nlohmann::json &aliases,
                                 const std::string &name, const maafw::Bundle *mod = nullptr);
class AssetResolver {
  public:
    AssetResolver(const maafw::Bundle &baseline, const nlohmann::json &aliases,
                  maafw::RecognitionCache &cache, const maafw::Bundle *mod = nullptr);
    cv::Mat load(const std::string &name);

  private:
    const maafw::Bundle &baseline_;
    const nlohmann::json aliases_;
    maafw::RecognitionCache &cache_;
    const maafw::Bundle *mod_;
};
} // namespace wvd::games::vision
