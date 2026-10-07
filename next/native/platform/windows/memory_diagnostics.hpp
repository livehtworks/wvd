#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace wvd::platform {
struct MemorySample {
    bool process_ok{}, system_ok{};
    std::uint32_t process_id{}, handle_count{};
    std::uint64_t process_created_100ns{};
    std::uint64_t private_bytes{}, sampled_peak_private_bytes{};
    std::uint64_t working_set_bytes{}, peak_working_set_bytes{};
    std::uint64_t commit_total_pages{}, commit_limit_pages{}, commit_peak_pages{};
    std::uint64_t physical_available_pages{}, page_size{};
};
MemorySample sample_memory() noexcept;
struct HeapMaintenance {
    bool succeeded{};
    std::uint32_t error{};
    std::uint64_t elapsed_us{};
    MemorySample before, after;
    struct Usage {
        struct Heap {
            std::uint64_t address{}, allocated{}, committed{}, reserved{}, elapsed_us{};
            std::uint32_t error{};
            bool process_heap{}, complete{};
        };
        bool available{}, complete{};
        std::uint32_t heaps{}, failed{};
        std::uint64_t allocated{}, committed{}, reserved{}, elapsed_us{};
        std::array<Heap, 64> detail{};
    };
    Usage heap_before, heap_after;
};
// Call only at an idle ownership boundary, not from the recognition hot path.
// Windows may decommit free LFH pages; live allocations remain owned by callers.
HeapMaintenance optimize_idle_heap(bool measure = true) noexcept;
enum class MemoryOwnerKind { RecognitionService, OcrEngine, ExecutionSession };
struct LifetimeCounts { std::uint64_t created{}, destroyed{}, live{}, ready{}; };
class MemoryOwnerLifetime final {
  public:
    explicit MemoryOwnerLifetime(MemoryOwnerKind kind) noexcept;
    ~MemoryOwnerLifetime();
    MemoryOwnerLifetime(const MemoryOwnerLifetime &) = delete;
    MemoryOwnerLifetime &operator=(const MemoryOwnerLifetime &) = delete;
    void ready() noexcept;
    std::uint64_t id() const { return id_; }
    static std::array<LifetimeCounts, 3> counts() noexcept;
  private:
    MemoryOwnerKind kind_;
    std::uint64_t id_{};
    bool ready_{};
};
struct MemoryOwner {
    std::uint32_t process_id{};
    std::uint64_t created_100ns{}, private_bytes{}, working_set_bytes{};
    std::array<wchar_t, MAX_PATH> name{};
};
struct MemoryOwners {
    bool available{}, truncated{};
    std::uint32_t examined{}, unreadable{}, count{};
    std::uint64_t elapsed_ms{}, readable_private_bytes{};
    std::array<MemoryOwner, 8> top{};
};
// Bounded read-only process counters, never command lines or process termination.
MemoryOwners sample_memory_owners() noexcept;

// 匹配前预留标量槽并打开句柄；低内存记录不生成 JSON 或复制异常长文本。
class MemoryDiagnostics {
  public:
    explicit MemoryDiagnostics(const std::filesystem::path &path,
                               std::uint64_t run_id = 0,
                               std::uint64_t generation = 0,
                               bool periodic_enabled = true,
                               std::uint32_t interval_ms = 1000) noexcept;
    ~MemoryDiagnostics();
    void owner_boundary(const char *phase, const char *kind, std::uint64_t owner_id) noexcept;
    MemoryDiagnostics(const MemoryDiagnostics &) = delete;
    MemoryDiagnostics &operator=(const MemoryDiagnostics &) = delete;
    struct Context {
        std::uint64_t source_id{}, asset_id{}, estimated_workspace_bytes{};
        int image_width{}, image_height{}, roi_width{}, roi_height{};
        int template_width{}, template_height{}, channels{}, algorithm{};
        bool mask{}, gray{}, exclude{};
        std::uint64_t cache_retained{}, cache_in_use{};
        std::uint64_t active_matches{};
    };
    class Slot {
      public:
        Slot() = default;
        Slot(MemoryDiagnostics *owner, unsigned index) : owner_(owner), index_(index) {}
        Slot(const Slot &) = delete;
        Slot &operator=(const Slot &) = delete;
        Slot(Slot &&other) noexcept;
        ~Slot();
        void finish() noexcept;
        void failure(int opencv_code) noexcept;
      private:
        MemoryDiagnostics *owner_{};
        unsigned index_{};
    };
    Slot begin(const Context &context) noexcept;
    bool write_failed() const noexcept { return write_failed_.load(); }
  private:
    friend class Slot;
    struct Record { std::atomic<bool> busy{false}; Context context; MemorySample memory; };
    void write(unsigned index, const char *event, int code) noexcept;
    std::array<Record, 16> records_{}; // 最多4路匹配及嵌套解码的固定标量槽，不保存像素。
    HANDLE file_{INVALID_HANDLE_VALUE};
    std::mutex write_mutex_;
    std::atomic<bool> write_failed_{false};
    std::atomic<std::uint64_t> next_sample_ms_{0};
    std::atomic<std::uint64_t> sampled_peak_private_{0};
    const std::uint64_t run_id_, generation_;
    const bool periodic_enabled_;
    const std::uint32_t interval_ms_;
};
} // namespace wvd::platform
