#pragma once

#include "contracts/recognition.hpp"
#include "platform/windows/memory_diagnostics.hpp"
#include <filesystem>
#include <memory>
#include <mutex>
#include <opencv2/core.hpp>
#include <vector>

class OcrLite;

namespace wvd::recognition {
class OcrEngine final {
  public:
    explicit OcrEngine(const std::filesystem::path &model_root,
        std::shared_ptr<platform::MemoryDiagnostics> diagnostics = {});
    ~OcrEngine();
    OcrEngine(const OcrEngine &) = delete;
    OcrEngine &operator=(const OcrEngine &) = delete;
    std::vector<contracts::RecognitionMatch> recognize(const cv::Mat &region);
    void cancel();

  private:
    std::mutex run_mutex_;
    std::shared_ptr<OcrLite> model_;
    platform::MemoryOwnerLifetime lifetime_{platform::MemoryOwnerKind::OcrEngine};
    std::shared_ptr<platform::MemoryDiagnostics> diagnostics_;
};
} // namespace wvd::recognition
