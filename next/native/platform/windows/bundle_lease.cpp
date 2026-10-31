#include "bundle_lease.hpp"
#include "file_digest.hpp"
#include "runtime_files.hpp"
#include "path_utf8.hpp"
#include <algorithm>
#include <cwctype>
#include <mutex>
#include <set>
#include <windows.h>
#include <winternl.h>
#include <cstring>
#include <limits>
#include <json.hpp>

namespace wvd::platform {
namespace {
void require(bool condition, const char *code) {
    if (!condition)
        throw std::runtime_error(code);
}
struct Held {
    HANDLE handle{INVALID_HANDLE_VALUE};
    std::filesystem::path path;
    BY_HANDLE_FILE_INFORMATION info{};
    mutable std::mutex content_mutex;
    std::vector<std::uint8_t> content;
    bool content_loaded{};
    explicit Held(std::filesystem::path p, bool directory, DWORD extra_access = 0) : path(std::move(p)) {
        const auto sharing = FILE_SHARE_READ | (extra_access ? FILE_SHARE_WRITE : 0);
        handle = CreateFileW(extended_path(path).c_str(), GENERIC_READ | extra_access, sharing, nullptr, OPEN_EXISTING,
                             directory ? FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT
                                       : FILE_FLAG_OPEN_REPARSE_POINT,
                             nullptr);
        require(handle != INVALID_HANDLE_VALUE, "INTEGRITY_SHARING_CONFLICT");
        if (!GetFileInformationByHandle(handle, &info) ||
            (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            CloseHandle(handle);
            handle = INVALID_HANDLE_VALUE;
            throw std::runtime_error("INTEGRITY_FILE_IDENTITY");
        }
    }
    ~Held() {
        if (handle != INVALID_HANDLE_VALUE)
            CloseHandle(handle);
    }
    Held(const Held &) = delete;
    std::uint64_t checked_size() const {
        const auto length = (std::uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
        require(length <= 256 * 1024 * 1024, "INTEGRITY_FILE_TOO_LARGE");
        return length;
    }
    void verify_streamed(const std::string &expected_hash,
                         const std::function<void()> &check_cancel = {}) {
        std::lock_guard lock(content_mutex);
        const auto length = checked_size();
        verify_identity();
        const auto digest = handle_sha256(handle, length, check_cancel);
        verify_identity();
        require(digest == expected_hash, "RESOURCE_HASH_MISMATCH");
    }
    const std::vector<std::uint8_t> &verified_bytes(const std::string &expected_hash,
                                                  const std::function<void()> &check_cancel = {}) {
        std::lock_guard lock(content_mutex);
        if (content_loaded) return content;
        const auto length = checked_size();
        verify_identity();
        // Stream verification leaves this handle at EOF. Every first load or
        // failed-load retry must seek on the SAME frozen handle before reading.
        require(SetFilePointerEx(handle, LARGE_INTEGER{}, nullptr, FILE_BEGIN),
                "INTEGRITY_SEEK_FAILED");
        std::vector<std::uint8_t> loaded(static_cast<std::size_t>(length));
        for (std::size_t offset = 0; offset < loaded.size();) {
            if (check_cancel) check_cancel();
            DWORD count{};
            require(
                ReadFile(handle, loaded.data() + offset,
                         static_cast<DWORD>(std::min<std::size_t>(65536, loaded.size() - offset)),
                         &count, nullptr) &&
                    count,
                "INTEGRITY_READ_FAILED");
            offset += count;
        }
        verify_identity();
        require(bytes_sha256(loaded) == expected_hash, "RESOURCE_HASH_MISMATCH");
        // Publish only a fully verified buffer. On any failure, the local buffer
        // is released and the next caller retries from offset 0 under this mutex.
        content.swap(loaded);
        content_loaded = true;
        return content;
    }
    void verify_identity() const {
        auto attributes = GetFileAttributesW(extended_path(path).c_str());
        require(attributes != INVALID_FILE_ATTRIBUTES &&
                    !(attributes & FILE_ATTRIBUTE_REPARSE_POINT),
                "INTEGRITY_PATH_CHANGED");
        HANDLE current =
            CreateFileW(extended_path(path).c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        require(current != INVALID_HANDLE_VALUE, "INTEGRITY_PATH_CHANGED");
        BY_HANDLE_FILE_INFORMATION now{};
        bool ok = GetFileInformationByHandle(current, &now);
        CloseHandle(current);
        require(ok && info.dwVolumeSerialNumber == now.dwVolumeSerialNumber &&
                    info.nFileIndexHigh == now.nFileIndexHigh &&
                    info.nFileIndexLow == now.nFileIndexLow,
                "INTEGRITY_PATH_CHANGED");
    }
};
std::wstring fold(const std::filesystem::path &p) {
    auto value = p.generic_wstring();
    std::transform(value.begin(), value.end(), value.begin(),
                   [](wchar_t c) { return std::towlower(c); });
    return value;
}
std::vector<std::filesystem::path> ancestors(const std::filesystem::path &path) {
    // Traverse a DOS/UNC path, not the extended namespace: MSVC otherwise
    // treats \\?\D: as a parent, which is not a valid Win32 directory.
    auto text = path.native();
    if (text.starts_with(L"\\\\?\\UNC\\")) text = L"\\\\" + text.substr(8);
    else if (text.starts_with(L"\\\\?\\")) text = text.substr(4);
    std::vector<std::filesystem::path> result;
    for (auto p = std::filesystem::absolute(text).lexically_normal(); !p.empty();) {
        result.push_back(extended_path(p));
        auto parent = p.parent_path();
        if (parent == p) break;
        p = std::move(parent);
    }
    return result;
}
std::string exception_text(std::exception_ptr error) {
    try { if (error) std::rethrow_exception(error); }
    catch (const std::exception &failure) { return failure.what(); }
    catch (...) { return "NON_STANDARD_EXCEPTION"; }
    return "UNKNOWN_PREPARATION_FAILURE";
}
bool same_file(const BY_HANDLE_FILE_INFORMATION &a, const BY_HANDLE_FILE_INFORMATION &b) {
    return a.dwVolumeSerialNumber == b.dwVolumeSerialNumber &&
        a.nFileIndexHigh == b.nFileIndexHigh && a.nFileIndexLow == b.nFileIndexLow;
}
// Delete only through handles opened without delete sharing. The parent stays
// pinned until each child is removed, so an identity check is not separated
// from deletion by a path-swap window.
void erase_owned_tree(const std::filesystem::path &path,
                      const BY_HANDLE_FILE_INFORMATION *expected = nullptr) {
    struct Close {
        HANDLE value{INVALID_HANDLE_VALUE};
        ~Close() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    } handle;
    handle.value = CreateFileW(extended_path(path).c_str(), FILE_READ_ATTRIBUTES | DELETE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle.value == INVALID_HANDLE_VALUE)
        throw std::runtime_error("OWNED_DIRECTORY_DELETE_OPEN_FAILED:" + std::to_string(GetLastError()));
    BY_HANDLE_FILE_INFORMATION info{};
    require(GetFileInformationByHandle(handle.value, &info) &&
        !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT), "OWNED_DIRECTORY_DELETE_IDENTITY_FAILED");
    require(!expected || same_file(*expected, info), "OWNED_DIRECTORY_DELETE_IDENTITY_CHANGED");
    if (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        for (const auto &child : std::filesystem::directory_iterator(extended_path(path))) erase_owned_tree(child.path());
    FILE_DISPOSITION_INFO disposition{TRUE};
    if (!SetFileInformationByHandle(handle.value, FileDispositionInfo, &disposition, sizeof(disposition)))
        throw std::runtime_error("OWNED_DIRECTORY_DELETE_FAILED:" + std::to_string(GetLastError()));
}
} // namespace
DirectoryCleanupPending::DirectoryCleanupPending(std::string primary, std::string cleanup,
    std::filesystem::path path)
    : std::runtime_error(primary + ";cleanup_error=" + cleanup + ";cleanup_pending=" + utf8(path)),
      primary_(std::move(primary)), cleanup_(std::move(cleanup)), path_(std::move(path)) {}
void OwnedBundleDirectory::Receipt::require_clean_acceptance() const {
    if (accepted && cleanup_pending)
        throw DirectoryCleanupPending("OWNED_DIRECTORY_ACCEPT_INDEX_FAILED",
            cleanup_error.empty() ? "OWNED_DIRECTORY_PREPARATION_MARKER_PENDING" : cleanup_error, path);
}
thread_local std::function<void(const char *)> OwnedBundleDirectory::fault_;
struct OwnedBundleDirectory::Impl {
    BY_HANDLE_FILE_INFORMATION identity{};
    HANDLE pending{INVALID_HANDLE_VALUE};
    BY_HANDLE_FILE_INFORMATION pending_identity{};
    bool pending_identity_confirmed{};
    ~Impl() { if (pending != INVALID_HANDLE_VALUE) CloseHandle(pending); }
};
OwnedBundleDirectory::OwnedBundleDirectory(std::filesystem::path path)
    : impl_(std::make_unique<Impl>()), receipt_(std::make_shared<Receipt>()) {
    require(path.is_absolute(), "OWNED_DIRECTORY_PATH_INVALID");
    receipt_->path = std::filesystem::absolute(path).lexically_normal().make_preferred();
}
void OwnedBundleDirectory::require_no_pending(const std::filesystem::path &parent) {
    if (!std::filesystem::exists(extended_path(parent))) return;
    std::vector<std::unique_ptr<Held>> ancestors_held;
    for (const auto &ancestor : ancestors(parent)) ancestors_held.push_back(std::make_unique<Held>(ancestor, true));
    std::size_t examined{};
    for (const auto &entry : std::filesystem::directory_iterator(extended_path(parent))) {
        require(++examined <= 65536, "PREPARATION_DIRECTORY_SCAN_LIMIT");
        const auto name = entry.path().filename().native();
        if (name.find(L".cleanup-pending-") != std::wstring::npos ||
            name.find(L".staging-") != std::wstring::npos ||
            name.find(L".tmp-") != std::wstring::npos || name.starts_with(L"portraits-tmp-"))
            throw std::runtime_error("PREPARATION_CLEANUP_PENDING:" + utf8(entry.path()));
    }
}
std::unique_ptr<OwnedBundleDirectory> OwnedBundleDirectory::create(const std::filesystem::path &path) {
    auto owner = std::unique_ptr<OwnedBundleDirectory>(new OwnedBundleDirectory(path));
    require_no_pending(path.parent_path());
    std::vector<std::unique_ptr<Held>> parents;
    for (const auto &parent : ancestors(path.parent_path())) parents.push_back(std::make_unique<Held>(parent, true));
    try {
        require(std::filesystem::create_directory(extended_path(owner->receipt_->path)), "OWNED_DIRECTORY_ALREADY_EXISTS");
        owner->receipt_->created = true;
        if (fault_) fault_("before_identity");
        Held identity(owner->receipt_->path, true);
        owner->impl_->identity = identity.info;
        owner->receipt_->identity_confirmed = true;
        if (fault_) fault_("after_identity");
    } catch (...) {
        // Do not retain our parent locks while rolling back: they protect the
        // creation window but deletion needs its own identity-checked handles.
        parents.clear();
        owner->rethrow_with_cleanup(std::current_exception());
    }
    return owner;
}
void OwnedBundleDirectory::note_cleanup_failure(const std::string &error) noexcept {
    receipt_->cleanup_pending = true;
    try {
        receipt_->cleanup_error = error;
        const auto text = nlohmann::json{{"path", utf8(receipt_->path)},
            {"identity_confirmed", receipt_->identity_confirmed}, {"cleanup_error", error},
            {"volume", impl_->identity.dwVolumeSerialNumber}, {"file_high", impl_->identity.nFileIndexHigh},
            {"file_low", impl_->identity.nFileIndexLow}}.dump(2);
        const auto marker = receipt_->path.parent_path() /
            (L".cleanup-pending-" + path_from_utf8(bytes_sha256({
                reinterpret_cast<const std::uint8_t *>(text.data()), text.size()})).native() + L".json");
        if (fault_) fault_("before_cleanup_marker");
        atomic_write(marker, text, false);
        receipt_->marker_failed = false;
    } catch (...) {
        // The receipt still blocks this Application even if the disk cannot
        // accept a marker; surviving staging is never auto-deleted on restart.
        receipt_->marker_failed = true;
    }
}
void OwnedBundleDirectory::rollback() {
    auto &state = *receipt_;
    if ((!state.created && impl_->pending == INVALID_HANDLE_VALUE) || state.accepted) return;
    if (state.cleanup_attempted) {
        if (state.cleanup_pending) throw std::runtime_error(state.cleanup_error);
        return;
    }
    state.cleanup_attempted = true;
    try {
        require(state.identity_confirmed, "OWNED_DIRECTORY_IDENTITY_UNCONFIRMED");
        if (fault_) fault_("before_cleanup");
        std::vector<std::unique_ptr<Held>> parents;
        for (const auto &parent : ancestors(state.path.parent_path())) parents.push_back(std::make_unique<Held>(parent, true));
        if (state.created) {
            erase_owned_tree(state.path, &impl_->identity);
            state.created = false;
        }
        remove_preparation_marker();
    } catch (...) {
        // Failure state must survive even if allocating diagnostic text fails.
        state.cleanup_pending = true;
        state.marker_failed = true;
        const auto error = exception_text(std::current_exception());
        note_cleanup_failure(error);
        throw;
    }
}
OwnedBundleDirectory::DirectoryIdentity OwnedBundleDirectory::directory_identity() const {
    require(receipt_->identity_confirmed, "OWNED_DIRECTORY_IDENTITY_UNCONFIRMED");
    return {impl_->identity.dwVolumeSerialNumber, impl_->identity.nFileIndexHigh, impl_->identity.nFileIndexLow};
}
void OwnedBundleDirectory::erase_retired(const std::filesystem::path &path, const DirectoryIdentity &identity) {
    require(path.is_absolute(), "OWNED_DIRECTORY_PATH_INVALID");
    std::vector<std::unique_ptr<Held>> parents;
    for (const auto &parent : ancestors(path.parent_path())) parents.push_back(std::make_unique<Held>(parent, true));
    BY_HANDLE_FILE_INFORMATION expected{};
    expected.dwVolumeSerialNumber = identity[0];
    expected.nFileIndexHigh = identity[1];
    expected.nFileIndexLow = identity[2];
    erase_owned_tree(path, &expected);
}
[[noreturn]] void OwnedBundleDirectory::rethrow_with_cleanup(std::exception_ptr primary) {
    try { rollback(); }
    catch (...) { throw DirectoryCleanupPending(exception_text(primary),
        exception_text(std::current_exception()), receipt_->path); }
    std::rethrow_exception(primary);
}
OwnedBundleDirectory::~OwnedBundleDirectory() noexcept {
    try { rollback(); } catch (...) {}
}
void OwnedBundleDirectory::rename_to(const std::filesystem::path &destination) {
    require(receipt_->created && receipt_->identity_confirmed && !receipt_->accepted,
        "OWNED_DIRECTORY_NOT_PREPARED");
    auto target = std::filesystem::absolute(destination).lexically_normal().make_preferred();
    require(target.parent_path() == receipt_->path.parent_path(), "OWNED_DIRECTORY_CROSS_PARENT_RENAME");
    std::vector<std::unique_ptr<Held>> parents;
    for (const auto &parent : ancestors(target.parent_path()))
        parents.push_back(std::make_unique<Held>(parent, true,
            parents.empty() ? FILE_ADD_FILE | FILE_ADD_SUBDIRECTORY : 0));
    // Index the uncertain window BEFORE a staging name can become an ordinary
    // request/cache name. Even failure to allocate/write a later cleanup record
    // then leaves a durable, conservatively blocking identity record.
    require(impl_->pending == INVALID_HANDLE_VALUE, "OWNED_DIRECTORY_RENAME_ALREADY_PENDING");
    const auto text = nlohmann::json{{"source", utf8(receipt_->path)}, {"destination", utf8(target)},
        {"accepted", false}, {"volume", impl_->identity.dwVolumeSerialNumber},
        {"file_high", impl_->identity.nFileIndexHigh}, {"file_low", impl_->identity.nFileIndexLow}}.dump(2);
    const auto marker = target.parent_path() / (L".cleanup-pending-owned-" + path_from_utf8(bytes_sha256({
        reinterpret_cast<const std::uint8_t *>(text.data()), text.size()})).native() + L".json");
    if (fault_) fault_("before_prepare_marker_create");
    impl_->pending = CreateFileW(extended_path(marker).c_str(), GENERIC_READ | GENERIC_WRITE | DELETE, FILE_SHARE_READ,
        nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    require(impl_->pending != INVALID_HANDLE_VALUE, "OWNED_DIRECTORY_PREPARATION_MARKER_CREATE_FAILED");
    require(GetFileInformationByHandle(impl_->pending, &impl_->pending_identity) &&
        !(impl_->pending_identity.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT),
        "OWNED_DIRECTORY_PREPARATION_MARKER_IDENTITY_FAILED");
    impl_->pending_identity_confirmed = true;
    DWORD written{};
    require(text.size() <= MAXDWORD && WriteFile(impl_->pending, text.data(), static_cast<DWORD>(text.size()),
        &written, nullptr) && written == text.size(), "OWNED_DIRECTORY_PREPARATION_MARKER_WRITE_FAILED");
    require(FlushFileBuffers(impl_->pending), "OWNED_DIRECTORY_PREPARATION_MARKER_FLUSH_FAILED");
    struct Close { HANDLE value{INVALID_HANDLE_VALUE}; ~Close() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); } } current;
    current.value = CreateFileW(extended_path(receipt_->path).c_str(), FILE_READ_ATTRIBUTES | DELETE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (current.value == INVALID_HANDLE_VALUE)
        throw std::runtime_error("NATIVE_PUBLICATION_RENAME_FAILED:" + std::to_string(GetLastError()));
    BY_HANDLE_FILE_INFORMATION info{};
    require(GetFileInformationByHandle(current.value, &info) && same_file(info, impl_->identity) &&
        !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT), "OWNED_DIRECTORY_IDENTITY_CHANGED");
    struct RenameInformation { BOOLEAN replace; HANDLE root; ULONG length; WCHAR name[1]; };
    const auto name = target.filename().native();
    const auto bytes = name.size() * sizeof(WCHAR);
    const auto size = (std::max)(sizeof(RenameInformation), offsetof(RenameInformation, name) + bytes);
    require(size <= MAXDWORD, "OWNED_DIRECTORY_RENAME_PATH_TOO_LARGE");
    std::vector<std::max_align_t> buffer((size + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t));
    auto *rename = reinterpret_cast<RenameInformation *>(buffer.data());
    rename->replace = FALSE; rename->root = parents.front()->handle; rename->length = static_cast<ULONG>(bytes);
    std::memcpy(rename->name, name.data(), bytes);
    using SetInformation = NTSTATUS(NTAPI *)(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, ULONG);
    const auto call = reinterpret_cast<SetInformation>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtSetInformationFile"));
    require(call != nullptr, "NATIVE_BUNDLE_LINK_API_UNAVAILABLE");
    IO_STATUS_BLOCK result{};
    const auto status = call(current.value, &result, rename, static_cast<ULONG>(size), 10);
    if (status < 0) throw std::runtime_error("NATIVE_PUBLICATION_RENAME_FAILED:NTSTATUS=" +
        std::to_string(static_cast<ULONG>(status)));
    receipt_->path.swap(target);
    if (fault_) fault_("after_rename");
}
void OwnedBundleDirectory::remove_preparation_marker() {
    if (impl_->pending == INVALID_HANDLE_VALUE) return;
    BY_HANDLE_FILE_INFORMATION now{};
    require(impl_->pending_identity_confirmed && GetFileInformationByHandle(impl_->pending, &now) &&
        same_file(now, impl_->pending_identity) && !(now.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT),
        "OWNED_DIRECTORY_PREPARATION_MARKER_IDENTITY_CHANGED");
    if (fault_) fault_("before_prepare_marker_remove");
    FILE_DISPOSITION_INFO disposition{TRUE};
    require(SetFileInformationByHandle(impl_->pending, FileDispositionInfo, &disposition, sizeof(disposition)),
        "OWNED_DIRECTORY_PREPARATION_MARKER_DELETE_FAILED");
    CloseHandle(impl_->pending);
    impl_->pending = INVALID_HANDLE_VALUE;
}
void OwnedBundleDirectory::accept() noexcept {
    // Acceptance is never reversed merely because the auxiliary index cannot
    // be removed. Keep that index and block later preparations instead.
    receipt_->accepted = true;
    try { remove_preparation_marker(); }
    catch (...) {
        receipt_->cleanup_pending = true;
        receipt_->marker_failed = true;
        try { note_cleanup_failure(exception_text(std::current_exception())); } catch (...) {}
    }
}
thread_local std::function<void(const char *)> BundleLease::copy_fault_;
thread_local std::function<bool(const char *)> BundleLease::io_failure_;
struct BundleLease::Impl {
    std::filesystem::path root;
    std::string revision, id;
    Manifest manifest;
    Manifest publication_resources;
    bool publication_validated{};
    std::map<std::string, std::shared_ptr<Held>> files;
    std::map<std::filesystem::path, std::unique_ptr<Held>> directories;
    std::vector<std::shared_ptr<BundleLease>> parts;
    std::vector<std::unique_ptr<Held>> anchors;
    std::string resource_digest;
    std::uint64_t resource_bytes{}, control_bytes{};
    std::uint64_t hashed_bytes{};
    mutable std::uint64_t directory_checks{};
    std::set<std::string> members() const {
        std::set<std::string> found;
        const auto os_root = extended_path(root);
        for (const auto &entry : std::filesystem::recursive_directory_iterator(os_root)) {
            auto attributes = GetFileAttributesW(entry.path().c_str());
            require(attributes != INVALID_FILE_ATTRIBUTES &&
                        !(attributes & FILE_ATTRIBUTE_REPARSE_POINT),
                    "BUNDLE_LINK_REJECTED");
            const auto relative = entry.path().lexically_relative(os_root);
            auto name = utf8(relative);
            std::replace(name.begin(), name.end(), '\\', '/');
            if (entry.is_directory())
                require(directories.contains(root / relative), "BUNDLE_DIRECTORY_CHANGED");
            else {
                require(entry.is_regular_file(), "BUNDLE_FILE_TYPE");
                found.insert(name);
            }
        }
        return found;
    }
};
std::filesystem::path BundleLease::checked_relative(const std::string &value) {
    require(!value.empty() && value.find_first_of(":\\\0") == std::string::npos &&
                value.find('\0') == std::string::npos,
            "RESOURCE_PATH_INVALID");
    auto path = path_from_utf8(value);
    // Windows 的 /foo 没有盘符，不满足 is_absolute，却会覆盖拼接路径的根目录。
    // manifest 成员必须完全相对，同时拒绝 root_name 与 root_directory。
    require(!path.has_root_path(), "RESOURCE_PATH_INVALID");
    for (const auto &part : path) {
        auto text = part.wstring();
        auto stem = fold(part.stem());
        require(!text.empty() && text != L"." && text != L".." && text.back() != L'.' &&
                    text.back() != L' ' && text.find_first_of(L"<>\"|?*") == std::wstring::npos &&
                    stem != L"con" && stem != L"nul" && stem != L"prn" && stem != L"aux" &&
                    !(stem.size() == 4 && (stem.starts_with(L"com") || stem.starts_with(L"lpt")) &&
                      stem[3] >= L'0' && stem[3] <= L'9'),
                "RESOURCE_PATH_INVALID");
    }
    return path.make_preferred();
}
BundleLease::BundleLease(std::filesystem::path root, std::string revision, Manifest manifest,
                         const std::function<void()> &check_cancel)
    : BundleLease(std::move(root), std::move(revision), std::move(manifest), check_cancel, false) {}
namespace {
using J = nlohmann::json;
bool digest_valid(const std::string &value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}
J held_json(Held &file, std::uint64_t limit, const std::function<void()> &cancel = {}) {
    require(file.checked_size() <= limit, "PUBLICATION_CONTROL_TOO_LARGE");
    const auto hash = handle_sha256(file.handle, file.checked_size(), cancel);
    const auto &bytes = file.verified_bytes(hash, cancel);
    return J::parse(bytes);
}
BundleLease::Manifest identity_resources(const J &identity) {
    auto files = identity.at("source_files").get<BundleLease::Manifest>();
    const auto mods = identity.at("mod_files").get<BundleLease::Manifest>();
    require(!files.empty() && identity.at("source_revision").is_string() &&
        !identity.at("source_revision").get<std::string>().empty(), "PUBLICATION_SOURCE_IDENTITY_INVALID");
    require(identity.at("mod_revision").is_null() ? mods.empty() :
        (identity.at("mod_revision").is_string() && !identity.at("mod_revision").get<std::string>().empty() && !mods.empty()),
        "PUBLICATION_MOD_IDENTITY_INVALID");
    for (const auto &[path, hash] : mods) {
        (void)hash;
        BundleLease::checked_relative(path);
        require(path.starts_with("image/"), "NATIVE_MOD_NOT_IMAGE");
    }
    for (const auto &[name, source] : identity.at("image_sources").items()) {
        (void)name;
        const auto kind = source.at("source").get<std::string>();
        const auto path = source.at("path").get<std::string>();
        const auto hash = source.at("sha256").get<std::string>();
        require(kind == "baseline" || kind == "mod", "PUBLICATION_IMAGE_SOURCE_INVALID");
        const auto &from = kind == "mod" ? mods : identity.at("source_files").get<BundleLease::Manifest>();
        require(from.contains(path) && from.at(path) == hash, "PUBLICATION_IMAGE_SOURCE_INVALID");
        if (kind == "mod") files[path] = hash;
    }
    return files;
}
std::string json_digest(const J &value) {
    const auto text = value.dump();
    return bytes_sha256({reinterpret_cast<const std::uint8_t *>(text.data()), text.size()});
}
void check_program(const J &identity, const J &flow, const std::string &revision) {
    require(json_digest(identity) == revision, "PUBLICATION_IDENTITY_MISMATCH");
    auto program = identity.at("program");
    require(identity.at("engine_kind") == "wvd_native" && identity.at("program_schema") == program.at("program_schema"),
        "PUBLICATION_PROGRAM_SCHEMA_INVALID");
    require(program.at("revision") == "pending", "PUBLICATION_IDENTITY_PROGRAM_INVALID");
    program["revision"] = revision;
    require(program == flow, "PUBLICATION_PROGRAM_MISMATCH");
}
}
BundleLease::BundleLease(std::filesystem::path root, std::string revision, Manifest manifest,
                         const std::function<void()> &check_cancel, bool physical)
    : impl_(std::make_unique<Impl>()) {
    auto &s = *impl_;
    require(!revision.empty() && !manifest.empty(), "BUNDLE_MANIFEST_INVALID");
    s.root = std::filesystem::absolute(root).lexically_normal().make_preferred();
    s.revision = std::move(revision);
    s.id = unique_id();
    s.manifest = std::move(manifest);
    if (!physical && std::filesystem::exists(extended_path(s.root / "publication-layout.json"))) {
        // Pin the marker before interpreting it. No fallback is possible after
        // the explicit marker has been observed, including parse failures.
        // Ancestors must resist rename, while sibling requests/sets may still
        // be added. The two strict physical parts separately freeze their own
        // directories and files without FILE_SHARE_WRITE.
        for (const auto &path : ancestors(s.root)) s.anchors.push_back(std::make_unique<Held>(path, true, FILE_READ_ATTRIBUTES));
        auto marker = std::make_unique<Held>(s.root / "publication-layout.json", false);
        const auto layout = held_json(*marker, 65536, check_cancel);
        require(layout.is_object() && layout.size() == 4 && layout.at("schema") == 1 &&
            layout.at("kind") == "shared_resource_set" && layout.at("program_revision") == s.revision,
            "PUBLICATION_LAYOUT_INVALID");
        s.resource_digest = layout.at("resource_digest").get<std::string>();
        require(digest_valid(s.resource_digest), "PUBLICATION_RESOURCE_DIGEST_INVALID");
        const auto shared_root = s.root.parent_path() / ".asset-sets" / s.resource_digest;
        for (const auto &path : ancestors(shared_root)) s.anchors.push_back(std::make_unique<Held>(path, true, FILE_READ_ATTRIBUTES));
        auto description = std::make_unique<Held>(shared_root / "manifest.json", false);
        const auto resource = held_json(*description, 16ULL * 1024 * 1024, check_cancel);
        require(resource.is_object() && resource.size() == 2 && resource.at("schema") == 1 &&
            resource.at("files").is_array() && json_digest(resource) == s.resource_digest,
            "PUBLICATION_RESOURCE_MANIFEST_INVALID");
        Manifest shared_manifest{{"manifest.json", bytes_sha256(description->content)}};
        Manifest resources;
        std::string previous;
        for (const auto &file : resource.at("files")) {
            require(file.is_object() && file.size() == 3, "PUBLICATION_RESOURCE_MANIFEST_INVALID");
            const auto path = file.at("path").get<std::string>();
            checked_relative(path);
            const auto normalized = fold(checked_relative(path));
            require(normalized != L"publication-layout.json" && normalized != L"program/flow.json" && normalized != L"program/identity.json",
                "NATIVE_PUBLICATION_RESERVED_PATH");
            require(previous.empty() || previous < path, "PUBLICATION_RESOURCE_ORDER_INVALID");
            previous = path;
            const auto hash = file.at("sha256").get<std::string>();
            require(digest_valid(hash) && resources.emplace(path, hash).second,
                "PUBLICATION_RESOURCE_MANIFEST_INVALID");
            shared_manifest.emplace("payload/" + path, hash);
        }
        auto shared = std::shared_ptr<BundleLease>(new BundleLease(shared_root, s.resource_digest,
            std::move(shared_manifest), check_cancel, true));
        Manifest request_manifest{{"publication-layout.json", bytes_sha256(marker->content)}};
        for (const auto &name : {"program/flow.json", "program/identity.json"}) {
            require(s.manifest.contains(name), "PUBLICATION_PROGRAM_MISSING");
            request_manifest.emplace(name, s.manifest.at(name));
        }
        if (s.manifest.contains("parameters/image-sources.json") && !resources.contains("parameters/image-sources.json"))
            request_manifest.emplace("parameters/image-sources.json", s.manifest.at("parameters/image-sources.json"));
        auto request = std::shared_ptr<BundleLease>(new BundleLease(s.root, s.revision,
            std::move(request_manifest), check_cancel, true));
        const auto id = J::parse(request->bytes("program/identity.json"));
        check_program(id, J::parse(request->bytes("program/flow.json")), s.revision);
        require(identity_resources(id) == resources, "PUBLICATION_IDENTITY_RESOURCES_MISMATCH");
        const bool has_mod = !id.at("mod_revision").is_null();
        require(has_mod == request->manifest().contains("parameters/image-sources.json"),
            "PUBLICATION_PROVENANCE_INVALID");
        if (has_mod) require(J::parse(request->bytes("parameters/image-sources.json")) == id.at("image_sources"),
            "PUBLICATION_PROVENANCE_INVALID");
        Manifest logical = resources;
        for (const auto &[name, hash] : request->manifest()) {
            if (name == "publication-layout.json") continue;
            require(logical.emplace(name, hash).second, "PUBLICATION_LOGICAL_COLLISION");
            s.files.emplace(name, request->impl_->files.at(name));
        }
        require(logical == s.manifest, "PUBLICATION_LOGICAL_MANIFEST_MISMATCH");
        std::set<std::wstring> logical_names;
        for (const auto &[name, hash] : logical)
            require(logical_names.insert(fold(checked_relative(name))).second, "BUNDLE_CASE_COLLISION");
        for (const auto &file : resource.at("files")) {
            const auto name = file.at("path").get<std::string>();
            require(file.at("bytes").is_number_unsigned() &&
                file.at("bytes").get<std::uint64_t>() == shared->member_size("payload/" + name),
                "PUBLICATION_RESOURCE_SIZE_MISMATCH");
            s.resource_bytes += shared->member_size("payload/" + name);
            s.files.emplace(name, shared->impl_->files.at("payload/" + name));
        }
        s.control_bytes = marker->checked_size() + description->checked_size();
        s.hashed_bytes = request->hash_bytes() + shared->hash_bytes();
        s.parts = {std::move(request), std::move(shared)};
        verify_members();
        return;
    }
    // 连同祖先检查 reparse，避免 canonical 把链接证据抹掉。
    for (const auto &p : ancestors(s.root)) {
        auto attributes = GetFileAttributesW(p.c_str());
        require(attributes != INVALID_FILE_ATTRIBUTES &&
                    !(attributes & FILE_ATTRIBUTE_REPARSE_POINT),
                "BUNDLE_LINK_REJECTED");
    }
    s.directories.emplace(s.root, std::make_unique<Held>(s.root, true));
    const auto os_root = extended_path(s.root);
    for (const auto &entry : std::filesystem::recursive_directory_iterator(os_root)) {
        require(!(GetFileAttributesW(entry.path().c_str()) & FILE_ATTRIBUTE_REPARSE_POINT),
                "BUNDLE_LINK_REJECTED");
        if (entry.is_directory()) {
            const auto path = s.root / entry.path().lexically_relative(os_root);
            s.directories.emplace(path, std::make_unique<Held>(path, true));
        }
    }
    std::set<std::wstring> names;
    for (const auto &[relative, hash] : s.manifest) {
        if (check_cancel) check_cancel();
        auto path = checked_relative(relative);
        require(names.insert(fold(path)).second, "BUNDLE_CASE_COLLISION");
        require(hash.size() == 64 && std::all_of(hash.begin(), hash.end(),
                                                 [](char c) {
                                                     return (c >= '0' && c <= '9') ||
                                                            (c >= 'a' && c <= 'f');
                                                 }),
                "BUNDLE_MANIFEST_INVALID");
        s.files.emplace(relative, std::make_unique<Held>(s.root / path, false));
    }
    if (physical) {
        std::set<std::filesystem::path> expected{s.root};
        for (const auto &[relative, hash] : s.manifest) {
            (void)hash;
            for (auto path = (s.root / checked_relative(relative)).parent_path(); path != s.root; path = path.parent_path())
                expected.insert(path);
        }
        require(expected.size() == s.directories.size() &&
            std::all_of(s.directories.begin(), s.directories.end(), [&](const auto &item) { return expected.contains(item.first); }),
            "PUBLICATION_DIRECTORY_NOT_IN_MANIFEST");
    }
    // 全部文件锁定后才读取同一对象的内容，不重新按路径打开另一个文件计算哈希。
    for (auto &[relative, file] : s.files) {
        if (check_cancel) check_cancel();
        const auto &hash = s.manifest.at(relative);
        // OCR consumes locked model paths. Avoid retaining a second whole copy
        // of each ONNX file; templates and other bytes() consumers stay eager.
        if (relative.ends_with(".onnx")) file->verify_streamed(hash, check_cancel);
        else (void)file->verified_bytes(hash, check_cancel);
        s.hashed_bytes += file->checked_size();
    }
    verify_members();
}
BundleLease::~BundleLease() = default;
void BundleLease::verify_members() const {
    auto &s = *impl_;
    ++s.directory_checks;
    if (!s.parts.empty()) {
        for (const auto &part : s.parts) part->verify_members();
        for (const auto &anchor : s.anchors) anchor->verify_identity();
        return;
    }
    std::set<std::string> expected;
    for (const auto &[name, _] : s.manifest)
        expected.insert(name);
    auto found = s.members();
    for (const auto &name : found)
        if (!expected.contains(name)) throw MissingBundleMember(name);
    require(found == expected, "BUNDLE_MANIFEST_INCOMPLETE");
    for (const auto &[_, directory] : s.directories)
        directory->verify_identity();
    // 文件句柄拒绝删除/替换；成员扫描已拒绝 reparse，不重复打开全部文件或读内容。
}
void BundleLease::require_member(const std::string &relative) const {
    checked_relative(relative);
    if (!impl_->files.contains(relative)) throw MissingBundleMember(relative);
}
const std::vector<std::uint8_t> &BundleLease::bytes(const std::string &relative) const {
    require_member(relative);
    return impl_->files.at(relative)->verified_bytes(impl_->manifest.at(relative));
}
void BundleLease::copy_member(const std::string &relative,
    const std::filesystem::path &destination, const std::function<void()> &check_cancel) const {
    require_member(relative);
    require(destination.is_absolute(), "NATIVE_BUNDLE_DESTINATION_INVALID");
    if (check_cancel) check_cancel();
    // Freeze the target ancestor chain as well as the source; do not follow a
    // junction or permit a parent rename during the synchronous copy.
    std::vector<std::unique_ptr<Held>> parents;
    for (const auto &p : ancestors(destination.parent_path()))
        parents.push_back(std::make_unique<Held>(p, true));
    auto &file = *impl_->files.at(relative);
    std::lock_guard lock(file.content_mutex);
    const auto length = file.checked_size();
    file.verify_identity();
    struct Output {
        HANDLE handle{INVALID_HANDLE_VALUE};
        ~Output() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
    } output;
    output.handle = CreateFileW(extended_path(destination).c_str(), GENERIC_READ | GENERIC_WRITE, 0,
        nullptr, CREATE_NEW, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    require(output.handle != INVALID_HANDLE_VALUE, "NATIVE_BUNDLE_CREATE_FAILED");
    if (copy_fault_) copy_fault_("before_copy");
    const auto digest = copy_handle_sha256(file.handle, output.handle, length, [&] {
        if (check_cancel) check_cancel();
        if (io_failure_ && io_failure_("write")) throw std::runtime_error("NATIVE_BUNDLE_WRITE_FAILED");
    });
    file.verify_identity();
    require(digest == impl_->manifest.at(relative), "RESOURCE_HASH_MISMATCH");
    LARGE_INTEGER written{};
    require(GetFileSizeEx(output.handle, &written) && written.QuadPart == static_cast<LONGLONG>(length),
            "NATIVE_BUNDLE_SIZE_MISMATCH");
    if (copy_fault_) copy_fault_("before_flush");
    require(!(io_failure_ && io_failure_("flush")) && FlushFileBuffers(output.handle), "NATIVE_BUNDLE_FLUSH_FAILED");
    require(handle_sha256(output.handle, length, check_cancel) == digest, "RESOURCE_HASH_MISMATCH");
    if (check_cancel) check_cancel();
}
const std::string &BundleLease::hash(const std::string &relative) const {
    require_member(relative);
    return impl_->manifest.at(relative);
}
void BundleLease::link_member(const std::string &relative, const std::filesystem::path &destination) const {
    require_member(relative);
    require(destination.is_absolute(), "NATIVE_BUNDLE_DESTINATION_INVALID");
    std::vector<std::unique_ptr<Held>> parents;
    for (const auto &p : ancestors(destination.parent_path()))
        parents.push_back(std::make_unique<Held>(p, true, parents.empty() ? FILE_ADD_FILE : 0));
    require(!parents.empty(), "NATIVE_BUNDLE_DESTINATION_INVALID");
    auto &file = *impl_->files.at(relative);
    std::lock_guard lock(file.content_mutex);
    file.verify_identity();
    // FileLinkInformation operates on the frozen handle without reopening it
    // for DELETE access (CreateHardLink would conflict with the active lease).
    // This is the NTFS FILE_LINK_INFORMATION ABI, not the wire-format record.
    struct LinkInformation {
        BOOLEAN replace;
        HANDLE root;
        ULONG length;
        WCHAR name[1];
    };
    using SetInformation = NTSTATUS(NTAPI *)(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, ULONG);
    const auto module = GetModuleHandleW(L"ntdll.dll");
    const auto set_information = reinterpret_cast<SetInformation>(
        module ? GetProcAddress(module, "NtSetInformationFile") : nullptr);
    require(set_information != nullptr, "NATIVE_BUNDLE_LINK_API_UNAVAILABLE");
    // Only this unpublished staging directory permits adding members. Its
    // rename remains locked; all source and published file leases stay read-only.
    // Publication verifies membership and then takes the normal read-only lease.
    const auto name = destination.filename().native();
    const auto name_bytes = name.size() * sizeof(WCHAR);
    const auto length = (std::max)(sizeof(LinkInformation), offsetof(LinkInformation, name) + name_bytes);
    require(length <= MAXDWORD, "NATIVE_BUNDLE_LINK_PATH_TOO_LARGE");
    std::vector<std::max_align_t> buffer((length + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t));
    auto *info = reinterpret_cast<LinkInformation *>(buffer.data());
    info->replace = FALSE;
    info->root = parents.front()->handle;
    info->length = static_cast<ULONG>(name_bytes);
    std::memcpy(info->name, name.data(), name_bytes);
    IO_STATUS_BLOCK result{};
    constexpr ULONG file_link_information = 11;
    const auto status = set_information(file.handle, &result, info, static_cast<ULONG>(length),
        file_link_information);
    if (status < 0)
        throw BundleLinkError(static_cast<std::uint32_t>(status));
    file.verify_identity();
}
std::uint64_t BundleLease::member_size(const std::string &relative) const {
    require_member(relative);
    const auto &file = *impl_->files.at(relative);
    std::lock_guard lock(file.content_mutex);
    file.verify_identity();
    return file.checked_size();
}
const std::filesystem::path &BundleLease::root() const { return impl_->root; }
const std::string &BundleLease::revision() const { return impl_->revision; }
const std::string &BundleLease::identity() const { return impl_->id; }
std::uint64_t BundleLease::hash_bytes() const { return impl_->hashed_bytes; }
BundleLease::StorageStats BundleLease::storage_stats() const {
    StorageStats result;
    if (!impl_->parts.empty()) {
        for (const auto &part : impl_->parts) {
            const auto value = part->storage_stats();
            result.size_bytes += value.size_bytes; result.capacity_bytes += value.capacity_bytes;
            result.model_bytes += value.model_bytes; result.image_bytes += value.image_bytes;
            result.json_bytes += value.json_bytes; result.other_bytes += value.other_bytes;
        }
        return result;
    }
    for (const auto &[name, file] : impl_->files) {
        std::lock_guard lock(file->content_mutex);
        const auto size = file->content.size();
        result.size_bytes += size;
        result.capacity_bytes += file->content.capacity();
        if (name.ends_with(".onnx")) result.model_bytes += size;
        else if (name.ends_with(".png")) result.image_bytes += size;
        else if (name.ends_with(".json")) result.json_bytes += size;
        else result.other_bytes += size;
    }
    return result;
}
std::size_t BundleLease::file_count() const { return impl_->files.size(); }
std::size_t BundleLease::directory_count() const {
    auto result = impl_->directories.size();
    for (const auto &part : impl_->parts) result += part->directory_count();
    return result;
}
std::uint64_t BundleLease::directory_checks() const { return impl_->directory_checks; }
const std::filesystem::path &BundleLease::physical_member(const std::string &relative) const {
    require_member(relative);
    impl_->files.at(relative)->verify_identity();
    return impl_->files.at(relative)->path;
}
const BundleLease::Manifest &BundleLease::manifest() const { return impl_->manifest; }
const std::string &BundleLease::resource_digest() const { return impl_->resource_digest; }
std::uint64_t BundleLease::resource_bytes() const { return impl_->resource_bytes; }
std::uint64_t BundleLease::control_bytes() const { return impl_->control_bytes; }
std::uint64_t BundleLease::retained_resource_bytes(const std::filesystem::path &published) {
    const auto sets = published / ".asset-sets";
    if (!std::filesystem::exists(extended_path(sets))) return 0;
    OwnedBundleDirectory::require_no_pending(sets);
    std::vector<std::unique_ptr<Held>> anchors;
    for (const auto &path : ancestors(sets)) anchors.push_back(std::make_unique<Held>(path, true, FILE_READ_ATTRIBUTES));
    std::uint64_t total = 0;
    std::size_t count = 0;
    for (const auto &entry : std::filesystem::directory_iterator(extended_path(sets))) {
        require(++count <= 65536 && entry.is_directory(), "PUBLICATION_RESOURCE_PRIVATE_ROOT_INVALID");
        const auto digest = utf8(entry.path().filename());
        require(digest_valid(digest), "PUBLICATION_RESOURCE_PRIVATE_ROOT_INVALID");
        const auto root = sets / entry.path().filename();
        auto pinned = std::make_unique<Held>(root, true);
        Held description(root / "manifest.json", false);
        const auto resource = held_json(description, 16ULL * 1024 * 1024);
        require(resource.is_object() && resource.size() == 2 && resource.at("schema") == 1 &&
            resource.at("files").is_array() && json_digest(resource) == digest,
            "PUBLICATION_RESOURCE_MANIFEST_INVALID");
        Manifest files{{"manifest.json", bytes_sha256(description.content)}};
        std::string previous;
        for (const auto &file : resource.at("files")) {
            require(file.is_object() && file.size() == 3, "PUBLICATION_RESOURCE_MANIFEST_INVALID");
            const auto path = file.at("path").get<std::string>();
            checked_relative(path);
            require(previous.empty() || previous < path, "PUBLICATION_RESOURCE_ORDER_INVALID");
            previous = path;
            require(files.emplace("payload/" + path, file.at("sha256").get<std::string>()).second,
                "PUBLICATION_RESOURCE_MANIFEST_INVALID");
        }
        BundleLease set(root, digest, std::move(files), {}, true);
        for (const auto &file : resource.at("files"))
            require(file.at("bytes").is_number_unsigned() && file.at("bytes").get<std::uint64_t>() ==
                set.member_size("payload/" + file.at("path").get<std::string>()), "PUBLICATION_RESOURCE_SIZE_MISMATCH");
        require(total <= (std::numeric_limits<std::uint64_t>::max)() - set.hash_bytes(), "NATIVE_PUBLICATION_BUDGET_OVERFLOW");
        total += set.hash_bytes();
    }
    return total;
}
std::shared_ptr<BundleLease> BundleLease::open_publication(const std::filesystem::path &root,
    const std::function<void()> &check_cancel) {
    std::vector<std::unique_ptr<Held>> anchors;
    for (const auto &path : ancestors(root)) anchors.push_back(std::make_unique<Held>(path, true, FILE_READ_ATTRIBUTES));
    Held identity(root / "program/identity.json", false), flow(root / "program/flow.json", false);
    const auto id = held_json(identity, 256ULL * 1024 * 1024, check_cancel);
    const auto program = held_json(flow, 256ULL * 1024 * 1024, check_cancel);
    const auto revision = json_digest(id);
    check_program(id, program, revision);
    auto files = identity_resources(id);
    const auto resources = files;
    require(files.emplace("program/identity.json", bytes_sha256(identity.content)).second &&
        files.emplace("program/flow.json", bytes_sha256(flow.content)).second, "PUBLICATION_LOGICAL_COLLISION");
    std::unique_ptr<Held> provenance;
    if (!id.at("mod_revision").is_null()) {
        provenance = std::make_unique<Held>(root / "parameters/image-sources.json", false);
        require(held_json(*provenance, 256ULL * 1024 * 1024, check_cancel) == id.at("image_sources"),
            "PUBLICATION_PROVENANCE_INVALID");
        require(files.emplace("parameters/image-sources.json", bytes_sha256(provenance->content)).second,
            "PUBLICATION_LOGICAL_COLLISION");
    }
    auto result = std::make_shared<BundleLease>(root, revision, std::move(files), check_cancel);
    result->impl_->publication_resources = resources;
    result->impl_->publication_validated = true;
    return result;
}
const BundleLease::Manifest &BundleLease::publication_resources() const {
    require(impl_->publication_validated, "PUBLICATION_IDENTITY_NOT_VALIDATED");
    return impl_->publication_resources;
}
} // namespace wvd::platform
