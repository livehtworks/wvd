#include "resources.hpp"
#include "platform/execution_timing.hpp"
#include "platform/windows/memory_diagnostics.hpp"
#include <algorithm>
#include <chrono>
#include <limits>
#include <stdexcept>
#include <utility>

namespace wvd::recognition {
namespace {
std::uint64_t multiply(std::uint64_t a, std::uint64_t b) {
    if (b && a > std::numeric_limits<std::uint64_t>::max() / b)
        throw ResourcePressure("MATCH_WORKSPACE_OVERFLOW");
    return a * b;
}
std::uint64_t add(std::uint64_t a, std::uint64_t b) {
    if (a > std::numeric_limits<std::uint64_t>::max() - b)
        throw ResourcePressure("MATCH_WORKSPACE_OVERFLOW");
    return a + b;
}
}

MatchBudget::Ticket::Ticket(Ticket &&other) noexcept
    : owner_(std::exchange(other.owner_, nullptr)), bytes_(other.bytes_) {}
MatchBudget::Ticket &MatchBudget::Ticket::operator=(Ticket &&other) noexcept {
    if (this != &other) {
        if (owner_) owner_->release(bytes_);
        owner_ = std::exchange(other.owner_, nullptr);
        bytes_ = other.bytes_;
    }
    return *this;
}
MatchBudget::Ticket::~Ticket() { if (owner_) owner_->release(bytes_); }
MatchBudget::Ticket MatchBudget::acquire(std::uint64_t bytes,
                                         const std::atomic<bool> &cancelled) {
    platform::timing::Scope measure(platform::timing::Part::Queue);
    std::unique_lock lock(mutex_);
    // 超目标单项等待其他票据退出后独占。短间隔唤醒兜住跨 Service 取消。
    while ((bytes > target_ ? active_ != 0 : used_ > target_ - bytes) && !cancelled)
        available_.wait_for(lock, std::chrono::milliseconds(50));
    if (cancelled) throw std::runtime_error("RECOGNITION_CANCELLED");
    if (bytes > target_) {
        const auto sample = platform::sample_memory();
        if (sample.system_ok && sample.page_size &&
            (sample.commit_limit_pages <= sample.commit_total_pages ||
             sample.commit_limit_pages - sample.commit_total_pages <
                 (bytes + sample.page_size - 1) / sample.page_size))
            throw ResourcePressure("MATCH_SYSTEM_COMMIT_PRESSURE");
    }
    used_ = add(used_, bytes);
    ++active_;
    peak_used_ = std::max(peak_used_, used_);
    peak_active_ = std::max(peak_active_, active_);
    return Ticket(this, bytes);
}
void MatchBudget::release(std::uint64_t bytes) noexcept {
    {
        std::lock_guard lock(mutex_);
        used_ -= bytes;
        --active_;
    }
    available_.notify_all();
}
ResourceStats MatchBudget::stats() const {
    std::lock_guard lock(mutex_);
    ResourceStats result;
    result.active_matches = active_;
    result.peak_matches = peak_active_;
    result.estimated_workspace_bytes = used_;
    result.peak_estimated_workspace_bytes = peak_used_;
    return result;
}

std::uint64_t estimate_match_workspace(cv::Size search, cv::Size templ, int channels,
                                       bool masked, bool excluded, bool scaled,
                                       bool preprocessed) {
    if (search.width <= 0 || search.height <= 0 || templ.width <= 0 || templ.height <= 0 ||
        channels < 1 || channels > 4 || templ.width > search.width || templ.height > search.height)
        throw std::runtime_error("MATCH_WORKSPACE_DIMENSIONS_INVALID");
    const auto pixels = multiply(search.width, search.height);
    const auto results = multiply(search.width - templ.width + 1,
                                  search.height - templ.height + 1);
    auto bytes = multiply(results, sizeof(float));
    // OpenCV 4.12 普通路径的 sum/sqsum 为 (w+1)*(h+1)*通道数的双精度图；
    // masked 路径另含浮点遮罩及多张乘积/均值中间图。
    if (!masked) {
        const auto integral = multiply(multiply(multiply(search.width + 1ULL,
            search.height + 1ULL), channels), sizeof(double));
        bytes = add(bytes, multiply(integral, 2));
        // crossCorr 的分块 DFT 缓冲取决于实现，这里只留调度余量，不称为上界。
        bytes = add(bytes, multiply(multiply(pixels, channels), 24));
    } else {
        bytes = add(bytes, multiply(multiply(pixels, channels), 64));
        bytes = add(bytes, multiply(multiply(templ.width, templ.height), 32));
    }
    if (excluded) bytes = add(bytes, multiply(multiply(pixels, channels), 1));
    if (scaled) bytes = add(bytes, multiply(multiply(templ.width, templ.height), channels));
    if (preprocessed) bytes = add(bytes, multiply(pixels, 3));
    return bytes;
}

DecodedAssetCache::Asset::~Asset() { if (counters) counters->live.fetch_sub(bytes); }
DecodedAssetCache::Lease::Lease(std::shared_ptr<Asset> asset,
                                std::weak_ptr<DecodedAssetCache> owner)
    : asset_(std::move(asset)), owner_(std::move(owner)) {
    if (asset_->borrowers.fetch_add(1) == 0)
        asset_->counters->borrowed.fetch_add(asset_->bytes);
}
DecodedAssetCache::Lease::Lease(Lease &&other) noexcept
    : asset_(std::move(other.asset_)), owner_(std::move(other.owner_)) {}
DecodedAssetCache::Lease &DecodedAssetCache::Lease::operator=(Lease &&other) noexcept {
    if (this != &other) {
        release();
        asset_ = std::move(other.asset_);
        owner_ = std::move(other.owner_);
    }
    return *this;
}
DecodedAssetCache::Lease::~Lease() { release(); }
void DecodedAssetCache::Lease::release() noexcept {
    const bool last = asset_ && asset_->borrowers.fetch_sub(1) == 1;
    if (last) asset_->counters->borrowed.fetch_sub(asset_->bytes);
    asset_.reset();
    if (last)
        if (auto owner = owner_.lock()) owner->trim_after_release();
    owner_.reset();
}
const cv::Mat &DecodedAssetCache::Lease::mat() const { return asset_->pixels; }
DecodedAssetCache::DecodedAssetCache(std::uint64_t target) : target_(target) {}
void DecodedAssetCache::trim_after_release() noexcept {
    try {
        std::lock_guard lock(mutex_);
        trim_locked();
    } catch (...) {
        // 析构路径不允许抛出；异常事实保留，下一次装载拒绝继续使用异常缓存。
        maintenance_failed_.store(true);
    }
}
void DecodedAssetCache::trim_locked() noexcept {
    while (retained_ > target_ && !lru_.empty()) {
        auto candidate = lru_.end();
        auto entry_it = entries_.end();
        for (auto cursor = lru_.end(); cursor != lru_.begin();) {
            --cursor;
            const auto found = entries_.find(*cursor);
            if (found == entries_.end() || !found->second->asset) {
                maintenance_failed_.store(true);
                return;
            }
            const auto &entry = found->second;
            if (!entry->waiters && !entry->asset->borrowers.load()) {
                candidate = cursor;
                entry_it = found;
                break;
            }
        }
        if (candidate == lru_.end()) break;
        // 全部使用已找到的迭代器，不复制 string、不调用可能抛 out_of_range 的 at。
        // 活动租约不会被驱逐；像素释放由 Asset 最后持有者负责。
        retained_ -= entry_it->second->asset->bytes;
        lru_.erase(candidate);
        entries_.erase(entry_it);
    }
}
DecodedAssetCache::Lease DecodedAssetCache::load(const std::string &key,
                                                 const std::function<cv::Mat()> &decode,
                                                 const std::atomic<bool> *cancelled) {
    if (maintenance_failed_.load()) throw ResourcePressure("ASSET_CACHE_MAINTENANCE_FAILED");
    std::shared_ptr<Entry> entry;
    bool loader = false;
    {
        std::unique_lock lock(mutex_);
        auto found = entries_.find(key);
        if (found == entries_.end()) {
            entry = std::make_shared<Entry>();
            entries_.emplace(key, entry);
            loader = true;
        } else {
            entry = found->second;
            ++entry->waiters;
            while (entry->loading && !(cancelled && cancelled->load()))
                entry->ready.wait_for(lock, std::chrono::milliseconds(50));
            --entry->waiters;
            if (cancelled && cancelled->load())
                throw std::runtime_error("RECOGNITION_CANCELLED");
            if (entry->error) std::rethrow_exception(entry->error);
            lru_.splice(lru_.begin(), lru_, entry->lru);
            return Lease(entry->asset, weak_from_this());
        }
    }
    if (loader) {
        try {
            if (cancelled && cancelled->load())
                throw std::runtime_error("RECOGNITION_CANCELLED");
            auto pixels = decode();
            if (pixels.empty()) throw std::runtime_error("ASSET_DECODE_EMPTY");
            if (!pixels.isContinuous()) throw std::runtime_error("ASSET_NONCONTIGUOUS");
            auto asset = std::make_shared<Asset>();
            asset->pixels = std::move(pixels);
            asset->bytes = multiply(multiply(asset->pixels.total(), asset->pixels.elemSize()), 1);
            asset->counters = counters_;
            counters_->live.fetch_add(asset->bytes);
            {
                std::lock_guard lock(mutex_);
                const auto new_retained = add(retained_, asset->bytes);
                lru_.push_front(key);
                entry->lru = lru_.begin();
                entry->asset = std::move(asset);
                retained_ = new_retained;
                if (key.starts_with("mask:")) ++mask_builds_;
                else ++decodes_;
                entry->loading = false;
                auto lease = Lease(entry->asset, weak_from_this());
                trim_locked();
                entry->ready.notify_all();
                return lease;
            }
        } catch (...) {
            std::lock_guard lock(mutex_);
            entry->error = std::current_exception();
            entry->loading = false;
            entry->ready.notify_all();
            std::rethrow_exception(entry->error);
        }
    }
    throw std::runtime_error("ASSET_LOADER_INVALID");
}
ResourceStats DecodedAssetCache::stats() const {
    std::lock_guard lock(mutex_);
    ResourceStats result;
    result.retained_bytes = retained_;
    result.in_use_bytes = counters_->borrowed.load();
    result.live_bytes = counters_->live.load();
    // trim_locked 不驱逐活动租约，借用像素属于 retained 的子集。
    // release 可并发减少 borrowed；这是有界标量快照，不为每次匹配遍历全部条目。
    result.evictable_bytes = retained_ - std::min(retained_, result.in_use_bytes);
    result.cache_maintenance_failed = maintenance_failed_.load();
    result.decode_count = decodes_;
    result.mask_build_count = mask_builds_;
    result.entries = entries_.size();
    return result;
}
} // namespace wvd::recognition
