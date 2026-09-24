#pragma once
#include <filesystem>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>
#include <cstdint>

namespace wvd::platform {
// Win32 文件共享锁保护整个快照，不依赖文件时间戳。由运行会话持有至资源释放。
class BundleLease final {
  public:
    using Manifest = std::map<std::string, std::string>;
    BundleLease(std::filesystem::path root, std::string revision, Manifest manifest);
    ~BundleLease();
    BundleLease(const BundleLease &) = delete;
    BundleLease &operator=(const BundleLease &) = delete;
    void verify_members() const;
    void require_member(const std::string &relative) const;
    const std::vector<std::uint8_t> &bytes(const std::string &relative) const;
    const std::string &hash(const std::string &relative) const;
    const std::filesystem::path &root() const;
    const std::string &revision() const;
    const std::string &identity() const;
    std::uint64_t hash_bytes() const;
    std::size_t file_count() const;
    std::size_t directory_count() const;
    std::uint64_t directory_checks() const;
    static std::filesystem::path checked_relative(const std::string &value);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace wvd::platform
