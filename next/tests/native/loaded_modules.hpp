#pragma once
#include "maafw/buffers.hpp"
#include "platform/windows/file_digest.hpp"
#include <windows.h>

// 只检查本测试进程实际已加载的模块，不能用依赖缓存文件替代运行时装载证据。
inline nlohmann::json loaded_vision_modules() {
    auto result = nlohmann::json::array();
    for (const auto *name : {L"MaaFramework.dll", L"opencv_world4_maa.dll"}) {
        auto handle = GetModuleHandleW(name);
        if (!handle)
            throw std::runtime_error("EXPECTED_RUNTIME_MODULE_NOT_LOADED");
        std::wstring path(32768, L'\0');
        auto length = GetModuleFileNameW(handle, path.data(), static_cast<DWORD>(path.size()));
        if (!length || length >= path.size())
            throw std::runtime_error("RUNTIME_MODULE_PATH_FAILED");
        path.resize(length);
        auto file = std::filesystem::path(path);
        result.push_back({{"name", wvd::maafw::utf8(file.filename())},
                          {"path", wvd::maafw::utf8(file)},
                          {"sha256", wvd::platform::file_sha256(file)}});
    }
    return result;
}
