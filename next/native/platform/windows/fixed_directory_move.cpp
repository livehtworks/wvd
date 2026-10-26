#include "fixed_directory_move.hpp"
#include "runtime_files.hpp"
#include <windows.h>
#include <winternl.h>
#include <vector>
#include <memory>
#include <cstring>
#include <stdexcept>
#include <cstdint>
#include <cstddef>
#include <algorithm>

namespace wvd::platform {
namespace {
void need(bool ok, const char *code) { if (!ok) throw std::runtime_error(code); }
struct Directory {
    HANDLE handle{INVALID_HANDLE_VALUE};
    BY_HANDLE_FILE_INFORMATION info{};
    Directory(const std::filesystem::path &path, DWORD access = 0) {
        handle = CreateFileW(extended_path(path).c_str(), GENERIC_READ | access,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error("PUBLICATION_MOVE_OPEN_FAILED:" + std::to_string(GetLastError()));
        if (!GetFileInformationByHandle(handle, &info) || GetFileType(handle) != FILE_TYPE_DISK ||
            !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            CloseHandle(handle); handle = INVALID_HANDLE_VALUE;
            throw std::runtime_error("PUBLICATION_MOVE_DIRECTORY_INVALID");
        }
    }
    ~Directory() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
    Directory(const Directory &) = delete;
    std::string id() const { return std::to_string(info.dwVolumeSerialNumber) + ":" +
        std::to_string((std::uint64_t(info.nFileIndexHigh) << 32) | info.nFileIndexLow); }
};
std::vector<std::unique_ptr<Directory>> pin(const std::filesystem::path &path, DWORD first_access = 0) {
    std::vector<std::unique_ptr<Directory>> result;
    // Match BundleLease's DOS/UNC traversal: MSVC treats \\?\D: as an
    // additional parent if the extended namespace is traversed directly.
    auto text = path.native();
    if (text.starts_with(L"\\\\?\\UNC\\")) text = L"\\\\" + text.substr(8);
    else if (text.starts_with(L"\\\\?\\")) text = text.substr(4);
    auto current = std::filesystem::absolute(text).lexically_normal();
    for (;;) {
        result.push_back(std::make_unique<Directory>(current, result.empty() ? first_access : 0));
        auto parent = current.parent_path();
        if (parent == current) break;
        current = parent;
    }
    return result;
}
}
std::string fixed_directory_identity(const std::filesystem::path &path) {
    auto parents = pin(path.parent_path());
    return Directory(path).id();
}
void move_fixed_directory(const std::filesystem::path &source,
    const std::filesystem::path &destination, const std::string &expected) {
    need(source.is_absolute() && destination.is_absolute() && source != destination,
        "PUBLICATION_MOVE_PATH_INVALID");
    auto source_parents = pin(source.parent_path());
    auto target_parents = pin(destination.parent_path(), FILE_ADD_SUBDIRECTORY);
    Directory moving(source, DELETE);
    need(moving.id() == expected, "PUBLICATION_MOVE_ID_MISMATCH");
    need(moving.info.dwVolumeSerialNumber == target_parents.front()->info.dwVolumeSerialNumber,
        "PUBLICATION_MOVE_CROSS_VOLUME");
    const auto name = destination.filename().native();
    need(!name.empty() && name != L"." && name != L"..", "PUBLICATION_MOVE_PATH_INVALID");
    struct Rename { BOOLEAN replace; HANDLE root; ULONG length; WCHAR name[1]; };
    const auto length = (std::max)(sizeof(Rename), offsetof(Rename, name) + name.size() * sizeof(WCHAR));
    std::vector<std::max_align_t> memory((length + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t));
    auto *info = reinterpret_cast<Rename *>(memory.data());
    info->replace = FALSE; info->root = target_parents.front()->handle;
    info->length = static_cast<ULONG>(name.size() * sizeof(WCHAR));
    std::memcpy(info->name, name.data(), info->length);
    using Set = NTSTATUS(NTAPI *)(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, ULONG);
    const auto set = reinterpret_cast<Set>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtSetInformationFile"));
    need(set != nullptr, "PUBLICATION_MOVE_API_UNAVAILABLE");
    IO_STATUS_BLOCK status{};
    const auto result = set(moving.handle, &status, info, static_cast<ULONG>(length), 10);
    if (result < 0) throw std::runtime_error("PUBLICATION_MOVE_REJECTED:" + std::to_string(std::uint32_t(result)));
    BY_HANDLE_FILE_INFORMATION after{};
    need(GetFileInformationByHandle(moving.handle,&after) &&
        after.dwVolumeSerialNumber == moving.info.dwVolumeSerialNumber && after.nFileIndexHigh == moving.info.nFileIndexHigh &&
        after.nFileIndexLow == moving.info.nFileIndexLow, "PUBLICATION_MOVE_POSTCONDITION_FAILED");
    std::vector<wchar_t> actual(32768), parent(32768);
    const auto actual_size = GetFinalPathNameByHandleW(moving.handle,actual.data(),static_cast<DWORD>(actual.size()),FILE_NAME_NORMALIZED);
    const auto parent_size = GetFinalPathNameByHandleW(target_parents.front()->handle,parent.data(),static_cast<DWORD>(parent.size()),FILE_NAME_NORMALIZED);
    need(actual_size && actual_size < actual.size() && parent_size && parent_size < parent.size(), "PUBLICATION_MOVE_POSTCONDITION_FAILED");
    auto wanted = std::filesystem::path(std::wstring(parent.data(),parent_size)) / destination.filename();
    need(_wcsicmp(wanted.c_str(),std::wstring(actual.data(),actual_size).c_str()) == 0, "PUBLICATION_MOVE_POSTCONDITION_FAILED");
}
} // namespace wvd::platform
