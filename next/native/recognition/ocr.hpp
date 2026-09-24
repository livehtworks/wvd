#pragma once

#include "contracts/recognition.hpp"
#include <filesystem>
#include <memory>
#include <mutex>
#include <opencv2/core.hpp>
#include <vector>

class OcrLite;

namespace wvd::recognition {
class OcrEngine final {
  public:
    explicit OcrEngine(const std::filesystem::path &model_root);
    ~OcrEngine();
    OcrEngine(const OcrEngine &) = delete;
    OcrEngine &operator=(const OcrEngine &) = delete;
    std::vector<contracts::RecognitionMatch> recognize(const cv::Mat &region);
    void cancel();

  private:
    std::mutex run_mutex_;
    std::shared_ptr<OcrLite> model_;
};
} // namespace wvd::recognition
