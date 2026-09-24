#pragma once

#include "contracts/recognition.hpp"
#include <opencv2/core.hpp>
#include <span>

namespace wvd::recognition {
void validate_frame_identity(const contracts::FrameEnvelope &frame,
                             const contracts::FrameIdentity &current,
                             const std::string &pack_revision);

class FramePixels final {
  public:
    FramePixels(const contracts::FrameEnvelope &frame,
                const contracts::FrameIdentity &current,
                const std::string &pack_revision);
    std::span<const std::uint8_t> bgr() const;
    contracts::Size size() const;
    const cv::Mat &mat() const { return pixels_; }

  private:
    std::shared_ptr<const std::vector<std::uint8_t>> owned_raw_;
    cv::Mat pixels_;
};
} // namespace wvd::recognition
