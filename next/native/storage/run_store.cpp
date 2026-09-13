#include "run_store.hpp"
#include "platform/windows/runtime_files.hpp"
#include <algorithm>
#include <fstream>

namespace wvd::storage {
using J = nlohmann::json;
namespace {
J event_record(const std::string &instance, std::uint64_t run, std::uint64_t generation,
               std::uint64_t seq, std::string type, J payload) {
    auto node = payload.is_object() ? payload.value("node", J(nullptr)) : J(nullptr);
    auto outcome = payload.is_object()
                       ? payload.value("outcome", payload.value("state", J(nullptr)))
                       : J(nullptr);
    return {{"server_instance_id", instance},
            {"run_id", run},
            {"session_generation", generation},
            {"seq", seq},
            {"monotonic_time", std::chrono::duration_cast<std::chrono::nanoseconds>(
                                   std::chrono::steady_clock::now().time_since_epoch())
                                   .count()},
            {"type", std::move(type)},
            {"node_id", std::move(node)},
            {"outcome", std::move(outcome)},
            {"payload", std::move(payload)}};
}
} // namespace
EventJournal::EventJournal(std::string instance, std::uint64_t run, std::size_t capacity)
    : instance_(std::move(instance)), run_(run), capacity_(capacity) {
    if (capacity < 8 || capacity > 65536)
        throw std::runtime_error("EVENT_CAPACITY_INVALID");
}
std::uint64_t EventJournal::emit(std::uint64_t generation, std::string type, J payload,
                                 bool critical) {
    if (type.size() > 256 || payload.dump().size() > 65536)
        throw std::runtime_error("EVENT_TOO_LARGE");
    std::lock_guard lock(mutex_);
    if (terminal_)
        throw std::runtime_error("JOURNAL_ALREADY_TERMINAL");
    const auto sequence = ++sequence_;
    if (events_.size() == capacity_) {
        auto discard = std::find_if(events_.begin(), events_.end(),
                                    [](const auto &event) { return !event.critical; });
        if (discard == events_.end()) {
            dropped_through_ = sequence;
            if (critical)
                throw std::runtime_error("CRITICAL_EVENT_CAPACITY_EXCEEDED");
            return sequence;
        }
        dropped_through_ = std::max(dropped_through_, discard->seq);
        events_.erase(discard);
    }
    events_.push_back(
        {sequence, critical,
         event_record(instance_, run_, generation, sequence, std::move(type), std::move(payload))});
    return sequence;
}
J EventJournal::read(std::uint64_t after) const {
    std::lock_guard lock(mutex_);
    J rows = J::array();
    for (const auto &event : events_)
        if (event.seq > after)
            rows.push_back(event.value);
    return {
        {"last_seq", sequence_}, {"resync_required", after < dropped_through_}, {"events", rows}};
}
void EventJournal::commit_terminal(std::uint64_t generation, J payload,
                                   const std::function<void(const J &)> &persist) {
    std::lock_guard lock(mutex_);
    if (terminal_)
        throw std::runtime_error("JOURNAL_ALREADY_TERMINAL");
    // 额外保留一个终态槽位；先构造完整提交内容，原子落盘成功后才发布到可读事件流。
    // 因而常规事件挤满、写盘失败或进程中断都不会产生未提交的 Completed 事件。
    auto committed = events_;
    const auto seq = sequence_ + 1;
    committed.push_back(
        {seq, true,
         event_record(instance_, run_, generation, seq, "run.terminal", std::move(payload))});
    J rows = J::array();
    for (const auto &event : committed)
        rows.push_back(event.value);
    persist(J{
        {"last_seq", seq}, {"resync_required", dropped_through_ > 0}, {"events", std::move(rows)}});
    events_.swap(committed);
    sequence_ = seq;
    terminal_ = true;
}
J snapshot_json(const contracts::RunSnapshot &s) {
    return {{"run_id", s.run_id},
            {"generation", s.generation},
            {"state", contracts::name(s.state)},
            {"reason", s.reason},
            {"quiescent", s.quiescent},
            {"result_saved", s.result_saved},
            {"engine_status", s.engine_status},
            {"inputs",
             {{"attempted", s.inputs.attempted},
              {"accepted", s.inputs.accepted},
              {"rejected", s.inputs.rejected},
              {"backend_called", s.inputs.backend_called},
              {"cleanup_called", s.inputs.cleanup_called}}}};
}
RunStore::RunStore(const std::filesystem::path &root, const std::string &instance,
                   std::uint64_t run, const J &definition) {
    // root 是调用者明确指定的新数据根；只新建本实例/本运行目录，既有同名目录不接管。
    auto parent = root / instance;
    std::filesystem::create_directories(parent);
    directory_ = parent / std::to_string(run);
    if (!std::filesystem::create_directory(directory_))
        throw std::runtime_error("RUN_DIRECTORY_EXISTS");
    platform::atomic_write(
        directory_ / "run.json",
        J{{"schema", 1}, {"instance", instance}, {"run_id", run}, {"definition", definition}}.dump(
            2),
        false);
}
void RunStore::save_events(const EventJournal &events) {
    platform::atomic_write(directory_ / "events.json", events.read().dump(2), true);
}
void RunStore::save_terminal(const contracts::RunSnapshot &snapshot,
                             const contracts::SessionResult &session, const J &events) {
    if (saved_)
        throw std::runtime_error("TERMINAL_ALREADY_SAVED");
    J document = snapshot_json(snapshot);
    document["result_saved"] = true;
    document["events"] = events;
    document["root_task_id"] = session.root_task_id;
    document["root_terminal"] = {{"task_id", session.terminal.task_id},
                                 {"generation", session.terminal.generation},
                                 {"depth", session.terminal.depth},
                                 {"node", session.terminal.node}};
    platform::atomic_write(directory_ / "result.json", document.dump(2), false);
    saved_ = true;
}
J RunStore::read_summary(const std::filesystem::path &directory) {
    std::ifstream run(directory / "run.json");
    J definition;
    if (!run || !(run >> definition) || definition.at("schema") != 1)
        throw std::runtime_error("RUN_RECORD_INVALID");
    if (!std::filesystem::exists(directory / "result.json"))
        return {{"state", "Interrupted"},
                {"reason", "NO_COMMITTED_TERMINAL"},
                {"resume_allowed", false}};
    std::ifstream result(directory / "result.json");
    J value;
    result >> value;
    return value;
}
} // namespace wvd::storage
