#include "memory_diagnostics.hpp"
#include <cstdio>
#include <psapi.h>
#include <utility>

namespace wvd::platform {
MemorySample sample_memory() noexcept {
    MemorySample result;
    PROCESS_MEMORY_COUNTERS_EX process{};
    if (GetProcessMemoryInfo(GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&process), sizeof(process))) {
        result.process_ok = true;
        result.private_bytes = process.PrivateUsage;
        result.working_set_bytes = process.WorkingSetSize;
        result.peak_working_set_bytes = process.PeakWorkingSetSize;
    }
    PERFORMANCE_INFORMATION system{};
    system.cb = sizeof(system);
    if (GetPerformanceInfo(&system, sizeof(system))) {
        result.system_ok = true;
        result.commit_total_pages = system.CommitTotal;
        result.commit_limit_pages = system.CommitLimit;
        result.commit_peak_pages = system.CommitPeak;
        result.physical_available_pages = system.PhysicalAvailable;
        result.page_size = system.PageSize;
    }
    return result;
}
MemoryDiagnostics::MemoryDiagnostics(const std::filesystem::path &path,
                                     std::uint64_t run_id,
                                     std::uint64_t generation) noexcept
    : run_id_(run_id), generation_(generation) {
    if (!path.empty())
        file_ = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (!path.empty() && file_ == INVALID_HANDLE_VALUE) write_failed_ = true;
}
MemoryDiagnostics::~MemoryDiagnostics() {
    if (file_ != INVALID_HANDLE_VALUE) CloseHandle(file_);
}
MemoryDiagnostics::Slot::Slot(Slot &&other) noexcept
    : owner_(std::exchange(other.owner_, nullptr)), index_(other.index_) {}
MemoryDiagnostics::Slot::~Slot() { finish(); }
void MemoryDiagnostics::Slot::finish() noexcept {
    if (!owner_) return;
    owner_->records_[index_].busy.store(false);
    owner_ = nullptr;
}
void MemoryDiagnostics::Slot::failure(int opencv_code) noexcept {
    if (!owner_) return;
    // 故障记录使用故障处理时的新样本，不再把准入排队前的 begin 样本称为 OOM 现场。
    auto &memory = owner_->records_[index_].memory;
    memory = sample_memory();
    if (memory.process_ok) {
        auto previous = owner_->sampled_peak_private_.load();
        while (previous < memory.private_bytes &&
               !owner_->sampled_peak_private_.compare_exchange_weak(previous, memory.private_bytes)) {}
        memory.sampled_peak_private_bytes = owner_->sampled_peak_private_.load();
    }
    owner_->write(index_, "resource_failure", opencv_code);
}
MemoryDiagnostics::Slot MemoryDiagnostics::begin(const Context &context) noexcept {
    for (unsigned i = 0; i < records_.size(); ++i) {
        bool free = false;
        if (!records_[i].busy.compare_exchange_strong(free, true)) continue;
        records_[i].context = context;
        records_[i].memory = {};
        const auto now = static_cast<std::uint64_t>(GetTickCount64());
        auto due = next_sample_ms_.load();
        // 采样限频仅控制诊断开销，不参与识别、输入许可和页面等待判定。
        if (now >= due && next_sample_ms_.compare_exchange_strong(due, now + 1000)) {
            records_[i].memory = sample_memory();
            if (records_[i].memory.process_ok) {
                auto previous = sampled_peak_private_.load();
                const auto current = records_[i].memory.private_bytes;
                while (previous < current &&
                       !sampled_peak_private_.compare_exchange_weak(previous, current)) {}
                records_[i].memory.sampled_peak_private_bytes = sampled_peak_private_.load();
            }
            write(i, "periodic_begin", 0);
        }
        return Slot(this, i);
    }
    write_failed_ = true;
    return {};
}
void MemoryDiagnostics::write(unsigned index, const char *event, int code) noexcept {
    if (file_ == INVALID_HANDLE_VALUE) return;
    const auto &record = records_[index];
    const auto &c = record.context;
    const auto &m = record.memory;
    char line[640];
    const int length = std::snprintf(line, sizeof(line),
        "%s run=%llu generation=%llu tick_ms=%llu src=%llu asset=%llu alg=%d frame=%dx%d roi=%dx%d templ=%dx%dx%d mask=%d gray=%d exclude=%d "
        "estimated_workspace_bytes=%llu active=%llu retained=%llu in_use=%llu process_ok=%d private=%llu sampled_peak_private=%llu "
        "working=%llu peak_working=%llu system_ok=%d commit=%llu limit=%llu peak_commit=%llu "
        "physical_available=%llu page_size=%llu opencv_code=%d\r\n",
        event, static_cast<unsigned long long>(run_id_),
        static_cast<unsigned long long>(generation_),
        static_cast<unsigned long long>(GetTickCount64()),
        static_cast<unsigned long long>(c.source_id),
        static_cast<unsigned long long>(c.asset_id), c.algorithm,
        c.image_width, c.image_height, c.roi_width, c.roi_height,
        c.template_width, c.template_height, c.channels, c.mask, c.gray, c.exclude,
        static_cast<unsigned long long>(c.estimated_workspace_bytes),
        static_cast<unsigned long long>(c.active_matches),
        static_cast<unsigned long long>(c.cache_retained),
        static_cast<unsigned long long>(c.cache_in_use), m.process_ok,
        static_cast<unsigned long long>(m.private_bytes),
        static_cast<unsigned long long>(m.sampled_peak_private_bytes),
        static_cast<unsigned long long>(m.working_set_bytes),
        static_cast<unsigned long long>(m.peak_working_set_bytes), m.system_ok,
        static_cast<unsigned long long>(m.commit_total_pages),
        static_cast<unsigned long long>(m.commit_limit_pages),
        static_cast<unsigned long long>(m.commit_peak_pages),
        static_cast<unsigned long long>(m.physical_available_pages),
        static_cast<unsigned long long>(m.page_size), code);
    if (length < 0 || length >= static_cast<int>(sizeof(line))) { write_failed_ = true; return; }
    try {
        std::lock_guard lock(write_mutex_);
        DWORD written{};
        if (!WriteFile(file_, line, static_cast<DWORD>(length), &written, nullptr) ||
            written != static_cast<DWORD>(length)) write_failed_ = true;
    } catch (...) {
        write_failed_ = true; // noexcept 诊断边界不能反过来终止运行线程。
    }
}
} // namespace wvd::platform
