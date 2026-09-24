#pragma once
#include <filesystem>
#include <string>

namespace wvd::platform {
inline std::string utf8(const std::filesystem::path &path) {
    const auto value = path.u8string();
    return {value.begin(), value.end()};
}
inline std::filesystem::path path_from_utf8(const std::string &value) {
    return std::filesystem::path(std::u8string(value.begin(), value.end()));
}
} // namespace wvd::platform
