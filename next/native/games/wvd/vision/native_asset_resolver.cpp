#include "native_asset_resolver.hpp"
#include "platform/windows/bundle_lease.hpp"
#include <algorithm>
#include <fstream>
#include <opencv2/imgcodecs.hpp>

namespace wvd::games::vision {
ImageSource resolve_image_source(const recognition::Bundle &baseline, const nlohmann::json &aliases,
                                 const std::string &name, const recognition::Bundle *mod) {
    if (!aliases.is_object())
        throw std::runtime_error("WVD_ALIASES_INVALID");
    const auto original = name.ends_with(".png") ? name : name + ".png";
    platform::BundleLease::checked_relative("image/" + original);
    auto exists = [](const recognition::Bundle &bundle, const std::string &path) {
        return std::any_of(bundle.files.begin(), bundle.files.end(),
                           [&](const auto &file) { return file.relative_path == "image/" + path; });
    };
    if (exists(baseline, original))
        return {&baseline, "image/" + original};
    const auto alternate = aliases.contains(original) ? aliases.at(original).get<std::string>() : original;
    platform::BundleLease::checked_relative("image/" + alternate);
    if (exists(baseline, alternate))
        return {&baseline, "image/" + alternate};
    // mod 原名沿用旧扩展目录的含义；显式别名只在该原名也缺失时作为后备。
    if (mod && exists(*mod, original))
        return {mod, "image/" + original};
    if (mod && exists(*mod, alternate))
        return {mod, "image/" + alternate};
    // 缺图仍交给统一 manifest 校验报告，不降级成 NoHit。
    return {&baseline, "image/" + alternate};
}
AssetResolver::AssetResolver(const recognition::Bundle &baseline, const nlohmann::json &aliases,
                             recognition::Cache &cache, const recognition::Bundle *mod)
    : baseline_(baseline), aliases_(aliases), cache_(cache), mod_(mod) {}
cv::Mat AssetResolver::load(const std::string &name) {
    const auto [owner, relative] = resolve_image_source(baseline_, aliases_, name, mod_);
    // 只读快照在服务建立时封存，运行期只访问清单成员。
    if (!owner->lease)
        throw std::runtime_error("BUNDLE_LEASE_REQUIRED");
    owner->lease->require_member(relative);
    auto key = "template:" + owner->revision + ":" + owner->lease->identity() + ":" + relative;
    if (auto found = cache_.assets.find(key); found != cache_.assets.end())
        return std::any_cast<cv::Mat>(found->second);
    const auto &bytes = owner->lease->bytes(relative);
    auto image = cv::imdecode(bytes, cv::IMREAD_COLOR);
    if (image.empty() || image.type() != CV_8UC3)
        throw std::runtime_error("WVD_TEMPLATE_DECODE_INVALID");
    if (cache_.assets.size() >= 2048)
        throw std::runtime_error("WVD_SESSION_ASSET_CAPACITY");
    cache_.assets.emplace(key, image);
    return image;
}
} // namespace wvd::games::vision
