#pragma once
#include "maafw/custom_recognition.hpp"
#include <opencv2/core.hpp>

namespace wvd::games::vision {
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
