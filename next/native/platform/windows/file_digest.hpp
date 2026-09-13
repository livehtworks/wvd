#pragma once
#include <filesystem>
#include <string>

namespace wvd::platform {
std::string file_sha256(const std::filesystem::path &path);
}
