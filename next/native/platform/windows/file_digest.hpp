#pragma once
#include <filesystem>
#include <string>
#include <span>
#include <cstdint>

namespace wvd::platform {
std::string file_sha256(const std::filesystem::path &path);
std::string bytes_sha256(std::span<const std::uint8_t> bytes);
} // namespace wvd::platform
