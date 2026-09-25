#pragma once

#include "custom.hpp"
#include "frame.hpp"
#include "ocr.hpp"
#include <atomic>
#include <memory>
#include <mutex>

namespace wvd::recognition {
class Service final {
  public:
    Service(Bundle bundle, Handlers handlers,
            std::shared_ptr<MatchBudget> budget = {},
            std::filesystem::path diagnostics_path = {},
            std::uint64_t run_id = 0, std::uint64_t generation = 0);
    contracts::Observation evaluate(const contracts::FrameEnvelope &frame,
                                    const contracts::FrameIdentity &current,
                                    const Request &request,
                                    const contracts::BusinessRunState *business = nullptr);
    void cancel() noexcept;
    const Bundle &bundle() const { return bundle_; }
    ResourceStats resource_stats() const;
    bool diagnostic_write_failed() const noexcept {
        return cache_.diagnostics && cache_.diagnostics->write_failed();
    }

  private:
    contracts::Observation evaluate_locked(const FramePixels &pixels, const Request &request,
                                           const contracts::BusinessRunState *business,
                                           const contracts::FrameIdentity &basis);
    contracts::Observation recognize_ocr(const FramePixels &pixels, const Request &request,
                                         const contracts::FrameIdentity &basis);
    Bundle bundle_;
    Handlers handlers_;
    Cache cache_;
    std::unique_ptr<FramePixels> frame_pixels_;
    std::string frame_pixels_key_;
    std::mutex mutex_;
    std::atomic<std::shared_ptr<OcrEngine>> ocr_;
    // 粘性取消覆盖首次模型初始化：取消不能因引擎尚未发布而丢失。
    std::atomic<bool> cancelled_{false};
    std::uint64_t invocation_{};
};
} // namespace wvd::recognition
