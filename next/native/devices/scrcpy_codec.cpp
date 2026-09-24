#include "scrcpy_codec.hpp"
#include <algorithm>
#include <stdexcept>

namespace wvd::devices::scrcpy {
namespace {
void be16(std::vector<std::uint8_t> &bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value >> 8);
    bytes[offset + 1] = static_cast<std::uint8_t>(value);
}
void be32(std::vector<std::uint8_t> &bytes, std::size_t offset, std::uint32_t value) {
    for (int i = 3; i >= 0; --i) { bytes[offset + i] = static_cast<std::uint8_t>(value); value >>= 8; }
}
void be64(std::vector<std::uint8_t> &bytes, std::size_t offset, std::uint64_t value) {
    for (int i = 7; i >= 0; --i) { bytes[offset + i] = static_cast<std::uint8_t>(value); value >>= 8; }
}
} // namespace

std::vector<std::uint8_t> touch(std::uint8_t action, std::uint64_t pointer,
                                int x, int y, int width, int height, bool pressed) {
    if (action > 2 || width <= 0 || width > 65535 || height <= 0 || height > 65535 ||
        x < 0 || x >= width || y < 0 || y >= height)
        throw std::runtime_error("SCRCPY_TOUCH_COORDINATES_INVALID");
    std::vector<std::uint8_t> bytes(32);
    bytes[0] = 2;
    bytes[1] = action;
    be64(bytes, 2, pointer);
    be32(bytes, 10, static_cast<std::uint32_t>(x));
    be32(bytes, 14, static_cast<std::uint32_t>(y));
    be16(bytes, 18, static_cast<std::uint16_t>(width));
    be16(bytes, 20, static_cast<std::uint16_t>(height));
    be16(bytes, 22, pressed ? 0xffff : 0);
    return bytes;
}
std::vector<std::uint8_t> key(std::uint8_t action, int android_keycode) {
    if (action > 1 || android_keycode < 0 || android_keycode > 65535)
        throw std::runtime_error("SCRCPY_KEY_INVALID");
    std::vector<std::uint8_t> bytes(14);
    bytes[0] = 0;
    bytes[1] = action;
    be32(bytes, 2, static_cast<std::uint32_t>(android_keycode));
    return bytes;
}
std::vector<std::vector<std::uint8_t>> encode(const contracts::Command &command,
                                               int width, int height) {
    constexpr std::uint64_t finger = UINT64_MAX - 1;
    using contracts::ActionKind;
    switch (command.kind) {
    case ActionKind::Click:
        return {touch(0, finger, command.x, command.y, width, height, true),
                touch(1, finger, command.x, command.y, width, height, false)};
    case ActionKind::TouchDown:
        return {touch(0, finger, command.x, command.y, width, height, true)};
    case ActionKind::TouchMove:
        return {touch(2, finger, command.x, command.y, width, height, true)};
    case ActionKind::TouchUp:
        return {touch(1, finger, command.x, command.y, width, height, false)};
    case ActionKind::Swipe: {
        if (command.duration < 1 || command.duration > 10000)
            throw std::runtime_error("SCRCPY_SWIPE_DURATION_INVALID");
        const int segments = std::clamp(command.duration / 25, 2, 80);
        std::vector<std::vector<std::uint8_t>> messages;
        messages.reserve(static_cast<std::size_t>(segments) + 2);
        messages.push_back(touch(0, finger, command.x, command.y, width, height, true));
        for (int i = 1; i < segments; ++i) {
            const auto x = command.x +
                static_cast<int>((static_cast<std::int64_t>(command.x2 - command.x) * i) / segments);
            const auto y = command.y +
                static_cast<int>((static_cast<std::int64_t>(command.y2 - command.y) * i) / segments);
            messages.push_back(touch(2, finger, x, y, width, height, true));
        }
        messages.push_back(touch(1, finger, command.x2, command.y2, width, height, false));
        return messages;
    }
    case ActionKind::ClickKey:
        return {key(0, command.key), key(1, command.key)};
    case ActionKind::KeyDown:
        return {key(0, command.key)};
    case ActionKind::KeyUp:
        return {key(1, command.key)};
    default:
        throw std::runtime_error("SCRCPY_COMMAND_UNSUPPORTED");
    }
}
} // namespace wvd::devices::scrcpy
