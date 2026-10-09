#pragma once

#include <chrono>
#include <cstdint>
#include <json.hpp>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace wvd::contracts {
enum class RecognitionOutcome { Hit, NoHit, Error };
struct Size {
    int width{}, height{};
    bool operator==(const Size &) const = default;
};
struct Box {
    int x{}, y{}, width{}, height{};
};
struct Point {
    int x{}, y{};
};

// 异步消费者只能持有自己的帧字节，不能借用截图缓存。
struct FrameIdentity {
    std::string device_id, game_id, pack_revision, viewport_id;
    std::uint64_t generation{}, frame_id{}, action_epoch{};
    Size raw_size, recognition_size;
    std::chrono::steady_clock::time_point captured_at;
    std::string color_format{"BGR8"};
    std::uint64_t connection_generation{};
    std::string backend, foreground_application;
    // captured_at 是采集开始时间；完成时间独立保存，不用重置旧时间掩盖耗时。
    std::chrono::steady_clock::time_point capture_finished_at{};
    int display_rotation{-1};
    std::string instance_id, instance_created_identity;
    bool operator==(const FrameIdentity &) const = default;
};
struct FrameEnvelope {
    FrameIdentity identity;
    std::vector<std::uint8_t> encoded_image;
    // 截图后端可直接交付 BGR 像素；持有者不可修改发布后的帧。
    std::shared_ptr<const std::vector<std::uint8_t>> raw_bgr;
};
// 缺元数据的像素永远不进入识别/输入接口；只供有界取证保存。
struct DiagnosticPixels {
    Size size;
    std::shared_ptr<const std::vector<std::uint8_t>> bgr;
    std::chrono::steady_clock::time_point captured_at{};
    std::string device_id, backend;
};
struct RecognitionMatch {
    Box box;
    double score{};
    std::string text;
};
struct Observation {
    FrameIdentity basis;
    std::string recognizer_id, parameter_revision;
    RecognitionOutcome outcome{RecognitionOutcome::Error};
    std::optional<Box> box;
    std::optional<Point> center;
    bool action_eligible{true};
    std::vector<RecognitionMatch> matches;
    std::string error_code, error_stage;
    nlohmann::json evidence = nlohmann::json::object();
    nlohmann::json timing_ms = nlohmann::json::object();
};
enum class PageAuthority { ReadOnly, InputEligible };
inline PageAuthority page_authority(const Observation &observation) noexcept {
    return observation.outcome == RecognitionOutcome::Hit && observation.action_eligible
        ? PageAuthority::InputEligible : PageAuthority::ReadOnly;
}
} // namespace wvd::contracts
