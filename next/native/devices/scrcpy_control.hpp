#pragma once
#include "adb_command.hpp"
#include "contracts/action.hpp"
#include <filesystem>
#include <stop_token>
#include <string>
#include <winsock2.h>
#include <windows.h>

namespace wvd::devices {
class ScrcpyControlClient final {
  public:
    ScrcpyControlClient(const AdbCommandClient &adb, std::filesystem::path server);
    ~ScrcpyControlClient();
    ScrcpyControlClient(const ScrcpyControlClient &) = delete;
    ScrcpyControlClient &operator=(const ScrcpyControlClient &) = delete;
    void connect(std::chrono::milliseconds timeout, std::stop_token stop = {});
    // A successful return only confirms that bytes reached the control socket.
    void submit(const contracts::Command &command, int width, int height,
                std::stop_token stop = {});
    void close() noexcept;
    bool connected() const { return socket_ != INVALID_SOCKET; }
    bool unresolved() const { return unresolved_; }

  private:
    const AdbCommandClient &adb_;
    std::filesystem::path server_;
    std::uint32_t scid_{};
    std::uint16_t port_{};
    std::string remote_;
    SOCKET socket_{INVALID_SOCKET};
    HANDLE child_{};
    bool forward_{}, uploaded_{}, winsock_{}, unresolved_{};
    bool held_{};
    int held_x_{}, held_y_{}, held_width_{}, held_height_{};
    void send_bytes(const std::vector<std::uint8_t> &bytes, std::stop_token stop);
};
} // namespace wvd::devices
