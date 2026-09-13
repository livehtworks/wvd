#include "file_digest.hpp"
#include <array>
#include <fstream>
#include <stdexcept>
#include <windows.h>

// bcrypt.h 依赖 Windows 类型，单独分组避免格式化工具调换顺序。
#include <bcrypt.h>

namespace wvd::platform {
std::string file_sha256(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("RESOURCE_UNREADABLE");
    // 使用 Windows 系统密码 API；不再引入另一份散列实现或外部命令。
    struct Hash {
        BCRYPT_ALG_HANDLE algorithm{};
        BCRYPT_HASH_HANDLE value{};
        ~Hash() {
            if (value)
                BCryptDestroyHash(value);
            if (algorithm)
                BCryptCloseAlgorithmProvider(algorithm, 0);
        }
    } hash;
    if (BCryptOpenAlgorithmProvider(&hash.algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
        BCryptCreateHash(hash.algorithm, &hash.value, nullptr, 0, nullptr, 0, 0) < 0)
        throw std::runtime_error("HASH_INITIALIZATION_FAILED");
    std::array<unsigned char, 65536> bytes{};
    while (input) {
        input.read(reinterpret_cast<char *>(bytes.data()), bytes.size());
        if (input.gcount() &&
            BCryptHashData(hash.value, bytes.data(), static_cast<ULONG>(input.gcount()), 0) < 0)
            throw std::runtime_error("HASH_UPDATE_FAILED");
    }
    if (!input.eof())
        throw std::runtime_error("RESOURCE_READ_FAILED");
    std::array<unsigned char, 32> digest{};
    if (BCryptFinishHash(hash.value, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0)
        throw std::runtime_error("HASH_FINISH_FAILED");
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    for (auto byte : digest) {
        result += hex[byte >> 4];
        result += hex[byte & 15];
    }
    return result;
}
} // namespace wvd::platform
