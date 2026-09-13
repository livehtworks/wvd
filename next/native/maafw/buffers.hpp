#pragma once

#include <MaaFramework/MaaAPI.h>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

namespace wvd::maafw {
template <class T, auto Destroy> using Handle = std::unique_ptr<T, decltype(Destroy)>;
using Image = Handle<MaaImageBuffer, MaaImageBufferDestroy>;
using String = Handle<MaaStringBuffer, MaaStringBufferDestroy>;
inline Image image_buffer() {
    Image result(MaaImageBufferCreate(), MaaImageBufferDestroy);
    if (!result)
        throw std::runtime_error("IMAGE_ALLOCATION_FAILED");
    return result;
}
inline String string_buffer() {
    String result(MaaStringBufferCreate(), MaaStringBufferDestroy);
    if (!result)
        throw std::runtime_error("STRING_ALLOCATION_FAILED");
    return result;
}
inline std::string utf8(const std::filesystem::path &path) {
    auto value = path.u8string();
    return std::string(value.begin(), value.end());
}
inline std::filesystem::path path_from_utf8(const std::string &text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}
} // namespace wvd::maafw
