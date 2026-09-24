#include "frame.hpp"
#include <opencv2/imgcodecs.hpp>
#include <stdexcept>

namespace wvd::recognition {
namespace {
void require(bool condition, const char *code) {
    if (!condition) throw std::runtime_error(code);
}
} // namespace

void validate_frame_identity(const contracts::FrameEnvelope &frame,
                             const contracts::FrameIdentity &current,
                             const std::string &revision) {
    const auto &id = frame.identity;
    require(!id.device_id.empty() && !id.game_id.empty() && !id.viewport_id.empty() &&
                !revision.empty() && id.generation > 0 && id.frame_id > 0,
            "FRAME_IDENTITY_INVALID");
    require(id.device_id == current.device_id && id.game_id == current.game_id &&
                id.pack_revision == revision && id.pack_revision == current.pack_revision &&
                id.generation == current.generation, "FRAME_CONTEXT_MISMATCH");
    require(id.frame_id == current.frame_id && id.action_epoch == current.action_epoch &&
                id.connection_generation == current.connection_generation, "FRAME_STALE");
    require(id.viewport_id == current.viewport_id && id.raw_size == current.raw_size &&
                id.recognition_size == current.recognition_size, "FRAME_VIEWPORT_MISMATCH");
    require(id.color_format == "BGR8" && current.color_format == "BGR8", "FRAME_COLOR_INVALID");
    require(id.captured_at == current.captured_at &&
                id.capture_finished_at == current.capture_finished_at &&
                id.display_rotation == current.display_rotation &&
                id.captured_at.time_since_epoch().count() > 0 &&
                id.captured_at <= std::chrono::steady_clock::now() &&
                (id.capture_finished_at.time_since_epoch().count() == 0 ||
                 (id.capture_finished_at >= id.captured_at &&
                  id.capture_finished_at <= std::chrono::steady_clock::now())),
            "FRAME_TIME_INVALID");
    require(id.raw_size.width > 0 && id.raw_size.height > 0 && id.recognition_size.width > 0 &&
                id.recognition_size.height > 0 && id.raw_size.width <= 16384 &&
                id.raw_size.height <= 16384 && id.recognition_size.width <= 16384 &&
                id.recognition_size.height <= 16384, "FRAME_SIZE_INVALID");
    require(static_cast<std::int64_t>(id.raw_size.width) * id.recognition_size.height ==
                static_cast<std::int64_t>(id.raw_size.height) * id.recognition_size.width,
            "FRAME_ASPECT_MISMATCH");
    const bool raw = bool(frame.raw_bgr);
    require(raw != !frame.encoded_image.empty(), "FRAME_PAYLOAD_AMBIGUOUS");
    if (raw) {
        const auto count = static_cast<std::uint64_t>(id.recognition_size.width) *
                           id.recognition_size.height * 3;
        require(frame.raw_bgr->size() == count, "FRAME_BYTES_INVALID");
    } else
        require(!frame.encoded_image.empty() && frame.encoded_image.size() <= 64 * 1024 * 1024,
                "FRAME_BYTES_INVALID");
}

FramePixels::FramePixels(const contracts::FrameEnvelope &frame,
                         const contracts::FrameIdentity &current,
                         const std::string &revision) {
    validate_frame_identity(frame, current, revision);
    const auto size = frame.identity.recognition_size;
    if (frame.raw_bgr) {
        owned_raw_ = frame.raw_bgr;
        cv::Mat borrowed(size.height, size.width, CV_8UC3,
                         const_cast<std::uint8_t *>(owned_raw_->data()));
        pixels_ = borrowed;
    } else
        pixels_ = cv::imdecode(frame.encoded_image, cv::IMREAD_COLOR);
    require(!pixels_.empty() && pixels_.isContinuous() && pixels_.type() == CV_8UC3 &&
                pixels_.cols == size.width && pixels_.rows == size.height,
            "FRAME_DECODE_INVALID");
}

std::span<const std::uint8_t> FramePixels::bgr() const {
    return {pixels_.ptr<const std::uint8_t>(), pixels_.total() * pixels_.elemSize()};
}

contracts::Size FramePixels::size() const { return {pixels_.cols, pixels_.rows}; }
} // namespace wvd::recognition
