#pragma once
#include <filesystem>
#include <string>

namespace wvd::platform {
// A directory identity is observed from an opened, non-reparse disk handle.
// This primitive never claims deletion ownership, including on failure.
std::string fixed_directory_identity(const std::filesystem::path &path);
void move_fixed_directory(const std::filesystem::path &source,
    const std::filesystem::path &destination, const std::string &expected_identity);
} // namespace wvd::platform
