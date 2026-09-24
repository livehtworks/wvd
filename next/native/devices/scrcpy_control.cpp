#include "scrcpy_control.hpp"
#include "scrcpy_codec.hpp"
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
    if (result.state != platform::ProcessState::Exited || result.exit_code)
        throw std::runtime_error(code);
}
} // namespace

ScrcpyControlClient::ScrcpyControlClient(const AdbCommandClient &adb,
                                         std::filesystem::path server)
    : adb_(adb), server_(std::move(server)) {
    if (!std::filesystem::is_regular_file(server_) || !server_.is_absolute())
        throw std::runtime_error("SCRCPY_SERVER_MISSING");
}
ScrcpyControlClient::~ScrcpyControlClient() { close(); }

void ScrcpyControlClient::connect(std::chrono::milliseconds timeout, std::stop_token stop) {
    if (connected()) return;
    if (timeout < 1000ms || timeout > 60000ms)
        throw std::runtime_error("SCRCPY_TIMEOUT_INVALID");
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data)) throw std::runtime_error("SCRCPY_WINSOCK_START_FAILED");
    winsock_ = true;
    try {
        std::random_device random;
        scid_ = (static_cast<std::uint32_t>(random()) ^ GetCurrentProcessId());
        if (!scid_) scid_ = 1;
        const auto id = hex(scid_);
        remote_ = "/data/local/tmp/wvd_scrcpy_" + id;
        auto name = std::string("localabstract:scrcpy_") + id;
        SOCKET reservation = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (reservation == INVALID_SOCKET) throw std::runtime_error("SCRCPY_PORT_CREATE_FAILED");
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        int length = sizeof(address);
        if (bind(reservation, reinterpret_cast<sockaddr *>(&address), length) ||
            getsockname(reservation, reinterpret_cast<sockaddr *>(&address), &length)) {
            closesocket(reservation); throw std::runtime_error("SCRCPY_PORT_RESERVE_FAILED");
        }
        port_ = ntohs(address.sin_port);
        closesocket(reservation);
        const auto local = std::string("tcp:") + std::to_string(port_);
        require_adb(adb_.run({L"push", server_.wstring(),
                              std::wstring(remote_.begin(), remote_.end())}, 20000ms, stop),
                    "SCRCPY_PUSH_FAILED");
        uploaded_ = true;
        require_adb(adb_.run({L"forward", std::wstring(local.begin(), local.end()),
                              std::wstring(name.begin(), name.end())}, 5000ms, stop),
                    "SCRCPY_FORWARD_FAILED");
        forward_ = true;
        std::wstring command = quote(adb_.executable().wstring()) + L" -s " +
            quote(std::wstring(adb_.serial().begin(), adb_.serial().end())) +
            L" shell CLASSPATH=" + std::wstring(remote_.begin(), remote_.end()) +
            L" app_process / com.genymobile.scrcpy.Server 3.3.4 scid=" +
            std::wstring(id.begin(), id.end()) +
            L" log_level=error video=false audio=false control=true tunnel_forward=true"
            L" send_dummy_byte=true send_device_meta=true";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(adb_.executable().c_str(), command.data(), nullptr, nullptr,
                            FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
            throw std::runtime_error("SCRCPY_LAUNCH_FAILED");
        child_ = process.hProcess;
        CloseHandle(process.hThread);
        while (std::chrono::steady_clock::now() < deadline && !stop.stop_requested()) {
            if (WaitForSingleObject(child_, 0) == WAIT_OBJECT_0)
                throw std::runtime_error("SCRCPY_SERVER_EXITED");
            SOCKET candidate = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (candidate == INVALID_SOCKET) throw std::runtime_error("SCRCPY_SOCKET_FAILED");
            address.sin_port = htons(port_);
            if (::connect(candidate, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0) {
                socket_ = candidate;
                break;
            }
            closesocket(candidate);
            std::this_thread::sleep_for(50ms);
        }
        if (!connected()) throw std::runtime_error("SCRCPY_CONNECT_TIMEOUT");
        DWORD socket_timeout = 5000;
        setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char *>(&socket_timeout), sizeof(socket_timeout));
        setsockopt(socket_, SOL_SOCKET, SO_SNDTIMEO,
                   reinterpret_cast<const char *>(&socket_timeout), sizeof(socket_timeout));
        char dummy{};
        if (recv(socket_, &dummy, 1, MSG_WAITALL) != 1 || dummy != 0)
            throw std::runtime_error("SCRCPY_DUMMY_BYTE_INVALID");
        std::array<char, 64> metadata{};
        std::size_t offset{};
        while (offset < metadata.size()) {
            const int count = recv(socket_, metadata.data() + offset,
                                   static_cast<int>(metadata.size() - offset), 0);
            if (count <= 0) throw std::runtime_error("SCRCPY_METADATA_MISSING");
            offset += count;
        }
        if (metadata.front() == 0) throw std::runtime_error("SCRCPY_METADATA_INVALID");
    } catch (...) { close(); throw; }
}

