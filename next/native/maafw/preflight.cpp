#include "preflight.hpp"
#include "platform/windows/file_digest.hpp"
#include "platform/windows/bundle_lease.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>

namespace wvd::maafw {
namespace {
void require(bool condition, const char *code) {
    if (!condition)
        throw std::runtime_error(code);
}
bool within(contracts::Box roi, contracts::Size size) {
    return roi.x >= 0 && roi.y >= 0 && roi.width > 0 && roi.height > 0 && roi.width <= size.width &&
           roi.height <= size.height && roi.x <= size.width - roi.width &&
           roi.y <= size.height - roi.height;
}
std::string literal_regex(const std::string &text) {
    std::string result;
    for (char c : text) {
        if (std::string_view(R"(\.^$|()[]{}*+?)").find(c) != std::string_view::npos)
            result += '\\';
        result += c;
    }
    return result;
}
} // namespace

void verify_file(const Bundle &bundle, const std::string &relative) {
    if (bundle.lease) {
        require(bundle.lease->root() == bundle.root && bundle.lease->revision() == bundle.revision,
                "BUNDLE_LEASE_MISMATCH");
        bundle.lease->require_member(relative);
        auto item = std::find_if(bundle.files.begin(), bundle.files.end(),
                                 [&](const auto &file) { return file.relative_path == relative; });
        require(item != bundle.files.end() && item->sha256 == bundle.lease->hash(relative),
                "BUNDLE_LEASE_MISMATCH");
        return;
    }
    const auto path = platform::BundleLease::checked_relative(relative);
    const auto root = std::filesystem::canonical(bundle.root);
    const auto resolved = std::filesystem::weakly_canonical(root / path);
    auto a = root.begin(), b = resolved.begin();
    for (; a != root.end() && b != resolved.end() && *a == *b; ++a, ++b) {
    }
    require(a == root.end() && b != resolved.end(), "RESOURCE_PATH_ESCAPE");
    auto item = std::find_if(bundle.files.begin(), bundle.files.end(),
                             [&](const auto &file) { return file.relative_path == relative; });
    require(item != bundle.files.end(), "RESOURCE_NOT_IN_MANIFEST");
    require(platform::file_sha256(resolved) == item->sha256, "RESOURCE_HASH_MISMATCH");
}

void verify_bundle(const Bundle &bundle) {
    if (bundle.lease) {
        require(bundle.lease->root() == bundle.root &&
                    bundle.lease->revision() == bundle.revision &&
                    bundle.files.size() == bundle.lease->file_count(),
                "BUNDLE_LEASE_MISMATCH");
        bundle.lease->verify_members();
        return;
    }
    std::size_t count = 0;
    for (const auto &entry : std::filesystem::recursive_directory_iterator(bundle.root)) {
        require(!entry.is_symlink(), "BUNDLE_LINK_REJECTED");
        if (!entry.is_regular_file())
            continue;
        ++count;
        auto relative = entry.path().lexically_relative(bundle.root).generic_u8string();
        verify_file(bundle, std::string(relative.begin(), relative.end()));
    }
    require(count == bundle.files.size(), "BUNDLE_MANIFEST_INCOMPLETE");
}

Image validate_frame(const contracts::FrameEnvelope &frame, const contracts::FrameIdentity &current,
                     const std::string &revision) {
    const auto &id = frame.identity;
    require(!id.device_id.empty() && !id.game_id.empty() && !id.viewport_id.empty() &&
                !revision.empty() && id.generation > 0 && id.frame_id > 0,
            "FRAME_IDENTITY_INVALID");
    require(id.device_id == current.device_id && id.game_id == current.game_id &&
                id.pack_revision == revision && id.pack_revision == current.pack_revision &&
                id.generation == current.generation,
            "FRAME_CONTEXT_MISMATCH");
    require(id.frame_id == current.frame_id && id.action_epoch == current.action_epoch &&
                id.connection_generation == current.connection_generation,
            "FRAME_STALE");
    require(id.viewport_id == current.viewport_id && id.raw_size == current.raw_size &&
                id.recognition_size == current.recognition_size,
            "FRAME_VIEWPORT_MISMATCH");
    require(id.color_format == "BGR8" && current.color_format == "BGR8", "FRAME_COLOR_INVALID");
    require(id.captured_at == current.captured_at &&
                id.captured_at.time_since_epoch().count() > 0 &&
                id.captured_at <= std::chrono::steady_clock::now(),
            "FRAME_TIME_INVALID");
    require(id.raw_size.width > 0 && id.raw_size.height > 0 && id.recognition_size.width > 0 &&
                id.recognition_size.height > 0 && id.raw_size.width <= 16384 &&
                id.raw_size.height <= 16384 && id.recognition_size.width <= 16384 &&
                id.recognition_size.height <= 16384,
            "FRAME_SIZE_INVALID");
    require(static_cast<std::int64_t>(id.raw_size.width) * id.recognition_size.height ==
                static_cast<std::int64_t>(id.raw_size.height) * id.recognition_size.width,
            "FRAME_ASPECT_MISMATCH");
    require(!frame.encoded_image.empty() && frame.encoded_image.size() <= 64 * 1024 * 1024,
            "FRAME_BYTES_INVALID");
    // SDK 的 SetEncoded(true) 不保证解码出像素，必须继续核对尺寸、通道和指针。
    auto image = image_buffer();
    auto owned = frame.encoded_image;
    require(MaaImageBufferSetEncoded(image.get(), owned.data(), owned.size()) &&
                MaaImageBufferGetRawData(image.get()) && MaaImageBufferChannels(image.get()) == 3 &&
                MaaImageBufferType(image.get()) == 16 &&
                MaaImageBufferWidth(image.get()) == id.recognition_size.width &&
                MaaImageBufferHeight(image.get()) == id.recognition_size.height,
            "FRAME_DECODE_INVALID");
    return image;
}

nlohmann::json validate_parameters(const Bundle &bundle, const RecognitionRequest &request,
                                   contracts::Size size) {
    require(!request.recognizer_id.empty() && !request.parameter_revision.empty(),
            "RECO_IDENTITY_INVALID");
    require(within(request.roi, size), "ROI_INVALID");
    const auto roi = request.roi;
    nlohmann::json parameters{{"roi", {roi.x, roi.y, roi.width, roi.height}}};
    if (const auto *templ = std::get_if<TemplateParameters>(&request.parameters)) {
        require(std::isfinite(templ->threshold) && templ->threshold >= 0 && templ->threshold <= 1,
                "THRESHOLD_INVALID");
        const auto relative = platform::BundleLease::checked_relative(templ->image);
        verify_file(bundle, "image/" + templ->image);
        require(std::filesystem::file_size(bundle.root / "image" / relative) <= 64 * 1024 * 1024,
                "TEMPLATE_BYTES_INVALID");
        require(bool(bundle.lease), "BUNDLE_LEASE_REQUIRED");
        auto bytes = bundle.lease->bytes("image/" + templ->image);
        require(!bytes.empty() && bytes.size() <= 64 * 1024 * 1024, "TEMPLATE_BYTES_INVALID");
        auto image = image_buffer();
        require(MaaImageBufferSetEncoded(image.get(), bytes.data(), bytes.size()) &&
                    MaaImageBufferGetRawData(image.get()) &&
                    MaaImageBufferChannels(image.get()) == 3 &&
                    MaaImageBufferType(image.get()) == 16,
                "TEMPLATE_DECODE_INVALID");
        const int w = MaaImageBufferWidth(image.get()), h = MaaImageBufferHeight(image.get());
        require(w > 0 && h > 0 && w <= roi.width && h <= roi.height, "TEMPLATE_EXCEEDS_ROI");
        parameters["template"] = templ->image;
        parameters["threshold"] = templ->threshold;
        parameters["method"] = 5;
        parameters["green_mask"] = false;
    } else if (const auto *custom =
                   std::get_if<RecognitionRequest::CustomParameters>(&request.parameters)) {
        require(!custom->binding.empty() && custom->parameters.is_object() &&
                    custom->parameters.dump().size() <= 32768,
                "CUSTOM_PARAMETERS_INVALID");
        require(!custom->parameters.contains("_wvd_verified_invocation"),
                "INTEGRITY_INVOCATION_RESERVED");
        parameters["custom_recognition"] = custom->binding;
        parameters["custom_recognition_param"] = custom->parameters;
        parameters["custom_recognition_param"]["parameter_revision"] = request.parameter_revision;
    } else {
        const auto &ocr = std::get<OcrParameters>(request.parameters);
        require(!ocr.expected_text.empty() && ocr.expected_text.size() <= 32,
                "OCR_EXPECTED_INVALID");
        parameters["expected"] = nlohmann::json::array();
        for (const auto &text : ocr.expected_text) {
            require(!text.empty() && text.size() <= 1024 && text.find('\0') == std::string::npos,
                    "OCR_EXPECTED_INVALID");
            parameters["expected"].push_back(literal_regex(text));
        }
        // 不把“模型文件存在”当作模型有效。当前仅开放已锁定的英文模型，逐文件复核 hash。
        const std::pair<const char *, const char *> models[] = {
            {"model/ocr/det.onnx",
             "8fe4bf6abfb20402357827f2efc964c8b28cf980e29fe09a99b742ae29725fa9"},
            {"model/ocr/rec.onnx",
             "da12c6e863761d774b07d3bd40fbaaa55516f90570e0b1dd9dc112e457301cc9"},
            {"model/ocr/keys.txt",
             "5662df9d2d03f0e8ca0d3b0649d6acbab904b6a14b3d3521463c71c37c668ce3"}};
        for (const auto &[path, hash] : models) {
            verify_file(bundle, path);
            require(bool(bundle.lease) && bundle.lease->hash(path) == hash, "OCR_MODEL_NOT_LOCKED");
        }
    }
    return parameters;
}
} // namespace wvd::maafw
