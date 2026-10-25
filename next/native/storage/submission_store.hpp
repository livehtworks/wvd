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
#include <limits>
#include <cctype>

namespace wvd::storage {
struct SubmissionStoreTestAccess;
// Damaged or unfinished history stops new writes. Trusted old IDs remain
// readable through the same bounded, identity-checked path.
class SubmissionStore {
    using J = nlohmann::json;
    struct Limits {
        std::uintmax_t record_bytes{1024 * 1024};
        std::uintmax_t total_bytes{256 * 1024 * 1024};
        std::size_t committed_count{65536};
        std::size_t scan_entries{65537};
    } limits_;
    std::filesystem::path root_;
    mutable std::mutex mutex_;
    std::uintmax_t bytes_{}, pending_bytes_{}, unknown_bytes_{};
    std::size_t committed_count_{}, pending_count_{}, pending_intents_{}, unknown_entries_{}, scanned_{};
    bool read_only_{}, capacity_known_{true}, scan_complete_{true};
    std::string reason_, last_write_error_;
    J issues_ = J::array();
    struct DirectoryHandle {
        HANDLE value{};
        ~DirectoryHandle() noexcept { if (value) CloseHandle(value); }
    };
    std::vector<std::unique_ptr<DirectoryHandle>> directories_;
    friend struct SubmissionStoreTestAccess;
    static std::string key(const std::string &id) {
        if (id.empty() || id.size() > 256) throw std::runtime_error("REQUEST_ID_INVALID");
        return platform::bytes_sha256({reinterpret_cast<const std::uint8_t *>(id.data()), id.size()});
    }
    static bool hexadecimal(const std::string &text, std::size_t start, std::size_t length) {
        if (start > text.size() || length > text.size() - start) return false;
        for (std::size_t i = start; i < start + length; ++i)
            if (!std::isxdigit(static_cast<unsigned char>(text[i]))) return false;
        return true;
    }
    static bool committed_name(const std::string &name) {
        return name.size() == 69 && name.substr(64) == ".json" && hexadecimal(name, 0, 64) &&
            name.substr(0, 64).find_first_of("ABCDEF") == std::string::npos;
    }
    static bool temporary_name(const std::string &name) {
        if (name.size() != 110 || !committed_name(name.substr(0, 69)) ||
            name[69] != '.' || name.substr(106) != ".tmp") return false;
        for (std::size_t i = 0; i < 36; ++i) {
            const bool separator = i == 8 || i == 13 || i == 18 || i == 23;
            if (separator ? name[70 + i] != '-' : !hexadecimal(name, 70 + i, 1)) return false;
        }
        return true;
    }
    static std::string path_text(const std::filesystem::path &path) {
        const auto text = path.generic_u8string();
        return {text.begin(), text.end()};
    }
    static bool pending_receipt(const J &receipt) {
        const auto state = receipt.value("state", std::string{});
        return state == "preparing" || state == "unknown";
    }
    void block(const std::string &reason, const std::filesystem::path &path = {}) {
        read_only_ = true;
        if (reason_.empty()) reason_ = reason;
        if (issues_.size() < 16) issues_.push_back({{"reason", reason}, {"path", path_text(path)}});
    }
    bool add(std::uintmax_t &value, std::uintmax_t amount) {
        if (amount > (std::numeric_limits<std::uintmax_t>::max)() - value) {
            capacity_known_ = false; block("REQUEST_HISTORY_SIZE_OVERFLOW"); return false;
        }
        value += amount;
        return true;
    }
    J read(const std::filesystem::path &path) const {
        HANDLE h = CreateFileW(platform::extended_path(path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (h == INVALID_HANDLE_VALUE) throw std::runtime_error("REQUEST_HISTORY_READ_FAILED");
        struct Close { HANDLE h; ~Close() { CloseHandle(h); } } close{h};
        BY_HANDLE_FILE_INFORMATION info{}; LARGE_INTEGER size{};
        if (!GetFileInformationByHandle(h, &info) ||
            (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) ||
            GetFileType(h) != FILE_TYPE_DISK || !GetFileSizeEx(h, &size) ||
            size.QuadPart < 0 || static_cast<std::uintmax_t>(size.QuadPart) > limits_.record_bytes)
            throw std::runtime_error("REQUEST_HISTORY_INVALID");
        std::string text(static_cast<std::size_t>(size.QuadPart), '\0'); DWORD got{};
        if (!ReadFile(h, text.data(), static_cast<DWORD>(text.size()), &got, nullptr) || got != text.size())
            throw std::runtime_error("REQUEST_HISTORY_READ_FAILED");
        return J::parse(text);
    }
    void require_writable_locked() const {
        if (read_only_) throw std::runtime_error("REQUEST_HISTORY_READ_ONLY:" + reason_);
    }
    void scan() {
        bytes_ = pending_bytes_ = unknown_bytes_ = 0;
        committed_count_ = pending_count_ = pending_intents_ = unknown_entries_ = scanned_ = 0;
        read_only_ = false; capacity_known_ = scan_complete_ = true; reason_.clear(); issues_ = J::array();
        std::error_code iterate_error;
        std::filesystem::directory_iterator it(root_, iterate_error), end;
        if (iterate_error) { capacity_known_ = scan_complete_ = false; block("REQUEST_HISTORY_SCAN_FAILED", root_); return; }
        for (; it != end; it.increment(iterate_error)) {
            if (iterate_error) break;
            if (scanned_ == limits_.scan_entries) {
                capacity_known_ = scan_complete_ = false; block("REQUEST_HISTORY_SCAN_LIMIT", root_); break;
            }
            ++scanned_;
            const auto path = it->path();
            std::string name;
            try { name = path_text(path.filename()); }
            catch (const std::exception &) {
                ++unknown_entries_; capacity_known_ = false; block("REQUEST_HISTORY_ENTRY_NAME_UNVERIFIED"); continue;
            }
            HANDLE h = CreateFileW(platform::extended_path(path).c_str(), FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
            BY_HANDLE_FILE_INFORMATION info{}; LARGE_INTEGER size{};
            const bool valid = h != INVALID_HANDLE_VALUE && GetFileInformationByHandle(h, &info) &&
                !(info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) &&
                GetFileType(h) == FILE_TYPE_DISK && GetFileSizeEx(h, &size) && size.QuadPart >= 0;
            if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
            if (!valid) {
                ++unknown_entries_; capacity_known_ = false; block("REQUEST_HISTORY_ENTRY_UNVERIFIED", path); continue;
            }
            const auto length = static_cast<std::uintmax_t>(size.QuadPart);
            if (committed_name(name)) {
                ++committed_count_; add(bytes_, length);
                if (committed_count_ > limits_.committed_count) block("REQUEST_HISTORY_COUNT_LIMIT", path);
                if (length > limits_.record_bytes) { block("REQUEST_HISTORY_RECORD_LIMIT", path); continue; }
                // The total parse work is byte-bounded even for legacy oversized histories.
                if (bytes_ > limits_.total_bytes) { block("REQUEST_HISTORY_STORAGE_LIMIT", path); continue; }
                try {
                    const auto row = read(path);
                    const auto id = row.at("request_id").get<std::string>();
                    if (key(id) + ".json" != name || row.at("schema") != 1 ||
                        !row.at("intent").is_object() || !row.at("receipt").is_object())
                        throw std::runtime_error("REQUEST_HISTORY_INVALID");
                    if (pending_receipt(row.at("receipt"))) {
                        ++pending_intents_; block("REQUEST_HISTORY_PENDING_INTENT", path);
                    }
                } catch (const std::exception &) { block("REQUEST_HISTORY_RECORD_INVALID", path); }
            } else if (temporary_name(name)) {
                ++pending_count_; add(pending_bytes_, length); block("REQUEST_HISTORY_PENDING_ARTIFACT", path);
            } else {
                ++unknown_entries_; add(unknown_bytes_, length); block("REQUEST_HISTORY_UNKNOWN_ENTRY", path);
            }
        }
        if (iterate_error) { capacity_known_ = scan_complete_ = false; block("REQUEST_HISTORY_SCAN_FAILED", root_); }
        auto occupied = bytes_;
        if (!add(occupied, pending_bytes_) || !add(occupied, unknown_bytes_) || occupied > limits_.total_bytes)
            block("REQUEST_HISTORY_STORAGE_LIMIT", root_);
    }
    SubmissionStore(std::filesystem::path root, Limits limits) : limits_(limits), root_(std::move(root)) {
        if (!root_.is_absolute()) throw std::runtime_error("REQUEST_HISTORY_ABSOLUTE_ROOT_REQUIRED");
        if (!limits_.record_bytes || limits_.record_bytes > MAXDWORD ||
            limits_.total_bytes < limits_.record_bytes || !limits_.committed_count ||
            !limits_.scan_entries) throw std::runtime_error("REQUEST_HISTORY_POLICY_INVALID");
        if (root_.relative_path().empty()) throw std::runtime_error("REQUEST_HISTORY_ROOT_INVALID");
        for (const auto &part : root_.relative_path())
            if (part == "." || part == "..") throw std::runtime_error("REQUEST_HISTORY_ROOT_INVALID");
        // Validate/pin each ancestor before creating the next component.
        auto path = root_.root_path();
        for (const auto &part : root_.relative_path()) {
            path /= part;
            if (!std::filesystem::exists(path)) std::filesystem::create_directory(path);
            auto owner = std::make_unique<DirectoryHandle>();
            const auto handle = CreateFileW(platform::extended_path(path).c_str(), FILE_READ_ATTRIBUTES,
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
        scan();
    }
  public:
    explicit SubmissionStore(std::filesystem::path root) : SubmissionStore(std::move(root), Limits{}) {}
    void require_writable() const { std::lock_guard lock(mutex_); require_writable_locked(); }
    J status() const {
        std::lock_guard lock(mutex_);
        return {{"read_only", read_only_}, {"reason", reason_}, {"last_write_error", last_write_error_},
            {"committed_count", committed_count_}, {"committed_bytes", bytes_},
            {"pending_count", pending_count_}, {"pending_bytes", pending_bytes_},
            {"pending_intent_count", pending_intents_}, {"unknown_entries", unknown_entries_},
            {"unknown_bytes", capacity_known_ ? J(unknown_bytes_) : J(nullptr)},
            {"capacity_known", capacity_known_}, {"scan_complete", scan_complete_}, {"scanned_entries", scanned_},
            {"count_limit", limits_.committed_count}, {"record_limit", limits_.record_bytes},
            {"total_limit", limits_.total_bytes}, {"scan_limit", limits_.scan_entries}, {"issues", issues_}};
    }
    std::optional<J> replay(const std::string &id, const J &intent) {
        std::lock_guard lock(mutex_);
        const auto path = root_ / (key(id) + ".json");
        if (!std::filesystem::exists(path)) return std::nullopt;
        const auto row = read(path);
        if (row.at("request_id") != id || row.at("intent") != intent || row.at("schema") != 1 ||
            !row.at("receipt").is_object())
            throw std::runtime_error("IDEMPOTENCY_CONFLICT");
        auto result = row.at("receipt"); result["accepted"] = true; result["replayed"] = true;
        return result;
    }
    void save(const std::string &id, const J &intent, const J &receipt, bool replace) {
        std::lock_guard lock(mutex_);
        require_writable_locked();
        if (!intent.is_object() || !receipt.is_object() ||
            (receipt.contains("state") && !receipt.at("state").is_string()))
            throw std::runtime_error("REQUEST_HISTORY_RECORD_INVALID");
        const auto path = root_ / (key(id) + ".json");
        const bool exists = std::filesystem::exists(path);
        if (exists != replace) throw std::runtime_error("REQUEST_HISTORY_CONFLICT");
        if (!replace && committed_count_ >= limits_.committed_count)
            throw std::runtime_error("REQUEST_HISTORY_COUNT_LIMIT");
        std::uintmax_t previous{};
        bool was_pending{};
        if (exists) {
            const auto old = read(path);
            if (old.at("request_id") != id || old.at("intent") != intent)
                throw std::runtime_error("IDEMPOTENCY_CONFLICT");
            was_pending = pending_receipt(old.at("receipt"));
            previous = std::filesystem::file_size(path);
            if (previous > bytes_) { capacity_known_ = false; block("REQUEST_HISTORY_ACCOUNTING_INVALID", path); require_writable_locked(); }
            if (was_pending && !pending_intents_) {
                capacity_known_ = false; block("REQUEST_HISTORY_ACCOUNTING_INVALID", path); require_writable_locked();
            }
        }
        const auto text = J{{"schema", 1}, {"request_id", id}, {"intent", intent}, {"receipt", receipt}}.dump();
        if (text.size() > limits_.record_bytes || text.size() > limits_.total_bytes ||
            bytes_ - previous > limits_.total_bytes - text.size())
            throw std::runtime_error("REQUEST_HISTORY_STORAGE_LIMIT");
        const auto needed = bytes_ - previous + text.size();
        if (!replace && needed > limits_.total_bytes - limits_.record_bytes)
            throw std::runtime_error("REQUEST_HISTORY_STORAGE_LIMIT");
        try { platform::atomic_write(path, text, replace); }
        catch (const std::exception &error) {
            last_write_error_ = error.what();
            try { scan(); } catch (...) { capacity_known_ = scan_complete_ = false; }
            block("REQUEST_HISTORY_WRITE_FAILED", path);
            throw;
        }
        bytes_ = needed;
        if (!replace) ++committed_count_;
        if (was_pending && pending_intents_) --pending_intents_;
        if (pending_receipt(receipt)) ++pending_intents_;
        if (receipt.value("state", std::string{}) == "unknown") block("REQUEST_HISTORY_PENDING_INTENT", path);
    }
};
}
