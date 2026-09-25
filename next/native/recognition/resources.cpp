#include "resources.hpp"
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
void DecodedAssetCache::trim_after_release() {
    std::lock_guard lock(mutex_);
    trim_locked();
}
void DecodedAssetCache::trim_locked() {
    while (retained_ > target_ && !lru_.empty()) {
        auto candidate = lru_.end();
        for (auto cursor = lru_.end(); cursor != lru_.begin();) {
            --cursor;
            const auto &entry = entries_.at(*cursor);
            if (!entry->waiters && !entry->asset->borrowers.load()) {
                candidate = cursor;
                break;
            }
        }
        if (candidate == lru_.end()) break;
        const auto key = *candidate;
        auto entry = entries_.at(key);
        retained_ -= entry->asset->bytes;
        lru_.erase(candidate);
        entries_.erase(key);
        // 已借用的像素由租约继续持有；删除 LRU 键不代表底层缓冲已释放。
    }
}
DecodedAssetCache::Lease DecodedAssetCache::load(const std::string &key,
                                                 const std::function<cv::Mat()> &decode,
                                                 const std::atomic<bool> *cancelled) {
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
    for (const auto &[key, entry] : entries_)
        if (entry->asset && !entry->asset->borrowers.load())
            result.evictable_bytes += entry->asset->bytes;
    result.decode_count = decodes_;
    result.mask_build_count = mask_builds_;
    result.entries = entries_.size();
    return result;
}
} // namespace wvd::recognition
