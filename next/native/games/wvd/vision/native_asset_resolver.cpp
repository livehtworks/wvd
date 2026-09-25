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
    const auto key = owner->revision + ":" + owner->lease->identity() + ":" + relative;
    auto lease = cache_.decoded->load(key, [&] {
        const auto &bytes = owner->lease->bytes(relative);
        const auto stats = cache_.decoded->stats();
        auto diagnostic = cache_.diagnostics ? cache_.diagnostics->begin({
            cache_.source_id, std::hash<std::string>{}(relative), bytes.size(),
            0, 0, 0, 0, 0, 0, 3, -2, false, false, false,
            stats.retained_bytes, stats.in_use_bytes}) : platform::MemoryDiagnostics::Slot{};
        cv::Mat image;
        try { image = cv::imdecode(bytes, cv::IMREAD_COLOR); }
        catch (const std::bad_alloc &) { diagnostic.failure(-1); throw; }
        catch (const cv::Exception &error) {
            if (error.code == cv::Error::StsNoMem) diagnostic.failure(error.code);
            throw;
        }
        if (image.empty() || image.type() != CV_8UC3)
            throw std::runtime_error("WVD_TEMPLATE_DECODE_INVALID");
        return image;
    }, cache_.cancelled);
    auto pixels = lease.mat(); // 仅复制 Mat 头，像素租约由 resolver 持有。
    leases_.push_back(std::move(lease));
    return pixels;
}
std::string AssetResolver::canonical_key(const std::string &name) const {
    const auto [owner, relative] = resolve_image_source(baseline_, aliases_, name, mod_);
    if (!owner->lease) throw std::runtime_error("BUNDLE_LEASE_REQUIRED");
    return owner->revision + ":" + owner->lease->identity() + ":" + relative;
}
} // namespace wvd::games::vision
