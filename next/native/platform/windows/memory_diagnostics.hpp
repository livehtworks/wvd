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
    std::uint64_t private_bytes{}, sampled_peak_private_bytes{};
    std::uint64_t working_set_bytes{}, peak_working_set_bytes{};
    std::uint64_t commit_total_pages{}, commit_limit_pages{}, commit_peak_pages{};
    std::uint64_t physical_available_pages{}, page_size{};
};
MemorySample sample_memory() noexcept;

// 匹配前预留标量槽并打开句柄；低内存记录不生成 JSON 或复制异常长文本。
class MemoryDiagnostics {
  public:
    explicit MemoryDiagnostics(const std::filesystem::path &path,
                               std::uint64_t run_id = 0,
                               std::uint64_t generation = 0) noexcept;
    ~MemoryDiagnostics();
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
    std::array<Record, 4> records_{};
    HANDLE file_{INVALID_HANDLE_VALUE};
    std::mutex write_mutex_;
    std::atomic<bool> write_failed_{false};
    std::atomic<std::uint64_t> starts_{0};
    std::atomic<std::uint64_t> sampled_peak_private_{0};
    const std::uint64_t run_id_, generation_;
};
} // namespace wvd::platform