void ScrcpyControlClient::send_bytes(const std::vector<std::uint8_t> &bytes,
                                      std::stop_token stop) {
    std::size_t offset{};
    while (offset < bytes.size()) {
        if (stop.stop_requested()) {
            if (offset) unresolved_ = true;
            throw std::runtime_error("SCRCPY_CANCELLED");
        }
        const int count = send(socket_, reinterpret_cast<const char *>(bytes.data() + offset),
                               static_cast<int>(bytes.size() - offset), 0);
        if (count <= 0) { unresolved_ = true; throw std::runtime_error("SCRCPY_SEND_UNRESOLVED"); }
        offset += count;
    }
}

void ScrcpyControlClient::submit(const contracts::Command &command, int width, int height,
                                  std::stop_token stop) {
    if (!connected()) throw std::runtime_error("SCRCPY_NOT_CONNECTED");
    const auto messages = scrcpy::encode(command, width, height);
    bool sent{};
    try {
        for (std::size_t index = 0; index < messages.size(); ++index) {
            const auto &message = messages[index];
            if (stop.stop_requested()) throw std::runtime_error("SCRCPY_CANCELLED");
            send_bytes(message, stop);
            sent = true;
            if (message[0] == 2) {
                if (message[1] == 1) held_ = false;
                else {
                    held_ = true;
                    held_x_ = static_cast<int>((static_cast<std::uint32_t>(message[10]) << 24) |
                        (static_cast<std::uint32_t>(message[11]) << 16) |
                        (static_cast<std::uint32_t>(message[12]) << 8) | message[13]);
                    held_y_ = static_cast<int>((static_cast<std::uint32_t>(message[14]) << 24) |
                        (static_cast<std::uint32_t>(message[15]) << 16) |
                        (static_cast<std::uint32_t>(message[16]) << 8) | message[17]);
                    held_width_ = width; held_height_ = height;
                }
            }
            if (command.kind == contracts::ActionKind::Swipe && index + 1 < messages.size()) {
                const auto delay = std::chrono::milliseconds{
                    command.duration / static_cast<int>(messages.size() - 1)};
                const auto until = std::chrono::steady_clock::now() + delay;
                while (!stop.stop_requested() && std::chrono::steady_clock::now() < until)
                    std::this_thread::sleep_for(5ms);
            }
        }
    } catch (...) {
        if (sent) unresolved_ = true;
        throw;
    }
}

void ScrcpyControlClient::close() noexcept {
    if (socket_ != INVALID_SOCKET) {
        if (held_) {
            try {
                const auto release = scrcpy::touch(1, UINT64_MAX - 1, held_x_, held_y_,
                                                    held_width_, held_height_, false);
                if (send(socket_, reinterpret_cast<const char *>(release.data()),
                         static_cast<int>(release.size()), 0) !=
                    static_cast<int>(release.size())) unresolved_ = true;
            } catch (...) { unresolved_ = true; }
        }
        shutdown(socket_, SD_BOTH);
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }
    held_ = false;
    if (child_) {
        if (WaitForSingleObject(child_, 1000) != WAIT_OBJECT_0) {
            unresolved_ = true;
            TerminateProcess(child_, 1); // Only this owned adb client; never the global server.
            if (WaitForSingleObject(child_, 1000) != WAIT_OBJECT_0) unresolved_ = true;
        }
        CloseHandle(child_);
        child_ = nullptr;
    }
    if (forward_) {
        try {
            const auto local = std::string("tcp:") + std::to_string(port_);
            adb_.run({L"forward", L"--remove", std::wstring(local.begin(), local.end())},
                     3000ms);
        } catch (...) { unresolved_ = true; }
        forward_ = false;
    }
    if (uploaded_) {
        try { adb_.shell_fixed("rm " + remote_, 3000ms); }
        catch (...) { unresolved_ = true; }
        uploaded_ = false;
    }
    if (winsock_) { WSACleanup(); winsock_ = false; }
}
} // namespace wvd::devices
