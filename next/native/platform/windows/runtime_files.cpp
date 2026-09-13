#include "runtime_files.hpp"
#include <stdexcept>
#include <windows.h>

// COM 声明依赖 Windows 基础类型，保持在 windows.h 之后。
#include <objbase.h>

namespace wvd::platform {
std::string unique_id() {
    GUID id{};
    if (FAILED(CoCreateGuid(&id)))
        throw std::runtime_error("ID_GENERATION_FAILED");
    wchar_t text[40]{};
    if (!StringFromGUID2(id, text, 40))
        throw std::runtime_error("ID_FORMAT_FAILED");
    std::string result;
    for (auto p = text + 1; *p && *p != L'}'; ++p)
        result += static_cast<char>(*p);
    return result;
}
void atomic_write(const std::filesystem::path &target, const std::string &contents, bool replace) {
    auto temporary = target;
    temporary += "." + unique_id() + ".tmp";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        throw std::runtime_error("STORAGE_CREATE_FAILED");
    bool success = true;
    for (std::size_t position = 0; position < contents.size();) {
        DWORD written{};
        const auto size =
            static_cast<DWORD>(std::min<std::size_t>(contents.size() - position, 65536));
        if (!WriteFile(file, contents.data() + position, size, &written, nullptr) || !written) {
            success = false;
            break;
        }
        position += written;
    }
    success = FlushFileBuffers(file) && success;
    CloseHandle(file);
    // 失败时保留旧文件和本次 .tmp 证据，不用删除旧结果来伪造原子更新。
    if (!success ||
        !MoveFileExW(temporary.c_str(), target.c_str(),
                     MOVEFILE_WRITE_THROUGH | (replace ? MOVEFILE_REPLACE_EXISTING : 0)))
        throw std::runtime_error("STORAGE_COMMIT_FAILED");
}
DeviceLease::DeviceLease(const std::string &identity) {
    if (identity.empty() || identity.size() > 128)
        throw std::runtime_error("DEVICE_ID_INVALID");
    constexpr wchar_t hex[] = L"0123456789abcdef";
    std::wstring name = L"Local\\WvdNext.Device.";
    for (unsigned char c : identity) {
        name += hex[c >> 4];
        name += hex[c & 15];
    }
    auto handle = CreateSemaphoreW(nullptr, 1, 1, name.c_str());
    if (!handle)
        throw std::runtime_error("DEVICE_LEASE_FAILED");
    if (WaitForSingleObject(handle, 0) != WAIT_OBJECT_0) {
        CloseHandle(handle);
        throw std::runtime_error("DEVICE_BUSY");
    }
    handle_ = handle;
}
DeviceLease::~DeviceLease() {
    if (handle_) {
        ReleaseSemaphore(handle_, 1, nullptr);
        CloseHandle(handle_);
    }
}
} // namespace wvd::platform
