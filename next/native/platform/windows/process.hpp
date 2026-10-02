#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <stop_token>
#include <string>
#include <vector>

namespace wvd::platform {
enum class ProcessState { Exited, TimedOut, Cancelled };
struct ProcessResult {
    ProcessState state{ProcessState::Exited};
    unsigned long exit_code{};
    std::vector<std::uint8_t> stdout_bytes;
    std::vector<std::uint8_t> stderr_bytes;
};

// 长期服务独立运行，退出由服务自身的正式接口负责；不继承调用Shell的额外句柄。
std::uint32_t launch_background(const std::filesystem::path &executable,
                               const std::vector<std::wstring> &arguments,
                               const std::filesystem::path &stdout_log,
                               const std::filesystem::path &stderr_log);

ProcessResult run_process(const std::filesystem::path &executable,
                          const std::vector<std::wstring> &arguments,
                          std::chrono::milliseconds timeout, std::stop_token stop = {},
                          std::size_t output_limit = 2 * 1024 * 1024,
                          bool own_child_tree = true);
} // namespace wvd::platform
