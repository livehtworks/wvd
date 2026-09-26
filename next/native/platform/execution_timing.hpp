#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <json.hpp>

namespace wvd::platform::timing {
// 会话内固定大小记账，不采样像素、不写文件、不拥有运行线程。
enum class Part { Capture, Convert, Metadata, InputValidation, Queue, Match,
    ParallelWait, Recognition, JsonEvents, ExplicitWait, ArchiveSubmit, InputDelivery, Count };
enum class Counter { Captures, Matches, CacheHits, AdbClients, ContextTransactions,
    OverlayChecks, OverlayReuse, FrameReuse, BusinessPredicates, Count };
inline constexpr std::array names{"pixel_capture", "pixel_convert", "capture_metadata", "input_validation",
    "budget_queue", "match", "parallel_wait", "recognition_other", "json_events", "explicit_wait",
    "archive_submit", "input_delivery"};
inline constexpr std::array counter_names{"captures", "actual_matches", "leaf_cache_hits", "adb_clients",
    "context_transactions", "overlay_checks", "overlay_reuse", "frame_reuse", "business_predicates"};
struct Sample {
    std::array<std::uint64_t, names.size()> wall{}, workers{};
    std::array<std::uint64_t, counter_names.size()> counts{};
};
struct Totals {
    std::array<std::atomic<std::uint64_t>, names.size()> wall{}, workers{};
    std::array<std::atomic<std::uint64_t>, counter_names.size()> counts{};
    Sample sample() const noexcept {
        Sample value;
        for (std::size_t i = 0; i < names.size(); ++i) { value.wall[i] = wall[i]; value.workers[i] = workers[i]; }
        for (std::size_t i = 0; i < counter_names.size(); ++i) value.counts[i] = counts[i];
        return value;
    }
};
class Scope;
inline thread_local Totals *active{};
inline thread_local Scope *parent{};
inline thread_local bool worker{};
class Bind {
    Totals *old_; Scope *parent_; bool worker_;
  public:
    explicit Bind(Totals *totals, bool parallel_worker = false) noexcept
        : old_(active), parent_(parent), worker_(worker) { active = totals; parent = nullptr; worker = parallel_worker; }
    ~Bind() { active = old_; parent = parent_; worker = worker_; }
};
inline void count(Counter value) noexcept { if (active) ++active->counts[static_cast<std::size_t>(value)]; }
class Scope {
    Totals *totals_; Scope *parent_; Part part_; bool worker_;
    std::chrono::steady_clock::time_point start_;
    std::uint64_t children_{};
  public:
    explicit Scope(Part part) noexcept : totals_(active), parent_(parent), part_(part), worker_(worker),
        start_(std::chrono::steady_clock::now()) { if (totals_) parent = this; }
    ~Scope() { finish(); }
    void finish() noexcept {
        if (!totals_) return;
        const auto ns = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - start_).count());
        (worker_ ? totals_->workers : totals_->wall)[static_cast<std::size_t>(part_)] += ns > children_ ? ns - children_ : 0;
        if (parent_) parent_->children_ += ns;
        parent = parent_; totals_ = nullptr;
    }
};
inline nlohmann::json report(const Sample &end, std::uint64_t elapsed_ns, const Sample &begin = {}) {
    nlohmann::json wall = nlohmann::json::object(), workers = wall, counts = wall;
    std::uint64_t accounted{};
    for (std::size_t i = 0; i < names.size(); ++i) {
        const auto value = end.wall[i] - begin.wall[i];
        wall[names[i]] = value; workers[names[i]] = end.workers[i] - begin.workers[i]; accounted += value;
    }
    for (std::size_t i = 0; i < counter_names.size(); ++i) counts[counter_names[i]] = end.counts[i] - begin.counts[i];
    return {{"wall_ns", elapsed_ns}, {"exclusive_ns", std::move(wall)}, {"worker_exclusive_ns", std::move(workers)},
        {"counts", std::move(counts)}, {"unattributed_ns", elapsed_ns > accounted ? elapsed_ns - accounted : 0},
        {"accounted_ns", accounted}, {"worker_times_additive_to_wall", false}};
}
} // namespace wvd::platform::timing
