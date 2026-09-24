#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "adb_command.hpp"
#include "contracts/action.hpp"
#include <filesystem>
#include <functional>
#include <set>
#include <stop_token>
#include <string>
#include <winsock2.h>
#include <windows.h>

namespace wvd::devices {
// 只操作明确绑定设备的控制 socket；不是游戏执行器，不做游戏级重试。
class ScrcpyControlClient final {
  public:
    using Cancellation = std::function<bool()>;
    ScrcpyControlClient(const AdbCommandClient &adb, std::filesystem::path server);
    ~ScrcpyControlClient();
    ScrcpyControlClient(const ScrcpyControlClient &) = delete;
    ScrcpyControlClient &operator=(const ScrcpyControlClient &) = delete;
    void connect(std::chrono::milliseconds timeout, std::stop_token stop = {},
                 const Cancellation &cancelled = {});
    // 成功只表示报文写入通道；游戏结果由 FlowExecutor 的后置观察确认。
    void submit(const contracts::Command &command, int width, int height,
                std::stop_token stop = {}, const Cancellation &cancelled = {});
    bool close() noexcept;
    std::string cleanup_status() const;
    bool connected() const { return ready_ && socket_ != INVALID_SOCKET; }
    bool unresolved() const { return unresolved_; }

  private:
    using Clock = std::chrono::steady_clock;
    const AdbCommandClient &adb_;
    std::filesystem::path server_;
    std::uint32_t scid_{};
    std::uint16_t port_{};
    std::string remote_;
    SOCKET socket_{INVALID_SOCKET};
    HANDLE child_{};
    bool forward_{}, uploaded_{}, winsock_{}, unresolved_{}, ready_{}, partial_frame_{};
    bool cleanup_unconfirmed_{}, held_{};
    std::set<int> held_keys_;
    int held_x_{}, held_y_{}, held_width_{}, held_height_{};
    void wait_socket(bool write, Clock::time_point deadline, std::stop_token stop,
                     const Cancellation &cancelled);
    void receive_exact(char *destination, std::size_t count, Clock::time_point deadline,
                       std::stop_token stop, const Cancellation &cancelled);
    void send_bytes(const std::vector<std::uint8_t> &bytes, Clock::time_point deadline,
                    std::stop_token stop, const Cancellation &cancelled);
};
} // namespace wvd::devices
