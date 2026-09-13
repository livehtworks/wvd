#include "run_coordinator.hpp"

namespace wvd::runtime {
using namespace contracts;
using namespace std::chrono_literals;
namespace {
nlohmann::json frozen(const RunDefinition &d) {
    using J = nlohmann::json;
    J files = J::array(), actions = J::array(), permissions = J::array(), capabilities = J::array();
    for (const auto &f : d.initial.bundle.files)
        files.push_back({{"path", f.relative_path}, {"sha256", f.sha256}});
    for (const auto &[name, action] : d.initial.actions)
        actions.push_back(name);
    for (auto kind : d.policy.permissions)
        permissions.push_back(int(kind));
    for (auto kind : d.policy.capabilities)
        capabilities.push_back(int(kind));
    return {
        {"request_id", d.request_id},
        {"device_id", d.policy.device_id},
        {"game_id", d.policy.game_id},
        {"application_id", d.policy.application_id},
        {"pack_revision", d.policy.pack_revision},
        {"viewport", d.policy.viewport_id},
        {"recognition_size", {d.policy.recognition_size.width, d.policy.recognition_size.height}},
        {"max_frame_age_ms", d.policy.max_frame_age.count()},
        {"permissions", permissions},
        {"capabilities", capabilities},
        {"allowed_scenes", d.policy.allowed_scenes},
        {"entry", d.initial.entry},
        {"terminal", d.initial.terminal_node},
        {"time_limit_ms", d.initial.time_limit.count()},
        {"stop_timeout_ms", d.initial.stop_timeout.count()},
        {"custom_actions", actions},
        {"files", files},
        {"recovery_limit", d.recovery_limit},
        {"recovery_enabled", bool(d.recover)}};
}
} // namespace
RunCoordinator::RunCoordinator(std::filesystem::path root)
    : data_root_(std::move(root)), instance_id_(platform::unique_id()) {}
RunCoordinator::~RunCoordinator() {
    request_stop();
    if (supervisor_.joinable())
        supervisor_.join();
}
void RunCoordinator::validate(const RunDefinition &d, const devices::DeviceBackend &backend) {
    if (!backend.offline())
        throw std::runtime_error("REAL_DEVICE_NOT_ENABLED");
    const auto &p = d.policy;
    if (d.request_id.empty() || d.request_id.size() > 128 || p.device_id.empty() ||
        p.game_id.empty() || p.application_id.empty() || p.viewport_id.empty() ||
        p.pack_revision != d.initial.bundle.revision || p.recognition_size.width <= 0 ||
        p.recognition_size.height <= 0 || p.max_frame_age <= 0ms || d.initial.entry.empty() ||
        d.initial.terminal_node.empty() || d.initial.time_limit <= 0ms ||
        d.initial.stop_timeout <= 0ms || d.recovery_limit > 16)
        throw std::runtime_error("RUN_DEFINITION_INVALID");
}
RunSnapshot RunCoordinator::start(RunDefinition definition,
                                  std::shared_ptr<devices::DeviceBackend> backend) {
    if (!backend)
        throw std::runtime_error("DEVICE_BACKEND_REQUIRED");
    validate(definition, *backend);
    auto immutable = frozen(definition);
    std::unique_lock lock(mutex_);
    if (auto previous = requests_.find(definition.request_id); previous != requests_.end()) {
        if (previous->second.definition != immutable)
            throw std::runtime_error("IDEMPOTENCY_CONFLICT");
        return definition.request_id == last_request_ ? snapshot_ : previous->second.result;
    }
    if (active_)
        throw std::runtime_error("RUN_BUSY");
    // 不静默淘汰旧请求，否则迟到重发会变成新运行。达到显式有界上限后拒绝新增。
    if (requests_.size() >= 256)
        throw std::runtime_error("REQUEST_HISTORY_CAPACITY_EXCEEDED");
    if (supervisor_.joinable())
        supervisor_.join();
    auto lease = std::make_unique<platform::DeviceLease>(definition.policy.device_id);
    auto id = snapshot_.run_id + 1;
    auto store = std::make_unique<storage::RunStore>(data_root_, instance_id_, id, immutable);
    auto journal = std::make_unique<storage::EventJournal>(instance_id_, id);
    journal->emit(1, "run.preparing", {}, true);
    lease_ = std::move(lease);
    store_ = std::move(store);
    journal_ = std::move(journal);
    last_request_ = definition.request_id;
    snapshot_ = {id, 1, RunState::Preparing, "", false, false, 0, {}};
    requests_.emplace(last_request_, RequestRecord{std::move(immutable), snapshot_});
    stop_ = false;
    active_ = true;
    stopped_at_.reset();
    try {
        supervisor_ = std::thread(
            [this, definition = std::move(definition), backend = std::move(backend)]() mutable {
                drive(std::move(definition), std::move(backend));
            });
    } catch (...) {
        active_ = false;
        lease_.reset();
        snapshot_.state = RunState::Failed;
        snapshot_.quiescent = true;
        requests_.at(last_request_).result = snapshot_;
        throw;
    }
    return snapshot_;
}
void RunCoordinator::request_stop() {
    std::lock_guard lock(mutex_);
    if (!active_ || snapshot_.quiescent || stop_.exchange(true))
        return;
    stopped_at_ = std::chrono::steady_clock::now();
    if (session_)
        session_->request_stop();
    if (snapshot_.reason != "STOP_TIMEOUT")
        snapshot_.state = RunState::StopRequested;
    try {
        journal_->emit(snapshot_.generation, "run.stop_requested", {}, true);
    } catch (...) {
        snapshot_.reason = "CRITICAL_EVENT_CAPACITY_EXCEEDED";
    }
}
RunSnapshot RunCoordinator::snapshot() const {
    std::lock_guard lock(mutex_);
    auto result = snapshot_;
    if (session_) {
        auto current = session_->counts();
        result.inputs.attempted += current.attempted;
        result.inputs.accepted += current.accepted;
        result.inputs.rejected += current.rejected;
        result.inputs.backend_called += current.backend_called;
        result.inputs.cleanup_called += current.cleanup_called;
    }
    return result;
}
bool RunCoordinator::wait_for(std::chrono::milliseconds duration) {
    std::unique_lock lock(mutex_);
    return cv_.wait_for(lock, duration, [this] { return !active_; });
}
nlohmann::json RunCoordinator::events(std::uint64_t after) const {
    std::lock_guard lock(mutex_);
    return journal_ ? journal_->read(after) : nlohmann::json::object();
}
std::filesystem::path RunCoordinator::run_directory() const {
    std::lock_guard lock(mutex_);
    return store_ ? store_->directory() : std::filesystem::path{};
}
void RunCoordinator::drive(RunDefinition definition,
                           std::shared_ptr<devices::DeviceBackend> backend) noexcept {
    SessionResult result;
    try {
        auto next = definition.initial;
        std::size_t recovered = 0;
        while (true) {
            std::shared_ptr<ExecutionSession> session;
            {
                std::lock_guard lock(mutex_);
                auto policy = definition.policy;
                policy.pack_revision = next.bundle.revision;
                session = std::make_shared<ExecutionSession>(next, *backend, std::move(policy),
                                                             snapshot_.run_id, snapshot_.generation,
                                                             *journal_);
                session_ = session;
                if (stop_)
                    session->request_stop();
                else
                    snapshot_.state = RunState::Preparing;
                session->start();
            }
            const auto started = std::chrono::steady_clock::now();
            while (!session->wait_for(5ms)) {
                std::unique_lock lock(mutex_);
                const auto now = std::chrono::steady_clock::now();
                if (!stop_ && now - started > next.time_limit) {
                    stop_ = true;
                    stopped_at_ = now;
                    session->request_stop();
                    snapshot_.reason = "SESSION_TIME_LIMIT";
                    snapshot_.state = RunState::StopRequested;
                    journal_->emit(snapshot_.generation, "run.deadline_stop", {}, true);
                }
                if (stop_ && stopped_at_ && now - *stopped_at_ > next.stop_timeout &&
                    snapshot_.reason != "STOP_TIMEOUT") {
                    snapshot_.state = RunState::Failed;
                    snapshot_.reason = "STOP_TIMEOUT";
                    snapshot_.quiescent = false;
                    journal_->emit(snapshot_.generation, "run.stop_timeout", {{"quiescent", false}},
                                   true);
                    lock.unlock();
                    store_->save_events(*journal_);
                } else if (!stop_ && session->running())
                    snapshot_.state = RunState::Running;
            }
            result = session->join();
            session.reset();
            {
                std::lock_guard lock(mutex_);
                session_.reset();
                snapshot_.engine_status = result.engine_status;
                snapshot_.inputs.attempted += result.inputs.attempted;
                snapshot_.inputs.accepted += result.inputs.accepted;
                snapshot_.inputs.rejected += result.inputs.rejected;
                snapshot_.inputs.backend_called += result.inputs.backend_called;
                snapshot_.inputs.cleanup_called += result.inputs.cleanup_called;
                journal_->emit(snapshot_.generation, "session.quiescent",
                               {{"quiescent", result.quiescent}}, true);
            }
            store_->save_events(*journal_);
            if (!stop_ && result.end == SessionEnd::RecoveryRequired && definition.recover &&
                recovered < definition.recovery_limit) {
                {
                    std::lock_guard lock(mutex_);
                    snapshot_.state = RunState::Recovering;
                }
                auto decision = definition.recover(result);
                if (decision) {
                    if (decision->entry.empty() || decision->terminal_node.empty() ||
                        decision->time_limit <= 0ms || decision->stop_timeout <= 0ms)
                        throw std::runtime_error("RECOVERY_DEFINITION_INVALID");
                    next = std::move(*decision);
                    ++recovered;
                    std::lock_guard lock(mutex_);
                    ++snapshot_.generation;
                    journal_->emit(snapshot_.generation, "session.recovery_boundary", {}, true);
                    continue;
                }
            }
            break;
        }
        std::unique_lock lock(mutex_);
        // 原生侧已经静止，但结果尚未提交。磁盘刷新不占控制锁，也不提前发布 Completed。
        snapshot_.quiescent = result.quiescent;
        auto committed = snapshot_;
        committed.quiescent = result.quiescent;
        if (!committed.reason.empty())
            committed.state = RunState::Failed;
        else if (stop_) {
            committed.state = RunState::UserStopped;
            committed.reason = "USER_STOP";
        } else if (result.end == SessionEnd::Completed)
            committed.state = RunState::Completed;
        else if (result.end == SessionEnd::RecoveryRequired) {
            committed.state = RunState::Interrupted;
            committed.reason = "RECOVERY_REQUIRED";
        } else {
            committed.state = RunState::Failed;
            committed.reason = result.reason;
        }
        committed.result_saved = true;
        lock.unlock();
        journal_->commit_terminal(committed.generation, storage::snapshot_json(committed),
                                  [&](const nlohmann::json &events) {
                                      store_->save_terminal(committed, result, events);
                                  });
        lock.lock();
        snapshot_ = std::move(committed);
    } catch (const std::exception &error) {
        // 若后台会话仍未静止，保持所有权并继续等待；异常绝不能绕过 join 释放设备。
        std::shared_ptr<ExecutionSession> pending;
        {
            std::lock_guard lock(mutex_);
            snapshot_.state = RunState::Failed;
            snapshot_.reason = error.what();
            pending = session_;
        }
        if (pending) {
            pending->request_stop();
            result = pending->join();
        }
        std::lock_guard lock(mutex_);
        session_.reset();
        snapshot_.quiescent = true;
        snapshot_.result_saved = false;
    } catch (...) {
        std::shared_ptr<ExecutionSession> pending;
        {
            std::lock_guard lock(mutex_);
            snapshot_.state = RunState::Failed;
            snapshot_.reason = "COORDINATOR_EXCEPTION";
            pending = session_;
        }
        if (pending) {
            pending->request_stop();
            result = pending->join();
        }
        std::lock_guard lock(mutex_);
        session_.reset();
        snapshot_.quiescent = true;
    }
    {
        std::lock_guard lock(mutex_);
        requests_.at(last_request_).result = snapshot_;
        lease_.reset();
        active_ = false;
    }
    cv_.notify_all();
}
} // namespace wvd::runtime
