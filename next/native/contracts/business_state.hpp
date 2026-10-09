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
// 状态提交可能来自异步回调，必须在业务状态边界互斥。
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
    nlohmann::json field(const std::string &path) const {
        std::lock_guard lock(mutex_);
        if (path.empty() || path.front() != '/') throw std::runtime_error("BUSINESS_FIELD_PATH_INVALID");
        const auto end = path.find('/', 1);
        auto value = summarize_field(path.substr(1, end == std::string::npos ? end : end - 1));
        if (end != std::string::npos) value = value.at(nlohmann::json::json_pointer(path.substr(end)));
        return value;
    }
    nlohmann::json fields(std::initializer_list<std::string> names) const {
        std::lock_guard lock(mutex_);
        auto values = nlohmann::json::object();
        for (const auto &name : names) values[name] = summarize_field(name);
        return values;
    }
    std::uint64_t version() const {
        std::lock_guard lock(mutex_);
        return version_;
    }

  protected:
    virtual void on_segment(SegmentBoundary, std::uint64_t, std::size_t) = 0;
    virtual nlohmann::json summarize() const = 0;
    virtual nlohmann::json summarize_field(const std::string &name) const { return summarize().at(name); }

  private:
    mutable std::recursive_mutex mutex_;
    std::uint64_t version_{};
};
} // namespace wvd::contracts
