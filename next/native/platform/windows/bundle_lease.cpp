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
        handle = CreateFileW(path.c_str(), GENERIC_READ | extra_access, sharing, nullptr, OPEN_EXISTING,
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
        auto attributes = GetFileAttributesW(path.c_str());
        require(attributes != INVALID_FILE_ATTRIBUTES &&
                    !(attributes & FILE_ATTRIBUTE_REPARSE_POINT),
                "INTEGRITY_PATH_CHANGED");
        HANDLE current =
            CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
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
} // namespace
struct BundleLease::Impl {
    std::filesystem::path root;
    std::string revision, id;
    Manifest manifest;
    std::map<std::string, std::unique_ptr<Held>> files;
    std::map<std::filesystem::path, std::unique_ptr<Held>> directories;
    std::uint64_t hashed_bytes{};
    mutable std::uint64_t directory_checks{};
    std::set<std::string> members() const {
        std::set<std::string> found;
        for (const auto &entry : std::filesystem::recursive_directory_iterator(root)) {
            auto attributes = GetFileAttributesW(entry.path().c_str());
            require(attributes != INVALID_FILE_ATTRIBUTES &&
                        !(attributes & FILE_ATTRIBUTE_REPARSE_POINT),
                    "BUNDLE_LINK_REJECTED");
            auto name = utf8(entry.path().lexically_relative(root));
            std::replace(name.begin(), name.end(), '\\', '/');
            if (entry.is_directory())
                require(directories.contains(entry.path()), "BUNDLE_DIRECTORY_CHANGED");
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
    : impl_(std::make_unique<Impl>()) {
    auto &s = *impl_;
    require(!revision.empty() && !manifest.empty(), "BUNDLE_MANIFEST_INVALID");
    s.root = std::filesystem::absolute(root).lexically_normal().make_preferred();
    s.revision = std::move(revision);
    s.id = unique_id();
    s.manifest = std::move(manifest);
    // 连同祖先检查 reparse，避免 canonical 把链接证据抹掉。
    for (const auto &p : ancestors(s.root)) {
        auto attributes = GetFileAttributesW(p.c_str());
        require(attributes != INVALID_FILE_ATTRIBUTES &&
                    !(attributes & FILE_ATTRIBUTE_REPARSE_POINT),
                "BUNDLE_LINK_REJECTED");
    }
    s.directories.emplace(s.root, std::make_unique<Held>(s.root, true));
    for (const auto &entry : std::filesystem::recursive_directory_iterator(s.root)) {
        require(!(GetFileAttributesW(entry.path().c_str()) & FILE_ATTRIBUTE_REPARSE_POINT),
                "BUNDLE_LINK_REJECTED");
        if (entry.is_directory())
            s.directories.emplace(entry.path(), std::make_unique<Held>(entry.path(), true));
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
    output.handle = CreateFileW(destination.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
        nullptr, CREATE_NEW, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    require(output.handle != INVALID_HANDLE_VALUE, "NATIVE_BUNDLE_CREATE_FAILED");
    const auto digest = copy_handle_sha256(file.handle, output.handle, length, check_cancel);
    file.verify_identity();
    require(digest == impl_->manifest.at(relative), "RESOURCE_HASH_MISMATCH");
    LARGE_INTEGER written{};
    require(GetFileSizeEx(output.handle, &written) && written.QuadPart == static_cast<LONGLONG>(length),
            "NATIVE_BUNDLE_SIZE_MISMATCH");
    require(FlushFileBuffers(output.handle), "NATIVE_BUNDLE_FLUSH_FAILED");
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
    for (auto p = destination.parent_path(); !p.empty() && p != p.parent_path(); p = p.parent_path())
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
        throw std::runtime_error("NATIVE_BUNDLE_LINK_FAILED:NTSTATUS=" +
            std::to_string(static_cast<ULONG>(status)));
    file.verify_identity();
}
const std::filesystem::path &BundleLease::root() const { return impl_->root; }
const std::string &BundleLease::revision() const { return impl_->revision; }
const std::string &BundleLease::identity() const { return impl_->id; }
std::uint64_t BundleLease::hash_bytes() const { return impl_->hashed_bytes; }
BundleLease::StorageStats BundleLease::storage_stats() const {
    StorageStats result;
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
std::size_t BundleLease::directory_count() const { return impl_->directories.size(); }
std::uint64_t BundleLease::directory_checks() const { return impl_->directory_checks; }
} // namespace wvd::platform
