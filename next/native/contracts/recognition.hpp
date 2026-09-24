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
    bool operator==(const FrameIdentity &) const = default;
};
struct FrameEnvelope {
    FrameIdentity identity;
    std::vector<std::uint8_t> encoded_image;
    // 截图后端可直接交付 BGR 像素；持有者不可修改发布后的帧。
    std::shared_ptr<const std::vector<std::uint8_t>> raw_bgr;
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
} // namespace wvd::contracts
