#include "native_input_gate.hpp"
#include "platform/execution_timing.hpp"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <stdexcept>

namespace wvd::devices {
namespace {
void require(bool condition, const char *code) {
    if (!condition) throw std::runtime_error(code);
}
bool same(const contracts::FrameIdentity &a, const contracts::FrameIdentity &b) {
    return a == b;
}
bool inside(int x, int y, const contracts::Box &box) {
    return x >= box.x && y >= box.y &&
        static_cast<std::int64_t>(x) < static_cast<std::int64_t>(box.x) + box.width &&
        static_cast<std::int64_t>(y) < static_cast<std::int64_t>(box.y) + box.height;
}
} // namespace

NativeInputGate::NativeInputGate(DeviceBackend &backend, contracts::InputPolicy policy,
                                 std::uint64_t generation)
    : backend_(backend), policy_(std::move(policy)), generation_(generation) {
    require(generation_ > 0 && !policy_.device_id.empty() && !policy_.game_id.empty() &&
            !policy_.application_id.empty() && !policy_.pack_revision.empty() &&
            policy_.recognition_size.width > 0 && policy_.recognition_size.height > 0,
            "NATIVE_GATE_POLICY_INVALID");
}

contracts::FrameEnvelope NativeInputGate::capture() {
    std::lock_guard dispatch(dispatch_mutex_);
    if (stopped()) throw std::runtime_error("CAPTURE_CANCELLED");
    if (!policy_.observed_read_only_viewport && !policy_.permissions.empty())
        backend_.prepare_input_channel(stop_source_.get_token());
    const auto raw = backend_.capture(stop_source_.get_token());
    if (stopped()) throw std::runtime_error("CAPTURE_CANCELLED");
    require(raw.device_id == policy_.device_id && raw.size.width > 0 && raw.size.height > 0 &&
            raw.size.width <= 16384 && raw.size.height <= 16384 &&
            raw.captured_at.time_since_epoch().count() > 0 &&
            raw.connection_generation > 0 && !raw.viewport_id.empty(),
            "CAPTURE_IDENTITY_INVALID");
    auto recognition_size = policy_.observed_read_only_viewport ||
        raw.foreground_application != policy_.application_id
            ? raw.size : policy_.recognition_size;
    require(static_cast<std::int64_t>(raw.size.width) * recognition_size.height ==
                static_cast<std::int64_t>(raw.size.height) * recognition_size.width,
            "CAPTURE_ASPECT_MISMATCH");
    contracts::FrameEnvelope result;
    if (raw.raw_bgr) {
        require(raw.raw_bgr->size() == static_cast<std::uint64_t>(raw.size.width) *
                    raw.size.height * 3, "CAPTURE_BYTES_INVALID");
        if (raw.size == recognition_size) result.raw_bgr = raw.raw_bgr;
        else {
            const cv::Mat original(raw.size.height, raw.size.width, CV_8UC3,
                const_cast<std::uint8_t *>(raw.raw_bgr->data()));
            cv::Mat resized;
            cv::resize(original, resized, cv::Size(recognition_size.width,
                recognition_size.height), 0, 0, cv::INTER_AREA);
            result.raw_bgr = std::make_shared<const std::vector<std::uint8_t>>(
                resized.data, resized.data + resized.total() * resized.elemSize());
        }
    } else {
        require(!raw.encoded.empty(), "CAPTURE_BYTES_MISSING");
        if (raw.size == recognition_size) result.encoded_image = raw.encoded;
        else {
            const cv::Mat original = cv::imdecode(raw.encoded, cv::IMREAD_COLOR);
            require(!original.empty() && original.cols == raw.size.width &&
                    original.rows == raw.size.height, "CAPTURE_DECODE_INVALID");
            cv::Mat resized;
            cv::resize(original, resized, cv::Size(recognition_size.width,
                recognition_size.height), 0, 0, cv::INTER_AREA);
            result.raw_bgr = std::make_shared<const std::vector<std::uint8_t>>(
                resized.data, resized.data + resized.total() * resized.elemSize());
        }
    }
    {
        std::lock_guard lock(mutex_);
        if (stopped()) throw std::runtime_error("CAPTURE_CANCELLED");
        last_frame_ = {raw.device_id, policy_.game_id, policy_.pack_revision,
            raw.viewport_id, generation_, ++frame_id_, epoch_, raw.size, recognition_size,
            raw.captured_at, "BGR8"};
        last_frame_.connection_generation = raw.connection_generation;
        last_frame_.backend = raw.backend;
        last_frame_.foreground_application = raw.foreground_application;
        last_frame_.capture_finished_at = raw.capture_finished_at;
        last_frame_.display_rotation = raw.display_rotation;
        result.identity = last_frame_;
    }
    return result;
}

contracts::Command NativeInputGate::map_command(const contracts::Command &input,
                                                 const contracts::FrameIdentity &basis) const {
    auto result = input;
    const auto map_x = [&](int value) {
        return static_cast<int>((static_cast<std::int64_t>(value) * basis.raw_size.width) /
                                basis.recognition_size.width);
    };
    const auto map_y = [&](int value) {
        return static_cast<int>((static_cast<std::int64_t>(value) * basis.raw_size.height) /
                                basis.recognition_size.height);
    };
    using contracts::ActionKind;
    if (result.kind == ActionKind::Click || result.kind == ActionKind::Swipe ||
        result.kind == ActionKind::TouchDown || result.kind == ActionKind::TouchMove ||
        result.kind == ActionKind::TouchUp) {
        result.x = map_x(input.x);
        result.y = map_y(input.y);
    }
    if (result.kind == ActionKind::Swipe) {
        result.x2 = map_x(input.x2);
        result.y2 = map_y(input.y2);
    }
    return result;
}

InputReceipt NativeInputGate::submit(const contracts::Command &command,
                                      const contracts::Observation &scene,
                                      const contracts::Observation &target,
                                      const contracts::Box &area) {
    platform::timing::Scope measure(platform::timing::Part::InputValidation);
    std::lock_guard dispatch(dispatch_mutex_);
    if (stopped()) return {InputDisposition::Rejected, 0, {}, "STOP_REQUESTED"};
    contracts::FrameIdentity frame;
    std::uint64_t next_epoch{};
    {
        std::lock_guard lock(mutex_);
        ++counts_.attempted;
        frame = last_frame_;
        if (stopped() || !frame.frame_id || frame.action_epoch != epoch_ ||
            !same(scene.basis, frame) || !same(target.basis, frame) ||
            scene.outcome != contracts::RecognitionOutcome::Hit ||
            target.outcome != contracts::RecognitionOutcome::Hit ||
            frame.foreground_application != policy_.application_id ||
            frame.display_rotation < 0 || area.width <= 0 || area.height <= 0 ||
            area.x < 0 || area.y < 0 ||
            static_cast<std::int64_t>(area.x) + area.width > frame.recognition_size.width ||
            static_cast<std::int64_t>(area.y) + area.height > frame.recognition_size.height ||
            !policy_.allowed_scenes.contains(policy_.game_id) ||
            !policy_.capabilities.contains(command.kind) ||
            !policy_.permissions.contains(command.kind)) {
            ++counts_.rejected;
            return {InputDisposition::Rejected, 0, {}, "INPUT_EVIDENCE_OR_POLICY_INVALID"};
        }
        if (policy_.max_frame_age.count() > 0 &&
            std::chrono::steady_clock::now() - frame.captured_at > policy_.max_frame_age) {
            ++counts_.rejected;
            return {InputDisposition::Rejected, 0, {}, "INPUT_EVIDENCE_EXPIRED"};
        }
        if (command.kind == contracts::ActionKind::Click &&
            !inside(command.x, command.y, area)) {
            ++counts_.rejected;
            return {InputDisposition::Rejected, 0, {}, "INPUT_OUTSIDE_ALLOWED_AREA"};
        }
        if (command.kind == contracts::ActionKind::Swipe &&
            (!inside(command.x, command.y, area) ||
             !inside(command.x2, command.y2, area))) {
            ++counts_.rejected;
            return {InputDisposition::Rejected, 0, {}, "INPUT_OUTSIDE_ALLOWED_AREA"};
        }
        ++counts_.accepted;
        next_epoch = ++epoch_;
        // 消费的是一次性观察证据，不是由毫秒数决定的许可。
        // 即使随后上下文检查拒绝，也要重新观察，不能重复使用同一帧发输入。
        last_frame_ = {};
    }
    bool backend_started = false;
    try {
        if (stopped() || !backend_.context_matches(frame, policy_.application_id, stop_source_.get_token())) {
            std::lock_guard lock(mutex_);
            ++counts_.rejected;
            return {InputDisposition::Rejected, next_epoch, {}, "INPUT_CONTEXT_CHANGED"};
        }
        if (stopped()) return {InputDisposition::Rejected, next_epoch, {}, "STOP_REQUESTED"};
        {
            std::lock_guard lock(mutex_);
            ++counts_.backend_called;
        }
        backend_started = true;
        const bool sent = backend_.execute(map_command(command, frame), stop_source_.get_token());
        if (!sent) return {InputDisposition::Rejected, next_epoch, {}, "INPUT_NOT_SENT"};
        return {InputDisposition::Submitted, next_epoch,
                std::chrono::steady_clock::now(), {}};
    } catch (const std::exception &error) {
        // 设备上下文查询失败时尚未调用输入；只有进入输入通道后才可能送达未知。
        return {backend_started ? InputDisposition::Unresolved : InputDisposition::Rejected,
                next_epoch, std::chrono::steady_clock::now(), error.what()};
    }
}

void NativeInputGate::stop() {
    stopped_.store(true);
    stop_source_.request_stop();
}
bool NativeInputGate::cleanup() {
    {
        std::lock_guard lock(mutex_);
        ++counts_.cleanup_called;
    }
    return backend_.release_owned_inputs();
}
bool NativeInputGate::current(const contracts::FrameIdentity &identity) const {
    std::lock_guard lock(mutex_);
    return !stopped() && identity.frame_id && identity.action_epoch == epoch_ &&
        same(identity, last_frame_);
}
contracts::FrameIdentity NativeInputGate::current_identity() const {
    std::lock_guard lock(mutex_);
    return last_frame_;
}
bool NativeInputGate::reusable(const contracts::FrameIdentity &identity) const {
    return current(identity) && (policy_.max_frame_age.count() <= 0 ||
        std::chrono::steady_clock::now() - identity.captured_at <= policy_.max_frame_age);
}
std::uint64_t NativeInputGate::action_epoch() const {
    std::lock_guard lock(mutex_);
    return epoch_;
}
contracts::InputCounts NativeInputGate::counts() const {
    std::lock_guard lock(mutex_);
    return counts_;
}
} // namespace wvd::devices
