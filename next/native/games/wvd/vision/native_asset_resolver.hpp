#pragma once
#include "recognition/custom.hpp"
#include <opencv2/core.hpp>

namespace wvd::games::vision {
struct ImageSource {
    const recognition::Bundle *bundle;
    std::string relative_path;
};
// 发布和识别共用同一来源规则；这里只选择 manifest 成员，读取仍受 BundleLease 保护。
ImageSource resolve_image_source(const recognition::Bundle &baseline, const nlohmann::json &aliases,
                                 const std::string &name, const recognition::Bundle *mod = nullptr);
class AssetResolver {
  public:
    AssetResolver(const recognition::Bundle &baseline, const nlohmann::json &aliases,
                  recognition::Cache &cache, const recognition::Bundle *mod = nullptr);
    cv::Mat load(const std::string &name);
    std::string canonical_key(const std::string &name) const;

  private:
    const recognition::Bundle &baseline_;
    const nlohmann::json aliases_;
    recognition::Cache &cache_;
    const recognition::Bundle *mod_;
    std::vector<recognition::DecodedAssetCache::Lease> leases_;
};
} // namespace wvd::games::vision
