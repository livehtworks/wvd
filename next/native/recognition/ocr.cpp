#include "ocr.hpp"
#include "OcrLite.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <opencv2/imgproc.hpp>

namespace wvd::recognition {
namespace {
std::string utf8(const std::filesystem::path &path) {
    const auto value = path.u8string();
    return {reinterpret_cast<const char *>(value.data()), value.size()};
}
contracts::Box bounds(const std::vector<cv::Point> &points, cv::Size size) {
    if (points.size() != 4) throw std::runtime_error("OCR_BOX_INVALID");
    int left = size.width, top = size.height, right = 0, bottom = 0;
    for (const auto &point : points) {
        left = std::min(left, point.x);
        top = std::min(top, point.y);
        right = std::max(right, point.x);
        bottom = std::max(bottom, point.y);
    }
    left = std::clamp(left, 0, size.width);
    top = std::clamp(top, 0, size.height);
    right = std::clamp(right, 0, size.width);
    bottom = std::clamp(bottom, 0, size.height);
    if (right <= left || bottom <= top) throw std::runtime_error("OCR_BOX_INVALID");
    return {left, top, right - left, bottom - top};
}
} // namespace

OcrEngine::OcrEngine(const std::filesystem::path &model_root) {
    const auto det = model_root / "det.onnx";
    const auto rec = model_root / "rec.onnx";
    const auto keys = model_root / "keys.txt";
    if (!std::filesystem::is_regular_file(det) || !std::filesystem::is_regular_file(rec) ||
        !std::filesystem::is_regular_file(keys))
        throw std::runtime_error("OCR_MODEL_MISSING");
    model_ = std::make_shared<OcrLite>();
    model_->setNumThread(2);
    model_->initLogger(false, false, false);
    if (!model_->initModels(utf8(det), {}, utf8(rec), utf8(keys)))
        throw std::runtime_error("OCR_MODEL_INITIALIZATION_FAILED");
}

OcrEngine::~OcrEngine() = default;

std::vector<contracts::RecognitionMatch> OcrEngine::recognize(const cv::Mat &region) {
    if (region.empty() || region.type() != CV_8UC3)
        throw std::runtime_error("OCR_IMAGE_INVALID");
    std::lock_guard lock(run_mutex_);
    // These are RapidOCR's DB detector defaults; the caller applies the WVD text policy.
    auto result = model_->detect(region, 0, std::max(region.cols, region.rows),
                                  0.6f, 0.3f, 1.5f, false, false);
    std::vector<contracts::RecognitionMatch> matches;
    matches.reserve(result.textBlocks.size());
    for (const auto &block : result.textBlocks) {
        if (block.text.empty()) continue;
        const auto box = bounds(block.boxPoint, region.size());
        const double score = block.charScores.empty() ? 0.0 :
            std::accumulate(block.charScores.begin(), block.charScores.end(), 0.0) /
                block.charScores.size();
        if (!std::isfinite(score) || score < 0.0 || score > 1.0)
            throw std::runtime_error("OCR_SCORE_INVALID");
        matches.push_back({box, score, block.text});
    }
    return matches;
}

void OcrEngine::cancel() { model_->cancel(); }
} // namespace wvd::recognition
