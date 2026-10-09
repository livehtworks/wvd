#pragma once
#include <filesystem>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <functional>

namespace wvd::platform {
class MissingBundleMember final : public std::runtime_error {
  public:
    explicit MissingBundleMember(std::string member) : std::runtime_error("RESOURCE_NOT_IN_MANIFEST"), member_(std::move(member)) {}
    const std::string &member() const { return member_; }
  private:
    std::string member_;
};
// Win32 文件共享锁保护整个快照，不依赖文件时间戳。由运行会话持有至资源释放。
class BundleLease final {
  public:
    using Manifest = std::map<std::string, std::string>;
    BundleLease(std::filesystem::path root, std::string revision, Manifest manifest,
                const std::function<void()> &check_cancel = {});
    ~BundleLease();
    BundleLease(const BundleLease &) = delete;
    BundleLease &operator=(const BundleLease &) = delete;
    void verify_members() const;
    void require_member(const std::string &relative) const;
    // ONNX is verified without retaining its source buffer. An explicit bytes()
    // request loads it once from the same locked handle; the returned reference
    // remains immutable and valid until this lease is destroyed.
    const std::vector<std::uint8_t> &bytes(const std::string &relative) const;
    void copy_member(const std::string &relative, const std::filesystem::path &destination,
                     const std::function<void()> &check_cancel = {}) const;
    void link_member(const std::string &relative, const std::filesystem::path &destination) const;
    const std::string &hash(const std::string &relative) const;
    const std::filesystem::path &root() const;
    const std::string &revision() const;
    const std::string &identity() const;
    std::uint64_t hash_bytes() const;
    struct StorageStats {
        // Retained source buffers only; hash_bytes() includes streamed models.
        std::uint64_t size_bytes{}, capacity_bytes{}, model_bytes{}, image_bytes{}, json_bytes{}, other_bytes{};
    };
    StorageStats storage_stats() const;
    std::size_t file_count() const;
    std::size_t directory_count() const;
    std::uint64_t directory_checks() const;
    static std::filesystem::path checked_relative(const std::string &value);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace wvd::platform
