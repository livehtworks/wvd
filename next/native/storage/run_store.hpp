#pragma once
#include "contracts/run.hpp"
#include "contracts/business_state.hpp"
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

namespace wvd::storage {
// 只有Context/Session填写归属；原因和业务ID永不参与路径拼接。
struct DiagnosticRequest {
    std::uint64_t run_id{}, generation{}, unit_index{};
    std::int64_t task_id{};
    int depth{};
    std::string node, reason, stage, operation_id, error;
};
struct DiagnosticLimits {
    std::size_t rewards{128}, failures{32}, frame_bytes{8 * 1024 * 1024};
};
class EventJournal {
  public:
    EventJournal(std::string instance, std::uint64_t run, std::size_t capacity = 256);
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
};
class RunStore {
  public:
    RunStore(const std::filesystem::path &root, const std::string &instance, std::uint64_t run,
             const nlohmann::json &frozen_definition,
             std::shared_ptr<const contracts::MonotonicClock> diagnostic_clock =
                 std::make_shared<contracts::SteadyClock>(), DiagnosticLimits limits = {});
    ~RunStore();
    nlohmann::json save_diagnostic(const contracts::FrameEnvelope *frame,
                                  const DiagnosticRequest &request);
    bool save_recent_frame(const contracts::FrameEnvelope &frame) noexcept;
    void finish_recent_frames() noexcept;
    nlohmann::json diagnostic_summary() const;
    void note_diagnostic_hook_failure() noexcept;
    void save_events(const EventJournal &events);
    void save_terminal(const contracts::RunSnapshot &snapshot,
                       const contracts::SessionResult &session,
                       const nlohmann::json &events = nlohmann::json::object());
    const std::filesystem::path &directory() const {
        return directory_;
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
    mutable std::mutex diagnostic_mutex_;
    std::map<std::string, contracts::MonotonicClock::TimePoint> diagnostic_times_;
    std::set<std::string> diagnostic_operations_;
    nlohmann::json diagnostic_entries_ = nlohmann::json::array();
    std::uint64_t diagnostic_rewards_{}, diagnostic_failures_{}, diagnostic_bytes_{},
        diagnostic_failed_{}, diagnostic_throttled_{}, diagnostic_duplicates_{}, diagnostic_quota_{},
        diagnostic_unavailable_{}, diagnostic_unrecorded_{};
    bool diagnostic_closed_{}, diagnostic_directory_created_{};
    std::uint64_t diagnostic_directory_id_{};
    unsigned long diagnostic_volume_{};
    // RunStore 唯一所有者：最多一个待处理帧和一个在途帧，丢弃仅影响辅助历史图。
    std::mutex recent_mutex_;
    std::condition_variable recent_wake_;
    std::optional<contracts::FrameEnvelope> recent_pending_;
    bool recent_closed_{};
    std::atomic<std::uint64_t> recent_saved_{}, recent_dropped_{}, recent_failed_{}, recent_work_ns_{};
    std::jthread recent_worker_;
    void recent_loop() noexcept;
    bool write_recent_frame(const contracts::FrameEnvelope &frame);
};
nlohmann::json snapshot_json(const contracts::RunSnapshot &snapshot);
} // namespace wvd::storage
