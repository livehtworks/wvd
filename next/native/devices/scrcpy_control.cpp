#include "scrcpy_control.hpp"
#include "scrcpy_codec.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <ws2tcpip.h>

namespace wvd::devices {
namespace {
using namespace std::chrono_literals;
void check_cancel(std::stop_token stop, const ScrcpyControlClient::Cancellation &cancelled) {
    if (stop.stop_requested() || (cancelled && cancelled())) throw std::runtime_error("SCRCPY_CANCELLED");
}
std::wstring quote(const std::wstring &value) {
    std::wstring result = L"\"";
    unsigned slash{};
    for (auto c : value) {
        if (c == L'\\') { ++slash; continue; }
        if (c == L'"') { result.append(slash * 2 + 1, L'\\'); result.push_back(c); }
        else { result.append(slash, L'\\'); result.push_back(c); }
        slash = 0;
    }
    result.append(slash * 2, L'\\');
    return result + L'"';
}
std::string hex(std::uint32_t value) {
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(8) << value;
    return out.str();
}
void require_adb(const platform::ProcessResult &result, const char *code) {
    if (result.state != platform::ProcessState::Exited || result.exit_code) throw std::runtime_error(code);
}
} // namespace

ScrcpyControlClient::ScrcpyControlClient(const AdbCommandClient &adb, std::filesystem::path server)
    : adb_(adb), server_(std::move(server)) {
    if (!std::filesystem::is_regular_file(server_) || !server_.is_absolute())
        throw std::runtime_error("SCRCPY_SERVER_MISSING");
}
ScrcpyControlClient::~ScrcpyControlClient() {
    if (!close()) {
        OutputDebugStringW(L"WVD scrcpy cleanup unconfirmed during destruction\n");
        if (child_) { CloseHandle(child_); child_ = nullptr; }
    }
}
void ScrcpyControlClient::wait_socket(bool write, Clock::time_point deadline,
    std::stop_token stop, const Cancellation &cancelled) {
    for (;;) {
        check_cancel(stop, cancelled);
        if (Clock::now() >= deadline) throw std::runtime_error("SCRCPY_IO_TIMEOUT");
        fd_set set, errors;
        FD_ZERO(&set); FD_SET(socket_, &set);
        FD_ZERO(&errors); FD_SET(socket_, &errors);
        timeval interval{0, 25000};
        const int result = select(0, write ? nullptr : &set, write ? &set : nullptr, &errors, &interval);
        if (result == SOCKET_ERROR) throw std::runtime_error("SCRCPY_SOCKET_ERROR");
        if (result > 0) { check_cancel(stop, cancelled); return; }
    }
}
void ScrcpyControlClient::receive_exact(char *data, std::size_t count, Clock::time_point deadline,
    std::stop_token stop, const Cancellation &cancelled) {
    while (count) {
        wait_socket(false, deadline, stop, cancelled);
        const int got = recv(socket_, data, static_cast<int>(count), 0);
        if (got == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) continue;
        if (got <= 0) throw std::runtime_error("SCRCPY_HANDSHAKE_CLOSED");
        data += got; count -= static_cast<std::size_t>(got);
    }
}
void ScrcpyControlClient::connect(std::chrono::milliseconds timeout, std::stop_token stop,
    const Cancellation &cancelled) {
    check_cancel(stop, cancelled);
    if (connected()) return;
    if (child_ || forward_ || uploaded_ || cleanup_unconfirmed_)
        throw std::runtime_error("SCRCPY_CLEANUP_PENDING");
    if (timeout < 1000ms || timeout > 60000ms) throw std::runtime_error("SCRCPY_TIMEOUT_INVALID");
    const auto deadline = Clock::now() + timeout;
    const auto remaining = [&](std::chrono::milliseconds ceiling) {
        check_cancel(stop, cancelled);
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
        if (left <= 0ms) throw std::runtime_error("SCRCPY_CONNECT_TIMEOUT");
        return std::min(left, ceiling);
    };
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data)) throw std::runtime_error("SCRCPY_WINSOCK_START_FAILED");
    winsock_ = true;
    try {
        std::random_device random;
        // scrcpy 3.3.4 的服务端按 Java 非负 int 解析十六进制 scid。
        scid_ = (static_cast<std::uint32_t>(random()) ^ GetCurrentProcessId()) & 0x7fffffffU;
        if (!scid_) scid_ = 1;
        const auto id = hex(scid_);
        remote_ = "/data/local/tmp/wvd_scrcpy_" + id;
        const auto name = std::string("localabstract:scrcpy_") + id;
        SOCKET reservation = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (reservation == INVALID_SOCKET) throw std::runtime_error("SCRCPY_PORT_CREATE_FAILED");
        sockaddr_in address{};
        address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        int length = sizeof(address);
        if (bind(reservation, reinterpret_cast<sockaddr *>(&address), length) ||
            getsockname(reservation, reinterpret_cast<sockaddr *>(&address), &length)) {
            closesocket(reservation); throw std::runtime_error("SCRCPY_PORT_RESERVE_FAILED");
        }
        port_ = ntohs(address.sin_port); closesocket(reservation);
        const auto local = std::string("tcp:") + std::to_string(port_);
        require_adb(adb_.run({L"push", server_.wstring(), std::wstring(remote_.begin(), remote_.end())},
            remaining(20000ms), stop), "SCRCPY_PUSH_FAILED");
        uploaded_ = true;
        // 端口释放与 forward 间有竞争窗口；--no-rebind 防止覆盖其他客户端的转发。
        require_adb(adb_.run({L"forward", L"--no-rebind", std::wstring(local.begin(), local.end()),
            std::wstring(name.begin(), name.end())}, remaining(5000ms), stop), "SCRCPY_FORWARD_FAILED");
        forward_ = true;
        check_cancel(stop, cancelled);
        std::wstring command = quote(adb_.executable().wstring()) + L" -s " +
            quote(std::wstring(adb_.serial().begin(), adb_.serial().end())) +
            L" shell CLASSPATH=" + std::wstring(remote_.begin(), remote_.end()) +
            L" app_process / com.genymobile.scrcpy.Server 3.3.4 scid=" + std::wstring(id.begin(), id.end()) +
            L" log_level=error video=false audio=false control=true tunnel_forward=true"
            L" send_dummy_byte=true send_device_meta=true clipboard_autosync=false power_on=false";
        STARTUPINFOW startup{}; startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(adb_.executable().c_str(), command.data(), nullptr, nullptr,
            FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
            throw std::runtime_error("SCRCPY_LAUNCH_FAILED");
        child_ = process.hProcess; CloseHandle(process.hThread);
        address.sin_port = htons(port_);
        while (Clock::now() < deadline) {
            check_cancel(stop, cancelled);
            if (WaitForSingleObject(child_, 0) == WAIT_OBJECT_0) throw std::runtime_error("SCRCPY_SERVER_EXITED");
            socket_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (socket_ == INVALID_SOCKET) throw std::runtime_error("SCRCPY_SOCKET_FAILED");
            u_long nonblocking = 1;
            if (ioctlsocket(socket_, FIONBIO, &nonblocking)) throw std::runtime_error("SCRCPY_NONBLOCK_FAILED");
            int connected = ::connect(socket_, reinterpret_cast<sockaddr *>(&address), sizeof(address));
            if (connected == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) {
                wait_socket(true, deadline, stop, cancelled);
                int error{}; int size = sizeof(error);
                connected = getsockopt(socket_, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&error), &size) == 0 &&
                    error == 0 ? 0 : SOCKET_ERROR;
            }
            if (connected == 0) {
                // ADB forward 的主机端口可能早于 Android server 就绪。
                // 只有收到 server 的 dummy 才离开启动重试；没有发送任何业务输入。
                wait_socket(false, deadline, stop, cancelled);
                char dummy{};
                const int count = recv(socket_, &dummy, 1, 0);
                if (count == 1) {
                    if (dummy != 0) throw std::runtime_error("SCRCPY_DUMMY_BYTE_INVALID");
                    break;
                }
                if (count == SOCKET_ERROR && WSAGetLastError() != WSAECONNRESET &&
                    WSAGetLastError() != WSAECONNABORTED && WSAGetLastError() != WSAEWOULDBLOCK)
                    throw std::runtime_error("SCRCPY_DUMMY_READ_FAILED");
                // EOF/连接重置：本次 server 尚未接受通道，仍在同一总期限内等启动。
            }
            closesocket(socket_); socket_ = INVALID_SOCKET;
            std::this_thread::sleep_for(25ms);
        }
        if (socket_ == INVALID_SOCKET) throw std::runtime_error("SCRCPY_CONNECT_TIMEOUT");
        std::array<char, 64> metadata{};
        receive_exact(metadata.data(), metadata.size(), deadline, stop, cancelled);
        if (!metadata.front()) throw std::runtime_error("SCRCPY_METADATA_INVALID");
        check_cancel(stop, cancelled);
        ready_ = true; // socket 建立不等于版本/元数据握手完成。
    } catch (...) { close(); throw; }
}
void ScrcpyControlClient::send_bytes(const std::vector<std::uint8_t> &bytes, Clock::time_point deadline,
    std::stop_token stop, const Cancellation &cancelled) {
    std::size_t offset{};
    try {
        while (offset < bytes.size()) {
            wait_socket(true, deadline, stop, cancelled);
            const int count = send(socket_, reinterpret_cast<const char *>(bytes.data() + offset),
                static_cast<int>(bytes.size() - offset), 0);
            if (count == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) continue;
            if (count <= 0) throw std::runtime_error("SCRCPY_SEND_UNRESOLVED");
            offset += static_cast<std::size_t>(count);
            partial_frame_ = offset < bytes.size();
        }
    } catch (...) {
        if (offset) unresolved_ = true;
        throw;
    }
}
void ScrcpyControlClient::submit(const contracts::Command &command, int width, int height,
    std::stop_token stop, const Cancellation &cancelled) {
    check_cancel(stop, cancelled);
    if (!connected() || partial_frame_) throw std::runtime_error("SCRCPY_NOT_READY");
    const auto messages = scrcpy::encode(command, width, height);
    const auto transport_duration = command.kind == contracts::ActionKind::Swipe ? command.duration : 0;
    const auto deadline = Clock::now() + 5000ms + std::chrono::milliseconds{transport_duration};
    bool sent{};
    try {
        for (std::size_t i = 0; i < messages.size(); ++i) {
            const auto &message = messages[i];
            send_bytes(message, deadline, stop, cancelled);
            sent = true;
            if (message[0] == 2) {
                if (message[1] == 1) held_ = false;
                else {
                    held_ = true;
                    const auto number = [&](std::size_t at) {
                        return static_cast<int>((std::uint32_t(message[at]) << 24) |
                            (std::uint32_t(message[at + 1]) << 16) |
                            (std::uint32_t(message[at + 2]) << 8) | message[at + 3]);
                    };
                    held_x_ = number(10); held_y_ = number(14);
                    held_width_ = width; held_height_ = height;
                }
            } else if (message[0] == 0) {
                if (message[1] == 0) held_keys_.insert(command.key);
                else held_keys_.erase(command.key);
            }
            if (command.kind == contracts::ActionKind::Swipe && i + 1 < messages.size()) {
                const auto until = Clock::now() + std::chrono::milliseconds{
                    command.duration / static_cast<int>(messages.size() - 1)};
                while (Clock::now() < until) { check_cancel(stop, cancelled); std::this_thread::sleep_for(5ms); }
            }
        }
    } catch (...) { if (sent) unresolved_ = true; throw; }
}
bool ScrcpyControlClient::close() noexcept {
    ready_ = false;
    if (socket_ != INVALID_SOCKET) {
        // 半个报文不能再拼接另一条释放报文；保持“释放未确认”，不得伪报静止。
        if (partial_frame_) cleanup_unconfirmed_ = true;
        else {
            const auto deadline = Clock::now() + 1000ms;
            try {
                if (held_) {
                    send_bytes(scrcpy::touch(1, UINT64_MAX - 1, held_x_, held_y_,
                        held_width_, held_height_, false), deadline, {}, {});
                    held_ = false;
                }
                while (!held_keys_.empty()) {
                    const auto key = *held_keys_.begin();
                    send_bytes(scrcpy::key(1, key), deadline, {}, {});
                    held_keys_.erase(key);
                }
            } catch (...) { cleanup_unconfirmed_ = true; }
        }
        shutdown(socket_, SD_BOTH); closesocket(socket_); socket_ = INVALID_SOCKET;
    }
    if (child_) {
        if (WaitForSingleObject(child_, 1000) != WAIT_OBJECT_0) {
            // 仅清理本工具启动的 adb 客户端；绝不杀共享 adb server 或 MuMu。
            TerminateProcess(child_, 1); // 是否成功以接下来的退出态检查为准。
        }
        if (WaitForSingleObject(child_, 1000) == WAIT_OBJECT_0) { CloseHandle(child_); child_ = nullptr; }
        // 未退出时保留 child_，下次显式清理仍可检查它。
    }
    if (forward_) {
        try {
            const auto local = std::string("tcp:") + std::to_string(port_);
            require_adb(adb_.run({L"forward", L"--remove", std::wstring(local.begin(), local.end())}, 3000ms),
                        "SCRCPY_FORWARD_REMOVE_FAILED");
            forward_ = false;
        } catch (...) { /* forward_ 保留，允许再次清理，不伪称已删除。 */ }
    }
    if (uploaded_) {
        try {
            // 上一次清理可能已删除文件但回执超时；重试必须幂等。
            if (adb_.shell_fixed("rm -f " + remote_, 3000ms).exit_code != 0)
                throw std::runtime_error("SCRCPY_REMOTE_REMOVE_FAILED");
            uploaded_ = false;
        } catch (...) { /* uploaded_ 保留；只处理本次明确拥有的远端文件。 */ }
    }
    if (winsock_) { WSACleanup(); winsock_ = false; }
    return !cleanup_unconfirmed_ && !held_ && held_keys_.empty() && !child_ && !forward_ && !uploaded_;
}
std::string ScrcpyControlClient::cleanup_status() const {
    std::ostringstream out;
    out << "child=" << bool(child_) << ",forward=" << forward_
        << ",uploaded=" << uploaded_ << ",socket=" << (socket_ != INVALID_SOCKET)
        << ",held=" << held_ << ",keys=" << held_keys_.size()
        << ",partial=" << partial_frame_ << ",unconfirmed=" << cleanup_unconfirmed_
        << ",unresolved=" << unresolved_;
    return out.str();
}
} // namespace wvd::devices
