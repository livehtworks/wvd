#pragma once
#include "platform/windows/path_utf8.hpp"
#include "platform/windows/runtime_files.hpp"
#include "json.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <stdexcept>
#include <atomic>

namespace wvd::app {
// 进程级所有权，不是游戏会话锁。先拿锁再构造 Application，避免重复启动修改同一配置/记录。
class ServiceInstance {
public:
    explicit ServiceInstance(const std::filesystem::path &data_root)
        : root_(std::filesystem::absolute(data_root)), id_(platform::unique_id()) {
        std::filesystem::create_directories(root_);
        handle_ = CreateFileW((root_ / "service.lock").c_str(), GENERIC_READ | GENERIC_WRITE,
            0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE)
            throw std::runtime_error("SERVICE_DATA_ROOT_LOCKED: " + std::to_string(GetLastError()));
    }
    ~ServiceInstance() { CloseHandle(handle_); }
    ServiceInstance(const ServiceInstance &) = delete;
    ServiceInstance &operator=(const ServiceInstance &) = delete;
    nlohmann::json identity() const {
        auto result = identity_;
        result["state"] = stopping_ ? "stopping" : "running";
        return result;
    }
    const std::string &id() const { return id_; }
    void mark_stopping() { stopping_ = true; }
    void publish(unsigned short port) {
        wchar_t executable[32768]{};
        const auto length = GetModuleFileNameW(nullptr, executable, 32768);
        if (!length || length >= 32768) throw std::runtime_error("SERVICE_EXECUTABLE_PATH_FAILED");
        FILETIME created{}, exited{}, kernel{}, user{};
        if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
            throw std::runtime_error("SERVICE_PROCESS_TIME_FAILED");
        const auto creation_time = (static_cast<unsigned long long>(created.dwHighDateTime) << 32) |
                                   created.dwLowDateTime;
        identity_ = {{"service", "automationd"}, {"instance_id", id_},
            {"pid", GetCurrentProcessId()}, {"port", port},
            {"process_start_filetime", std::to_string(creation_time)},
            {"data_root", platform::utf8(std::filesystem::canonical(root_))},
            {"executable", platform::utf8(std::filesystem::path(executable))}};
        // 仅供发现进程；不是配置权威。崩溃留下的记录必须再与 HTTP 实例身份核对。
        platform::atomic_write(root_ / "service.json", identity_.dump(2), true);
    }
private:
    std::filesystem::path root_;
    std::string id_;
    HANDLE handle_{INVALID_HANDLE_VALUE};
    nlohmann::json identity_;
    std::atomic<bool> stopping_{false};
};
} // namespace wvd::app
