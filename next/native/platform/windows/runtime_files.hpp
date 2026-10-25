#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <functional>
#include <mutex>

namespace wvd::platform {
struct AtomicWriteTestAccess;
void atomic_write(const std::filesystem::path &target, const std::string &contents, bool replace);
namespace detail {
enum class AtomicWritePhase { Write, Flush, Commit };
// Only the native test friend may install an I/O fault. There is no product
// setting or public API that can bypass the real atomic-write implementation.
class AtomicWriteFaults {
    using Callback = std::function<bool(AtomicWritePhase, const std::filesystem::path &)>;
    static std::mutex mutex_;
    static Callback callback_;
    static void install(Callback callback);
    static bool fail(AtomicWritePhase phase, const std::filesystem::path &target);
    friend struct ::wvd::platform::AtomicWriteTestAccess;
    friend void ::wvd::platform::atomic_write(const std::filesystem::path &, const std::string &, bool);
};
}
std::string unique_id();
std::filesystem::path extended_path(const std::filesystem::path &path);
class DeviceLease {
  public:
    explicit DeviceLease(const std::string &identity);
    ~DeviceLease();
    DeviceLease(const DeviceLease &) = delete;
    DeviceLease &operator=(const DeviceLease &) = delete;

  private:
    void *handle_{};
};
} // namespace wvd::platform
