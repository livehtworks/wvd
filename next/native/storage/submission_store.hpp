#pragma once
#include "platform/windows/runtime_files.hpp"
#include "platform/windows/file_digest.hpp"
#include <windows.h>
#include <json.hpp>
#include <filesystem>
#include <mutex>
#include <optional>
#include <memory>
#include <vector>

namespace wvd::storage {
// Accepted intents are durable facts, not an evictable in-memory deduplication cache.
class SubmissionStore {
    using J = nlohmann::json;
    std::filesystem::path root_;
    std::mutex mutex_;
    std::uintmax_t bytes_{};
    struct DirectoryHandle {
        HANDLE value{};
        ~DirectoryHandle() noexcept { if (value) CloseHandle(value); }
    };
    std::vector<std::unique_ptr<DirectoryHandle>> directories_;
    static constexpr std::uintmax_t record_limit = 1024 * 1024, total_limit = 256 * 1024 * 1024;
    static std::string key(const std::string &id) {
        if (id.empty() || id.size() > 256) throw std::runtime_error("REQUEST_ID_INVALID");
        return platform::bytes_sha256({reinterpret_cast<const std::uint8_t *>(id.data()), id.size()});
    }
    J read(const std::filesystem::path &path) {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (h == INVALID_HANDLE_VALUE) throw std::runtime_error("REQUEST_HISTORY_READ_FAILED");
        struct Close { HANDLE h; ~Close() { CloseHandle(h); } } close{h};
        BY_HANDLE_FILE_INFORMATION info{}; LARGE_INTEGER size{};
        if (!GetFileInformationByHandle(h, &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
            !GetFileSizeEx(h, &size) || size.QuadPart < 0 || size.QuadPart > record_limit)
            throw std::runtime_error("REQUEST_HISTORY_INVALID");
        std::string text(static_cast<std::size_t>(size.QuadPart), '\0'); DWORD got{};
        if (!ReadFile(h, text.data(), static_cast<DWORD>(text.size()), &got, nullptr) || got != text.size())
            throw std::runtime_error("REQUEST_HISTORY_READ_FAILED");
        return J::parse(text);
    }
  public:
    explicit SubmissionStore(std::filesystem::path root) : root_(std::move(root)) {
        if (!root_.is_absolute()) throw std::runtime_error("REQUEST_HISTORY_ABSOLUTE_ROOT_REQUIRED");
        std::filesystem::create_directories(root_);
        auto path = root_.root_path();
        for (const auto &part : root_.relative_path()) {
            if (part == "." || part == "..") throw std::runtime_error("REQUEST_HISTORY_ROOT_INVALID");
            path /= part;
            auto owner = std::make_unique<DirectoryHandle>();
            const auto handle = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error("REQUEST_HISTORY_DIRECTORY_OPEN_FAILED");
            owner->value = handle;
            BY_HANDLE_FILE_INFORMATION info{};
            if (!GetFileInformationByHandle(handle, &info) ||
                (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
                !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                throw std::runtime_error("REQUEST_HISTORY_REPARSE_POINT");
            directories_.push_back(std::move(owner));
        }
        std::size_t count{};
        for (const auto &file : std::filesystem::directory_iterator(root_)) {
            if (++count > 65536 || !file.is_regular_file() || file.path().extension() != ".json" ||
                (GetFileAttributesW(file.path().c_str()) & FILE_ATTRIBUTE_REPARSE_POINT))
                throw std::runtime_error("REQUEST_HISTORY_INVALID");
            bytes_ += file.file_size();
            if (bytes_ > total_limit) throw std::runtime_error("REQUEST_HISTORY_STORAGE_LIMIT");
        }
    }
    std::optional<J> replay(const std::string &id, const J &intent) {
        std::lock_guard lock(mutex_);
        const auto path = root_ / (key(id) + ".json");
        if (!std::filesystem::exists(path)) return std::nullopt;
        const auto row = read(path);
        if (row.at("request_id") != id || row.at("intent") != intent)
            throw std::runtime_error("IDEMPOTENCY_CONFLICT");
        auto result = row.at("receipt"); result["accepted"] = true; result["replayed"] = true;
        return result;
    }
    void save(const std::string &id, const J &intent, const J &receipt, bool replace) {
        std::lock_guard lock(mutex_);
        const auto path = root_ / (key(id) + ".json");
        const bool exists = std::filesystem::exists(path);
        if (exists != replace) throw std::runtime_error("REQUEST_HISTORY_CONFLICT");
        std::uintmax_t previous{};
        if (exists) {
            const auto old = read(path);
            if (old.at("request_id") != id || old.at("intent") != intent)
                throw std::runtime_error("IDEMPOTENCY_CONFLICT");
            previous = std::filesystem::file_size(path);
        }
        const auto text = J{{"schema", 1}, {"request_id", id}, {"intent", intent}, {"receipt", receipt}}.dump();
        const auto needed = bytes_ - previous + text.size();
        if (text.size() > record_limit || needed > total_limit ||
            (!replace && needed > total_limit - record_limit))
            throw std::runtime_error("REQUEST_HISTORY_STORAGE_LIMIT");
        platform::atomic_write(path, text, replace);
        bytes_ = bytes_ - previous + text.size();
    }
};
}
