#pragma once

#include "contracts/recognition.hpp"
#include "platform/windows/memory_diagnostics.hpp"
#include <filesystem>
#include <memory>
#include <mutex>
#include <opencv2/core.hpp>
#include <vector>
#include <array>
#include <functional>

namespace wvd::platform { class BundleLease; }

class OcrLite;

namespace wvd::recognition {
class OcrEngine final {
  public:
    explicit OcrEngine(const std::filesystem::path &model_root,
        std::shared_ptr<platform::MemoryDiagnostics> diagnostics = {},
        std::shared_ptr<const platform::BundleLease> files = {});
    ~OcrEngine();
    OcrEngine(const OcrEngine &) = delete;
    OcrEngine &operator=(const OcrEngine &) = delete;
    std::vector<contracts::RecognitionMatch> recognize(const cv::Mat &region);
    void cancel();

  private:
    friend class RunOcrModels;
    void prepare_reuse();
    std::mutex run_mutex_;
    std::shared_ptr<OcrLite> model_;
    platform::MemoryOwnerLifetime lifetime_{platform::MemoryOwnerKind::OcrEngine};
    std::shared_ptr<platform::MemoryDiagnostics> diagnostics_;
    std::shared_ptr<const platform::BundleLease> files_;
};
// A single run owns at most two locked language models. Services keep private
// frame/result/cancellation state; reuse requires the previous service lease
// to have gone, never a global cache or a reset of an active inference.
class RunOcrModels final {
  public:
    std::shared_ptr<OcrEngine> acquire(std::size_t language, const std::string &identity,
        const std::function<std::shared_ptr<OcrEngine>()> &create);
  private:
    struct Slot { std::string identity; std::shared_ptr<OcrEngine> model; };
    std::mutex mutex_;
    std::array<Slot, 2> slots_;
};
} // namespace wvd::recognition
