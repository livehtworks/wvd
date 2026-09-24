#include "capture_protocol.hpp"
#include <array>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <windows.h>

namespace {
using namespace wvd::devices::capture_protocol;
using Connect = int(__cdecl *)(const wchar_t *, int);
using Disconnect = void(__cdecl *)(int);
using Display = int(__cdecl *)(int, const char *, int);
using Capture = int(__cdecl *)(int, unsigned, int, int *, int *, unsigned char *);

bool exact(HANDLE handle, void *buffer, DWORD size, bool reading) {
    auto *cursor = static_cast<unsigned char *>(buffer);
    while (size) {
        DWORD count{};
        const bool ok = reading ? ReadFile(handle, cursor, size, &count, nullptr)
                                : WriteFile(handle, cursor, size, &count, nullptr);
        if (!ok || !count) return false;
        cursor += count;
        size -= count;
    }
    return true;
}
} // namespace

int wmain(int argc, wchar_t **argv) {
    if (argc != 6) return 2;
    const std::filesystem::path root(argv[1]), library(argv[2]);
    if (!root.is_absolute() || !library.is_absolute() ||
        !std::filesystem::is_regular_file(library)) return 3;
    wchar_t *end{};
    const long instance = wcstol(argv[3], &end, 10);
    if (!end || *end || instance < 0 || instance > 10000) return 4;
    const int name_bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[4], -1,
                                                nullptr, 0, nullptr, nullptr);
    if (name_bytes < 2 || name_bytes > 256) return 5;
    std::string package(static_cast<std::size_t>(name_bytes), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[4], -1, package.data(),
                             name_bytes, nullptr, nullptr)) return 5;
    // 独立继承的协议句柄在加载 DLL 前取得；标准输出留给厂商日志，绝不混入帧数据。
    wchar_t *handle_end{};
    const auto raw_handle = std::wcstoull(argv[5], &handle_end, 10);
    if (!handle_end || *handle_end || !raw_handle) return 13;
    HANDLE output = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(raw_handle));
    DWORD handle_flags{};
    if (!GetHandleInformation(output, &handle_flags)) return 13;
    HMODULE module = LoadLibraryExW(library.c_str(), nullptr,
                                    LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!module) return 6;
    const auto connect = reinterpret_cast<Connect>(GetProcAddress(module, "nemu_connect"));
    const auto disconnect = reinterpret_cast<Disconnect>(GetProcAddress(module, "nemu_disconnect"));
    const auto display = reinterpret_cast<Display>(GetProcAddress(module, "nemu_get_display_id"));
    const auto capture_frame = reinterpret_cast<Capture>(GetProcAddress(module, "nemu_capture_display"));
    if (!connect || !disconnect || !display || !capture_frame) return 7;
    const int handle = connect(root.c_str(), static_cast<int>(instance));
    if (handle <= 0) return 8;
    int display_id = -1;
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    int code = 0;
    for (;;) {
        Request request;
        if (!exact(input, &request, sizeof(request), true)) break;
        if (request.marker != magic || request.schema != version || request.reserved) {
            code = 10; break;
        }
        if (request.operation == quit) break;
        if (request.operation != capture) { code = 11; break; }
        Reply reply;
        reply.sequence = request.sequence;
        if (display_id < 0) display_id = display(handle, package.c_str(), 0);
        if (display_id < 0) {
            reply.status = 3; // 游戏显示尚未出现，保留 helper 供后续重试。
            if (!exact(output, &reply, sizeof(reply), false)) { code = 12; break; }
            continue;
        }
        reply.display_id = static_cast<std::uint32_t>(display_id);
        int width{}, height{};
        const int query = capture_frame(handle, static_cast<unsigned>(display_id), 0,
                                        &width, &height, nullptr);
        if (query != 0 || width <= 0 || height <= 0 ||
            width > static_cast<int>(max_dimension) || height > static_cast<int>(max_dimension)) {
            display_id = -1;
            reply.status = 1;
            if (!exact(output, &reply, sizeof(reply), false)) { code = 12; break; }
            continue;
        }
        const auto bytes = static_cast<std::uint32_t>(width * height * 4);
        std::vector<unsigned char> pixels(bytes);
        int actual_width{}, actual_height{};
        const int result = capture_frame(handle, static_cast<unsigned>(display_id), bytes,
                                         &actual_width, &actual_height, pixels.data());
        if (result != 0 || width != actual_width || height != actual_height) {
            display_id = -1;
            reply.status = 2;
            if (!exact(output, &reply, sizeof(reply), false)) { code = 12; break; }
            continue;
        }
        reply.width = static_cast<std::uint32_t>(width);
        reply.height = static_cast<std::uint32_t>(height);
        reply.stride = reply.width * 4;
        reply.byte_count = bytes;
        if (!exact(output, &reply, sizeof(reply), false) ||
            !exact(output, pixels.data(), bytes, false)) { code = 12; break; }
    }
    disconnect(handle);
    FreeLibrary(module);
    return code;
}
