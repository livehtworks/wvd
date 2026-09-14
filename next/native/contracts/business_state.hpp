#pragma once
#include <json.hpp>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>

namespace wvd::contracts {
class MonotonicClock {
  public:
    using TimePoint = std::chrono::steady_clock::time_point;
    virtual ~MonotonicClock() = default;
    virtual TimePoint now() const noexcept = 0;
};
class SteadyClock final : public MonotonicClock {
  public:
    TimePoint now() const noexcept override { return std::chrono::steady_clock::now(); }
};
enum class SegmentBoundary { Initial, Continuation, Recovery, LifecycleRecovery };
struct StateCreationContext {
    std::string instance_id;
    std::uint64_t run_id;
    std::shared_ptr<const MonotonicClock> clock;
};

// Coordinator 独占实例。Context 仅在回调范围内借用；观察端只能取得值拷贝。
// 互斥覆盖原生回调线程，不能假设 Maa 的回调始终等于 Session 工作线程。
class BusinessRunState {
  public:
    virtual ~BusinessRunState() = default;
    bool apply(const std::function<bool(BusinessRunState &)> &operation) {
        std::lock_guard lock(mutex_);
        struct Changed {
            std::uint64_t &version;
            ~Changed() { ++version; }
        } changed{version_};
        return operation(*this);
    }
    void enter_segment(SegmentBoundary boundary, std::uint64_t generation, std::size_t unit) {
        std::lock_guard lock(mutex_);
        on_segment(boundary, generation, unit);
        ++version_;
    }
    nlohmann::json summary() const {
        std::lock_guard lock(mutex_);
        return summarize();
    }
    std::uint64_t version() const {
        std::lock_guard lock(mutex_);
        return version_;
    }

  protected:
    virtual void on_segment(SegmentBoundary, std::uint64_t, std::size_t) = 0;
    virtual nlohmann::json summarize() const = 0;

  private:
    mutable std::recursive_mutex mutex_;
    std::uint64_t version_{};
};
} // namespace wvd::contracts
