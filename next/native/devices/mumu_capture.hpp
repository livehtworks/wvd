#pragma once
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <stop_token>
#include <string>
#include <vector>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace wvd::devices {
struct MumuPixels {
    int width{};
    int height{};
    int display_id{};
    std::vector<std::uint8_t> rgba_bottom_up;
};

// The IPC DLL is loaded only in the owned child. A stalled vendor call can never
// strand the application's Run thread after this child is terminated.
class MumuCaptureClient final {
  public:
    MumuCaptureClient(std::filesystem::path helper, std::filesystem::path install_root,
                      std::filesystem::path ipc_library, int instance, std::wstring package);
    ~MumuCaptureClient();
    MumuCaptureClient(const MumuCaptureClient &) = delete;
    MumuCaptureClient &operator=(const MumuCaptureClient &) = delete;
    MumuPixels capture(std::chrono::milliseconds timeout, std::stop_token stop = {});
    void close();
    std::uint64_t generation() const { return generation_; }

  private:
    std::filesystem::path helper_, root_, library_;
    std::wstring package_;
    int instance_{};
    HANDLE input_{}, output_{}, process_{}, job_{};
    std::uint64_t generation_{}, sequence_{};
    void start();
    void read_exact(void *buffer, std::size_t bytes,
                    std::chrono::steady_clock::time_point deadline, std::stop_token stop);
};
} // namespace wvd::devices
