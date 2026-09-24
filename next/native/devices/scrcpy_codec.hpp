#pragma once
#include "contracts/action.hpp"
#include <cstdint>
#include <vector>

namespace wvd::devices::scrcpy {
// Wire format pinned to scrcpy 3.3.4, app/src/control_msg.c.
std::vector<std::uint8_t> touch(std::uint8_t action, std::uint64_t pointer,
                                int x, int y, int width, int height,
                                bool pressed);
std::vector<std::uint8_t> key(std::uint8_t action, int android_keycode);
std::vector<std::vector<std::uint8_t>> encode(const contracts::Command &command,
                                               int width, int height);
} // namespace wvd::devices::scrcpy
