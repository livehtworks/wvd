#pragma once

#include <chrono>
#include <cstdint>
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

// 值对象不携带 Maa 指针。异步消费者只能持有自己的帧字节，不能借用截图缓存。
struct FrameIdentity {
    std::string device_id, game_id, pack_revision, viewport_id;
    std::uint64_t generation{}, frame_id{}, action_epoch{};
    Size raw_size, recognition_size;
    std::chrono::steady_clock::time_point captured_at;
    std::string color_format{"BGR8"};
};
struct FrameEnvelope {
    FrameIdentity identity;
    std::vector<std::uint8_t> encoded_image;
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
    std::vector<RecognitionMatch> matches;
    std::int64_t engine_task_id{}, engine_reco_id{};
    int engine_status{};
    std::string error_code, error_stage;
};
} // namespace wvd::contracts
