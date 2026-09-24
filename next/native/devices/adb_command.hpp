#pragma once

#include "android_probe.hpp"
#include "platform/windows/process.hpp"
#include <filesystem>
#include <stop_token>
#include <string>
#include <vector>

namespace wvd::devices {
class AdbCommandClient final {
  public:
    AdbCommandClient(std::filesystem::path executable, std::string serial);
    platform::ProcessResult run(const std::vector<std::wstring> &arguments,
                                std::chrono::milliseconds timeout,
                                std::stop_token stop = {},
                                std::size_t output_limit = 2 * 1024 * 1024) const;
    android::ShellReply shell_fixed(const std::string &command,
                                    std::chrono::milliseconds timeout,
                                    std::stop_token stop = {}) const;
    std::vector<std::uint8_t> screenshot_png(std::chrono::milliseconds timeout,
                                              std::stop_token stop = {}) const;
    bool connected(std::stop_token stop = {}) const;
    bool connect(std::stop_token stop = {}) const;
    const std::string &serial() const { return serial_; }
    const std::filesystem::path &executable() const { return executable_; }

  private:
    std::filesystem::path executable_;
    std::string serial_;
};
} // namespace wvd::devices
