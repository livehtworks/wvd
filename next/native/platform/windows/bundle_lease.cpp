#include "bundle_lease.hpp"
#include "file_digest.hpp"
#include "runtime_files.hpp"
#include "path_utf8.hpp"
#include <algorithm>
#include <cwctype>
#include <set>
#include <windows.h>

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
    std::vector<std::uint8_t> content;
    explicit Held(std::filesystem::path p, bool directory) : path(std::move(p)) {
        handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
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
    void read() {
        auto length = (std::uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
        require(length <= 256 * 1024 * 1024, "INTEGRITY_FILE_TOO_LARGE");
        content.resize(static_cast<std::size_t>(length));
        for (std::size_t offset = 0; offset < content.size();) {
            DWORD count{};
            require(
                ReadFile(handle, content.data() + offset,
                         static_cast<DWORD>(std::min<std::size_t>(65536, content.size() - offset)),
                         &count, nullptr) &&
                    count,
                "INTEGRITY_READ_FAILED");
            offset += count;
        }
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
    return path;
}
BundleLease::BundleLease(std::filesystem::path root, std::string revision, Manifest manifest)
    : impl_(std::make_unique<Impl>()) {
    auto &s = *impl_;
    require(!revision.empty() && !manifest.empty(), "BUNDLE_MANIFEST_INVALID");
    s.root = std::filesystem::absolute(root).lexically_normal();
    s.revision = std::move(revision);
    s.id = unique_id();
    s.manifest = std::move(manifest);
    // 连同祖先检查 reparse，避免 canonical 把链接证据抹掉。
    for (auto p = s.root; !p.empty() && p != p.parent_path(); p = p.parent_path()) {
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
        file->read();
        file->verify_identity();
        s.hashed_bytes += file->content.size();
        require(bytes_sha256(file->content) == s.manifest.at(relative), "RESOURCE_HASH_MISMATCH");
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
        require(expected.contains(name), "RESOURCE_NOT_IN_MANIFEST");
    require(found == expected, "BUNDLE_MANIFEST_INCOMPLETE");
    for (const auto &[_, directory] : s.directories)
        directory->verify_identity();
    // 文件句柄拒绝删除/替换；成员扫描已拒绝 reparse，不重复打开全部文件或读内容。
}
void BundleLease::require_member(const std::string &relative) const {
    checked_relative(relative);
    require(impl_->files.contains(relative), "RESOURCE_NOT_IN_MANIFEST");
}
const std::vector<std::uint8_t> &BundleLease::bytes(const std::string &relative) const {
    require_member(relative);
    return impl_->files.at(relative)->content;
}
const std::string &BundleLease::hash(const std::string &relative) const {
    require_member(relative);
    return impl_->manifest.at(relative);
}
const std::filesystem::path &BundleLease::root() const { return impl_->root; }
const std::string &BundleLease::revision() const { return impl_->revision; }
const std::string &BundleLease::identity() const { return impl_->id; }
std::uint64_t BundleLease::hash_bytes() const { return impl_->hashed_bytes; }
std::size_t BundleLease::file_count() const { return impl_->files.size(); }
std::size_t BundleLease::directory_count() const { return impl_->directories.size(); }
std::uint64_t BundleLease::directory_checks() const { return impl_->directory_checks; }
} // namespace wvd::platform
