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
#include <exception>

namespace wvd::platform {
class BundleLinkError final : public std::runtime_error {
  public:
    static constexpr std::uint32_t too_many_links = 0xc0000265u;
    explicit BundleLinkError(std::uint32_t status)
        : std::runtime_error("NATIVE_BUNDLE_LINK_FAILED:NTSTATUS=" + std::to_string(status)), status_(status) {}
    std::uint32_t ntstatus() const noexcept { return status_; }
  private:
    std::uint32_t status_;
};
class DirectoryCleanupPending final : public std::runtime_error {
  public:
    DirectoryCleanupPending(std::string primary, std::string cleanup, std::filesystem::path path);
    const std::string &primary_error() const noexcept { return primary_; }
    const std::string &cleanup_error() const noexcept { return cleanup_; }
    const std::filesystem::path &path() const noexcept { return path_; }
  private:
    std::string primary_, cleanup_;
    std::filesystem::path path_;
};
// The token is the only preparation-time deletion owner. A retained receipt
// proves explicit Coordinator acceptance even after the worker has finished.
class OwnedBundleDirectory final {
  public:
    struct Receipt {
        std::filesystem::path path;
        bool created{}, identity_confirmed{}, accepted{}, cleanup_pending{}, cleanup_attempted{}, marker_failed{};
        std::string cleanup_error;
        void require_clean_acceptance() const;
    };
    static std::unique_ptr<OwnedBundleDirectory> create(const std::filesystem::path &path);
    static void require_no_pending(const std::filesystem::path &parent);
    ~OwnedBundleDirectory() noexcept;
    OwnedBundleDirectory(const OwnedBundleDirectory &) = delete;
    OwnedBundleDirectory &operator=(const OwnedBundleDirectory &) = delete;
    std::shared_ptr<const Receipt> receipt() const noexcept { return receipt_; }
    void rename_to(const std::filesystem::path &destination);
    void accept() noexcept;
    void rollback();
    [[noreturn]] void rethrow_with_cleanup(std::exception_ptr primary);
  private:
    friend struct OwnedBundleDirectoryTestAccess;
    explicit OwnedBundleDirectory(std::filesystem::path path);
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::shared_ptr<Receipt> receipt_;
    static thread_local std::function<void(const char *)> fault_;
    void note_cleanup_failure(const std::string &error) noexcept;
    void remove_preparation_marker();
};
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
    std::uint64_t member_size(const std::string &relative) const;
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
    // Only paths backed by this live lease may escape to path-only consumers.
    const std::filesystem::path &physical_member(const std::string &relative) const;
    const Manifest &manifest() const;
    // Available only after the production publication identity validator ran.
    const Manifest &publication_resources() const;
    const std::string &resource_digest() const;
    std::uint64_t resource_bytes() const;
    std::uint64_t control_bytes() const;
    static std::shared_ptr<BundleLease> open_publication(const std::filesystem::path &root,
        const std::function<void()> &check_cancel = {});
    static std::uint64_t retained_resource_bytes(const std::filesystem::path &published);
    static std::filesystem::path checked_relative(const std::string &value);

  private:
    friend struct BundleLeaseTestAccess;
    static thread_local std::function<void(const char *)> copy_fault_;
    static thread_local std::function<bool(const char *)> io_failure_;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    BundleLease(std::filesystem::path root, std::string revision, Manifest manifest,
        const std::function<void()> &check_cancel, bool physical);
};
} // namespace wvd::platform
