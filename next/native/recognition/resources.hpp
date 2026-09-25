#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <opencv2/core.hpp>
#include <string>
#include <stdexcept>

namespace wvd::recognition {
struct ResourcePressure : std::exception {
    explicit ResourcePressure(const char *code) noexcept : code_(code) {}
    const char *what() const noexcept override { return code_; }
  private:
    const char *code_;
};

struct ResourceStats {
    std::uint64_t retained_bytes{}, in_use_bytes{}, evictable_bytes{}, live_bytes{};
    std::uint64_t decode_count{}, mask_build_count{}, entries{}, active_matches{}, peak_matches{};
    std::uint64_t estimated_workspace_bytes{}, peak_estimated_workspace_bytes{};
    std::uint64_t result_cache_entries{}, result_cache_estimated_bytes{};
};

// Application 持有同一份准入；合法超目标匹配独占执行。目标仅约束并发，
// 不宣称覆盖 OpenCV 内部所有分配。
class MatchBudget {
  public:
    class Ticket {
      public:
        Ticket() = default;
        Ticket(MatchBudget *owner, std::uint64_t bytes) : owner_(owner), bytes_(bytes) {}
        Ticket(const Ticket &) = delete;
        Ticket &operator=(const Ticket &) = delete;
        Ticket(Ticket &&other) noexcept;
        Ticket &operator=(Ticket &&other) noexcept;
        ~Ticket();
        bool oversized_single() const { return owner_ && bytes_ > owner_->target_; }
      private:
        MatchBudget *owner_{};
        std::uint64_t bytes_{};
    };
    explicit MatchBudget(std::uint64_t target = 256ULL * 1024 * 1024) : target_(target) {}
    Ticket acquire(std::uint64_t bytes, const std::atomic<bool> &cancelled);
    void wake() noexcept { available_.notify_all(); }
    ResourceStats stats() const;
  private:
    friend class Ticket;
    void release(std::uint64_t bytes) noexcept;
    const std::uint64_t target_;
    mutable std::mutex mutex_;
    std::condition_variable available_;
    std::uint64_t used_{}, peak_used_{}, peak_active_{};
    std::uint64_t active_{};
};

// 估算已知 CPU 缓冲，包括普通 CCOEFF_NORMED 的两张 CV_64F 积分图；
// 不是内存上界。
std::uint64_t estimate_match_workspace(cv::Size search, cv::Size templ, int channels,
                                       bool masked, bool excluded, bool scaled,
                                       bool preprocessed = false);

class DecodedAssetCache : public std::enable_shared_from_this<DecodedAssetCache> {
    struct Asset;
  public:
    class Lease {
      public:
        Lease() = default;
        Lease(const Lease &) = delete;
        Lease &operator=(const Lease &) = delete;
        Lease(Lease &&other) noexcept;
        Lease &operator=(Lease &&other) noexcept;
        ~Lease();
        const cv::Mat &mat() const;
      private:
        friend class DecodedAssetCache;
        Lease(std::shared_ptr<Asset> asset, std::weak_ptr<DecodedAssetCache> owner);
        void release() noexcept;
        std::shared_ptr<Asset> asset_;
        std::weak_ptr<DecodedAssetCache> owner_;
    };
    explicit DecodedAssetCache(std::uint64_t target = 128ULL * 1024 * 1024);
    Lease load(const std::string &key, const std::function<cv::Mat()> &decode,
               const std::atomic<bool> *cancelled = nullptr);
    ResourceStats stats() const;
  private:
    struct SharedCounters {
        std::atomic<std::uint64_t> live{0}, borrowed{0};
    };
    struct Asset {
        cv::Mat pixels;
        std::uint64_t bytes{};
        std::shared_ptr<SharedCounters> counters;
        std::atomic<unsigned> borrowers{0};
        ~Asset();
    };
    struct Entry {
        std::shared_ptr<Asset> asset;
        std::exception_ptr error;
        bool loading{true};
        unsigned waiters{};
        std::condition_variable ready;
        std::list<std::string>::iterator lru;
    };
    void trim_locked();
    void trim_after_release();
    const std::uint64_t target_;
    std::shared_ptr<SharedCounters> counters_ = std::make_shared<SharedCounters>();
    mutable std::mutex mutex_;
    std::map<std::string, std::shared_ptr<Entry>> entries_;
    std::list<std::string> lru_;
    std::uint64_t retained_{}, decodes_{}, mask_builds_{};
};
} // namespace wvd::recognition
