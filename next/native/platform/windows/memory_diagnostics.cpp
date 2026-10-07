#include "memory_diagnostics.hpp"
#include <cstdio>
#include <psapi.h>
#include <tlhelp32.h>
#include <algorithm>
#include <utility>

namespace wvd::platform {
namespace {
struct OwnerCounters { std::atomic<std::uint64_t> created{}, destroyed{}, live{}, ready{}; };
std::array<OwnerCounters, 3> owner_counters;
}
MemoryOwnerLifetime::MemoryOwnerLifetime(MemoryOwnerKind kind) noexcept : kind_(kind) {
    auto &c = owner_counters[static_cast<std::size_t>(kind_)];
    id_ = c.created.fetch_add(1) + 1;
    c.live.fetch_add(1);
}
MemoryOwnerLifetime::~MemoryOwnerLifetime() {
    auto &c = owner_counters[static_cast<std::size_t>(kind_)];
    if (ready_) c.ready.fetch_sub(1);
    c.live.fetch_sub(1);
    c.destroyed.fetch_add(1);
}
void MemoryOwnerLifetime::ready() noexcept {
    if (!ready_) { ready_ = true; owner_counters[static_cast<std::size_t>(kind_)].ready.fetch_add(1); }
}
std::array<LifetimeCounts, 3> MemoryOwnerLifetime::counts() noexcept {
    std::array<LifetimeCounts, 3> result;
    for (std::size_t i = 0; i < result.size(); ++i) {
        const auto &c = owner_counters[i];
        result[i] = {c.created.load(), c.destroyed.load(), c.live.load(), c.ready.load()};
    }
    return result;
}
MemoryOwners sample_memory_owners() noexcept {
    MemoryOwners result;
    const auto started = GetTickCount64();
    const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return result;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    result.available = Process32FirstW(snapshot, &entry) != FALSE;
    if (result.available) do {
        if (result.examined >= 4096 || GetTickCount64() - started >= 200) {
            result.truncated = true;
            break;
        }
        ++result.examined;
        const auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
            FALSE, entry.th32ProcessID);
        if (!process) { ++result.unreadable; continue; }
        PROCESS_MEMORY_COUNTERS_EX memory{};
        if (GetProcessMemoryInfo(process, reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory), sizeof(memory))) {
            MemoryOwner owner;
            owner.process_id = entry.th32ProcessID;
            owner.private_bytes = memory.PrivateUsage;
            owner.working_set_bytes = memory.WorkingSetSize;
            std::copy_n(entry.szExeFile, MAX_PATH, owner.name.begin());
            owner.name.back() = L'\0';
            FILETIME created{}, exited{}, kernel{}, user{};
            if (GetProcessTimes(process, &created, &exited, &kernel, &user))
                owner.created_100ns = (static_cast<std::uint64_t>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
            result.readable_private_bytes += owner.private_bytes;
            unsigned place = 0;
            while (place < result.count && result.top[place].private_bytes >= owner.private_bytes) ++place;
            if (place < result.top.size()) {
                result.count = std::min<std::uint32_t>(result.count + 1, static_cast<std::uint32_t>(result.top.size()));
                for (unsigned i = result.count - 1; i > place; --i) result.top[i] = result.top[i - 1];
                result.top[place] = owner;
            }
        } else ++result.unreadable;
        CloseHandle(process);
    } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    result.elapsed_ms = GetTickCount64() - started;
    return result;
}
MemorySample sample_memory() noexcept {
    MemorySample result;
    result.process_id = GetCurrentProcessId();
    DWORD handles{};
    if (GetProcessHandleCount(GetCurrentProcess(), &handles)) result.handle_count = handles;
    FILETIME created{}, exited{}, kernel{}, user{};
    if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
        result.process_created_100ns =
            (static_cast<std::uint64_t>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
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
HeapMaintenance optimize_idle_heap() noexcept {
    HeapMaintenance result;
    const auto heap_usage = []() noexcept {
        HeapMaintenance::Usage usage;
        // Resolve dynamically: older supported Windows builds lack this
        // diagnostic API. Absence is unavailable, never a zero-byte result.
        using Summary = BOOL(WINAPI *)(HANDLE, DWORD, LPHEAP_SUMMARY);
        const auto summary = reinterpret_cast<Summary>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "HeapSummary"));
        if (!summary) return usage;
        usage.available = true;
        LARGE_INTEGER frequency{}, started{}, ended{};
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&started);
        std::array<HANDLE, 64> heaps{};
        const auto count = GetProcessHeaps(static_cast<DWORD>(heaps.size()), heaps.data());
        if (!count || count > heaps.size()) { usage.failed = 1; return usage; }
        usage.heaps = count;
        for (DWORD index = 0; index < count; ++index) {
            HEAP_SUMMARY value{};
            value.cb = sizeof(value);
            if (!summary(heaps[index], 0, &value)) { ++usage.failed; continue; }
            usage.allocated += value.cbAllocated;
            usage.committed += value.cbCommitted;
            usage.reserved += value.cbReserved;
        }
        QueryPerformanceCounter(&ended);
        if (frequency.QuadPart > 0)
            usage.elapsed_us = static_cast<std::uint64_t>((ended.QuadPart - started.QuadPart) * 1000000 / frequency.QuadPart);
        usage.complete = usage.failed == 0;
        return usage;
    };
    result.heap_before = heap_usage();
    result.before = sample_memory();
    LARGE_INTEGER frequency{}, started{}, ended{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&started);
    HEAP_OPTIMIZE_RESOURCES_INFORMATION options{HEAP_OPTIMIZE_RESOURCES_CURRENT_VERSION, 0};
    result.succeeded = HeapSetInformation(nullptr, HeapOptimizeResources, &options, sizeof(options)) != FALSE;
    if (!result.succeeded) result.error = GetLastError();
    QueryPerformanceCounter(&ended);
    if (frequency.QuadPart > 0)
        result.elapsed_us = static_cast<std::uint64_t>((ended.QuadPart - started.QuadPart) * 1000000 / frequency.QuadPart);
    result.after = sample_memory();
    result.heap_after = heap_usage();
    return result;
}
MemoryDiagnostics::MemoryDiagnostics(const std::filesystem::path &path,
                                     std::uint64_t run_id,
                                     std::uint64_t generation, bool periodic_enabled,
                                     std::uint32_t interval_ms) noexcept
    : run_id_(run_id), generation_(generation), periodic_enabled_(periodic_enabled),
      interval_ms_(interval_ms) {
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
void MemoryDiagnostics::owner_boundary(const char *phase, const char *kind, std::uint64_t owner_id) noexcept {
    if (!periodic_enabled_ || file_ == INVALID_HANDLE_VALUE) return;
    const auto sample = sample_memory();
    char line[512];
    const int length = std::snprintf(line, sizeof(line),
        "level=debug category=memory event=owner_boundary run=%llu generation=%llu tick_ms=%llu kind=%s owner=%llu phase=%s private=%llu handles=%u process_ok=%d\r\n",
        static_cast<unsigned long long>(run_id_), static_cast<unsigned long long>(generation_),
        static_cast<unsigned long long>(GetTickCount64()), kind, static_cast<unsigned long long>(owner_id), phase,
        static_cast<unsigned long long>(sample.private_bytes), sample.handle_count, sample.process_ok);
    if (length < 0 || length >= static_cast<int>(sizeof(line))) { write_failed_ = true; return; }
    try {
        std::lock_guard lock(write_mutex_);
        DWORD written{};
        if (!WriteFile(file_, line, static_cast<DWORD>(length), &written, nullptr) ||
            written != static_cast<DWORD>(length)) write_failed_ = true;
    } catch (...) { write_failed_ = true; }
}
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
        if (periodic_enabled_ && now >= due &&
            next_sample_ms_.compare_exchange_strong(due, now + interval_ms_)) {
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
    char line[768];
    const int length = std::snprintf(line, sizeof(line),
        "level=%s category=memory event=%s run=%llu generation=%llu tick_ms=%llu src=%llu asset=%llu alg=%d frame=%dx%d roi=%dx%d templ=%dx%dx%d mask=%d gray=%d exclude=%d "
        "estimated_workspace_bytes=%llu active=%llu retained=%llu in_use=%llu process_ok=%d private=%llu sampled_peak_private=%llu "
        "working=%llu peak_working=%llu system_ok=%d commit=%llu limit=%llu peak_commit=%llu "
        "physical_available=%llu page_size=%llu opencv_code=%d\r\n",
        code ? "error" : "debug", event, static_cast<unsigned long long>(run_id_),
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
