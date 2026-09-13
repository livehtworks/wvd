#pragma once
#include <filesystem>
#include <memory>
#include <string>

namespace wvd::platform {
std::string unique_id();
void atomic_write(const std::filesystem::path &target, const std::string &contents, bool replace);
class DeviceLease {
  public:
    explicit DeviceLease(const std::string &identity);
    ~DeviceLease();
    DeviceLease(const DeviceLease &) = delete;
    DeviceLease &operator=(const DeviceLease &) = delete;

  private:
    void *handle_{};
};
} // namespace wvd::platform
