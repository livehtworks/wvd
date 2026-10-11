#pragma once
#include <filesystem>

namespace recorded_input_contract {
int run(const std::filesystem::path &pack, const std::filesystem::path &cases,
        const std::filesystem::path &output);
}
