#include "asset_resolver.hpp"
#include "maafw/preflight.hpp"
#include "platform/windows/bundle_lease.hpp"
#include <fstream>
#include <opencv2/imgcodecs.hpp>

namespace wvd::games::vision {
AssetResolver::AssetResolver(const maafw::Bundle &baseline, const nlohmann::json &aliases,
                             maafw::RecognitionCache &cache, const maafw::Bundle *mod)
    : baseline_(baseline), aliases_(aliases), cache_(cache), mod_(mod) {}
cv::Mat AssetResolver::load(const std::string &name) {
    auto path = name.ends_with(".png") ? name : name + ".png";
    if (aliases_.contains(path))
        path = aliases_.at(path).get<std::string>();
    const auto relative = "image/" + path;
    const maafw::Bundle *owner = &baseline_;
    auto exists = [&](const maafw::Bundle &bundle) {
        return std::any_of(bundle.files.begin(), bundle.files.end(),
                           [&](const auto &file) { return file.relative_path == relative; });
    };
    if (!exists(*owner) && mod_ && exists(*mod_))
        owner = mod_;
    // 即使有缓存也复核当前快照。缺图、坏图或修改后文件不能成为正常 NoHit。
    maafw::verify_file(*owner, relative);
    if (!owner->lease)
        throw std::runtime_error("BUNDLE_LEASE_REQUIRED");
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
