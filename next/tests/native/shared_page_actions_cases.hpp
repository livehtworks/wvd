#pragma once
#include <filesystem>

namespace shared_page_actions_contract_test {
int run(const std::filesystem::path &next_root, const std::filesystem::path &output,
        bool expect_fixed = true);
}
