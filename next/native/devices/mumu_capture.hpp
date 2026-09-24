#pragma once
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
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

// 厂商接口本次无法取帧（例如目标显示尚未创建）；协议和 helper 本身仍有效。
class MumuCaptureNotReady final : public std::runtime_error {
  public:
    explicit MumuCaptureNotReady(std::uint32_t status)
        : std::runtime_error("MUMU_CAPTURE_NOT_READY:" + std::to_string(status)) {}
};

// 厂商 IPC 仅加载到自有只读进程。是否已回收以进程句柄进入退出态为准；
// 不能用“已调用终止”代替“已退出”，更不能对 MuMu/共享 ADB 套用该终止策略。
class MumuCaptureClient final {
  public:
    MumuCaptureClient(std::filesystem::path helper, std::filesystem::path install_root,
                      std::filesystem::path ipc_library, int instance, std::wstring package);
    ~MumuCaptureClient();
    MumuCaptureClient(const MumuCaptureClient &) = delete;
    MumuCaptureClient &operator=(const MumuCaptureClient &) = delete;
    MumuPixels capture(std::chrono::milliseconds timeout, std::stop_token stop = {});
    bool close() noexcept;
    bool cleanup_pending() const { return cleanup_pending_; }
    std::uint64_t generation() const { return generation_; }

  private:
    std::filesystem::path helper_, root_, library_;
    std::wstring package_;
    int instance_{};
    HANDLE input_{}, output_{}, process_{}, job_{};
    std::uint64_t generation_{}, sequence_{};
    bool job_assigned_{}, cleanup_pending_{};
    void start();
    void read_exact(void *buffer, std::size_t bytes,
                    std::chrono::steady_clock::time_point deadline, std::stop_token stop);
};
} // namespace wvd::devices
