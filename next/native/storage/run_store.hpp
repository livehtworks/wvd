#pragma once
#include "contracts/run.hpp"
#include "contracts/business_state.hpp"
#include "logging_policy.hpp"
#include <deque>
#include <filesystem>
#include <functional>
#include <json.hpp>
#include <mutex>
#include <map>
#include <set>
#include <atomic>
#include <condition_variable>
#include <optional>
#include <thread>
#include <fstream>

namespace wvd::storage {
// 只有Context/Session填写归属；原因和业务ID永不参与路径拼接。
struct DiagnosticRequest {
    std::uint64_t run_id{}, generation{}, unit_index{};
    std::int64_t task_id{};
    int depth{};
    std::string node, reason, stage, operation_id, error;
    std::string evidence_kind{"captured_frame"};
    // 有界动作证据按同一操作去重/节流；不同操作仍共享原失败数量和单帧预算。
    bool operation_scoped{};
    nlohmann::json context = nlohmann::json::object();
};
struct DiagnosticLimits {
    std::size_t rewards{128}, failures{32}, frame_bytes{8 * 1024 * 1024};
};
class EventJournal {
  public:
    EventJournal(std::string instance, std::uint64_t run, std::size_t capacity = 256,
                 std::function<void(const nlohmann::json &)> event_sink = {});
    std::uint64_t emit(std::uint64_t generation, std::string type, nlohmann::json payload = {},
                       bool critical = false);
    nlohmann::json read(std::uint64_t after = 0) const;
    void commit_terminal(std::uint64_t generation, nlohmann::json payload,
                         const std::function<void(const nlohmann::json &)> &persist);

  private:
    struct Event {
        std::uint64_t seq;
        bool critical;
        nlohmann::json value;
    };
    std::string instance_;
    std::uint64_t run_{}, sequence_{}, dropped_through_{};
    std::size_t capacity_{};
    mutable std::mutex mutex_;
    std::deque<Event> events_;
    bool terminal_{};
    bool committing_{};
    std::function<void(const nlohmann::json &)> event_sink_;
};
class RunStore {
  public:
    RunStore(const std::filesystem::path &root, const std::string &instance, std::uint64_t run,
             const nlohmann::json &frozen_definition,
             std::shared_ptr<const contracts::MonotonicClock> diagnostic_clock =
                 std::make_shared<contracts::SteadyClock>(), DiagnosticLimits limits = {},
             LoggingPolicy logging = {});
    ~RunStore();
    nlohmann::json save_diagnostic(const contracts::FrameEnvelope *frame,
                                  const DiagnosticRequest &request,
                                  const contracts::DiagnosticPixels *pixels = nullptr);
    bool save_recent_frame(const contracts::FrameEnvelope &frame, bool action_evidence = false) noexcept;
    void finish_recent_frames() noexcept;
    nlohmann::json diagnostic_summary() const;
    void note_diagnostic_hook_failure() noexcept;
    void save_events(const EventJournal &events);
    void append_timing(std::uint64_t generation, const std::string &type,
                       const nlohmann::json &payload) noexcept;
    void append_log(std::uint64_t generation, LogLevel level, const char *category,
                    const char *type, const nlohmann::json &payload) noexcept;
    void append_event(const nlohmann::json &event) noexcept;
    // 终态之后的生命周期证据独立保存，不能追加到已冻结行数的diagnostics.jsonl。
    void record_memory_boundary(const std::string &phase, const nlohmann::json &sample) noexcept;
    void save_terminal(const contracts::RunSnapshot &snapshot,
                       const contracts::SessionResult &session,
                       const nlohmann::json &events = nlohmann::json::object());
    const std::filesystem::path &directory() const {
        return directory_;
    }
    bool memory_logging_enabled() const noexcept {
        return logging_.memory && logging_.accepts(LogLevel::Info);
    }
    static nlohmann::json read_summary(const std::filesystem::path &directory);

  private:
    std::filesystem::path directory_;
    std::filesystem::path recent_directory_;
    contracts::MonotonicClock::TimePoint recent_last_{};
    bool saved_{};
    const std::string instance_;
    const std::uint64_t run_;
    const nlohmann::json definition_;
    const std::shared_ptr<const contracts::MonotonicClock> diagnostic_clock_;
    const DiagnosticLimits diagnostic_limits_;
    const LoggingPolicy logging_;
    mutable std::mutex diagnostic_mutex_;
    std::map<std::string, contracts::MonotonicClock::TimePoint> diagnostic_times_;
    std::set<std::string> diagnostic_operations_;
    std::set<std::string> diagnostic_scoped_operations_;
    nlohmann::json diagnostic_entries_ = nlohmann::json::array();
    std::uint64_t diagnostic_rewards_{}, diagnostic_failures_{}, diagnostic_bytes_{},
        diagnostic_failed_{}, diagnostic_throttled_{}, diagnostic_duplicates_{}, diagnostic_quota_{},
        diagnostic_unavailable_{}, diagnostic_unrecorded_{};
    bool diagnostic_closed_{}, diagnostic_directory_created_{};
    std::uint64_t diagnostic_directory_id_{};
    // 与结果同一RunStore持有，只由协调工作线程写入；不新增日志线程或全局缓存。
    std::ofstream timing_stream_;
    std::uint64_t timing_bytes_{}, timing_rows_{}, timing_dropped_{}, timing_failed_{};
    std::uint64_t timing_write_ns_{};
    bool timing_closed_{};
    std::ofstream log_stream_;
    std::uint64_t log_bytes_{}, log_rows_{}, log_failed_{}, log_dropped_{};
    bool log_closed_{};
    std::ofstream event_stream_;
    std::uint64_t event_bytes_{}, event_rows_{}, event_failed_{}, event_dropped_{};
    bool event_closed_{};
    nlohmann::json memory_boundaries_ = nlohmann::json::object();
    std::uint64_t memory_boundary_failed_{};
    unsigned long diagnostic_volume_{};
    // 同一个历史图线程：最多四张待处理原帧；动作证据不受周期采样间隔限制。
    std::mutex recent_mutex_;
    std::condition_variable recent_wake_;
    struct RecentFrame { contracts::FrameEnvelope frame; bool action_evidence{}; };
    std::deque<RecentFrame> recent_pending_;
    std::uint64_t recent_action_generation_{}, recent_action_frame_{};
    bool recent_closed_{};
    std::atomic<std::uint64_t> recent_saved_{}, recent_dropped_{}, recent_failed_{}, recent_work_ns_{};
    std::jthread recent_worker_;
    void recent_loop() noexcept;
    bool write_recent_frame(const contracts::FrameEnvelope &frame, bool action_evidence);
};
nlohmann::json snapshot_json(const contracts::RunSnapshot &snapshot);
} // namespace wvd::storage
