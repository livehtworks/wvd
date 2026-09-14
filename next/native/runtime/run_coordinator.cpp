#include "run_coordinator.hpp"
#include <algorithm>

namespace wvd::runtime {
using namespace contracts;
using namespace std::chrono_literals;
namespace {
nlohmann::json frozen(const RunDefinition &d, const BehaviorRegistry &registry,
                      std::size_t capacity) {
    using J = nlohmann::json;
    J permissions = J::array(), capabilities = J::array();
    for (auto kind : d.policy.permissions)
        permissions.push_back(int(kind));
    for (auto kind : d.policy.capabilities)
        capabilities.push_back(int(kind));
    auto result = session_definition_json(d.initial);
    result.update(
        {{"definition_version", 3},
         {"registry", registry.manifest()},
         {"request_id", d.request_id},
         {"device_id", d.policy.device_id},
         {"game_id", d.policy.game_id},
         {"application_id", d.policy.application_id},
         {"viewport", d.policy.viewport_id},
         {"observed_read_only_viewport", d.policy.observed_read_only_viewport},
         {"recognition_size", {d.policy.recognition_size.width, d.policy.recognition_size.height}},
         {"max_frame_age_ms", d.policy.max_frame_age.count()},
         {"permissions", permissions},
         {"capabilities", capabilities},
         {"allowed_scenes", d.policy.allowed_scenes},
         {"recovery_limit", d.recovery_limit},
         {"recovery", d.recover ? binding_json(*d.recover) : J(nullptr)},
         {"event_capacity", capacity}});
    result["state_factory"] = d.state_factory ? binding_json(*d.state_factory) : J(nullptr);
    result["max_business_units"] = d.max_business_units;
    result["continuation_units"] = J::array();
    for (const auto &unit : d.continuation_units)
        result["continuation_units"].push_back(session_definition_json(unit));
    return result;
}
void remember_secondary(RunSnapshot &state, const std::string &reason) {
    if (reason.empty() || reason == state.reason ||
        std::find(state.secondary_errors.begin(), state.secondary_errors.end(), reason) !=
            state.secondary_errors.end())
        return;
    if (state.secondary_errors.size() < 32)
        state.secondary_errors.push_back(reason);
}
} // namespace
RunCoordinator::RunCoordinator(std::filesystem::path root,
                               std::shared_ptr<const BehaviorRegistry> registry,
                               std::size_t event_capacity,
                               std::shared_ptr<const contracts::MonotonicClock> clock)
    : data_root_(std::move(root)), instance_id_(platform::unique_id()),
      registry_(std::move(registry)), event_capacity_(event_capacity), clock_(std::move(clock)) {
    if (!registry_ || !registry_->sealed())
        throw std::runtime_error("REGISTRY_NOT_SEALED");
    if (event_capacity < 8 || event_capacity > 65536)
        throw std::runtime_error("EVENT_CAPACITY_INVALID");
    if (!clock_)
        throw std::runtime_error("MONOTONIC_CLOCK_REQUIRED");
}
RunCoordinator::~RunCoordinator() {
    request_stop();
    if (supervisor_.joinable())
        supervisor_.join();
}
void RunCoordinator::validate(const RunDefinition &d, const devices::DeviceBackend &backend) const {
    if (!backend.offline() && !backend.verified_access())
        throw std::runtime_error("REAL_DEVICE_NOT_ENABLED");
    const auto &p = d.policy;
    if (p.observed_read_only_viewport &&
        (!p.permissions.empty() || !p.capabilities.empty() || !p.allowed_scenes.empty()))
        throw std::runtime_error("READ_ONLY_VIEWPORT_WITH_INPUT_POLICY");
    if (d.request_id.empty() || d.request_id.size() > 128 || p.device_id.empty() ||
        p.game_id.empty() || p.application_id.empty() || p.viewport_id.empty() ||
        p.pack_revision != d.initial.bundle.revision || p.recognition_size.width <= 0 ||
        p.recognition_size.height <= 0 || p.max_frame_age <= 0ms || d.initial.entry.empty() ||
        d.initial.terminal_node.empty() || d.initial.time_limit <= 0ms ||
        d.initial.stop_timeout <= 0ms || d.recovery_limit > 16)
        throw std::runtime_error("RUN_DEFINITION_INVALID");
    registry_->validate(d.initial);
    if (d.recover)
        registry_->validate_recovery(*d.recover);
    if (!d.max_business_units || d.max_business_units > 256 ||
        d.continuation_units.size() + 1 > d.max_business_units)
        throw std::runtime_error("BUSINESS_UNIT_BUDGET_INVALID");
    if (!d.state_factory && (!d.continuation_units.empty() || !d.initial.checkpoint_node.empty()))
        throw std::runtime_error("BUSINESS_STATE_REQUIRED");
    if (d.state_factory) {
        registry_->validate_state_factory(*d.state_factory);
        auto validate_unit = [&](const SessionDefinition &unit) {
            if (unit.checkpoint_node.empty() || unit.entry.empty() || unit.terminal_node.empty() ||
                unit.time_limit <= 0ms || unit.stop_timeout <= 0ms ||
                unit.bundle.revision != d.initial.bundle.revision)
                throw std::runtime_error("BUSINESS_UNIT_INVALID");
            registry_->validate(unit);
        };
        validate_unit(d.initial);
        for (const auto &unit : d.continuation_units)
            validate_unit(unit);
    }
}
RunSnapshot RunCoordinator::start(RunDefinition definition,
                                  std::shared_ptr<devices::DeviceBackend> backend) {
    std::lock_guard starting(start_mutex_);
    if (!backend)
        throw std::runtime_error("DEVICE_BACKEND_REQUIRED");
    validate(definition, *backend);
    auto immutable = frozen(definition, *registry_, event_capacity_);
    std::unique_lock lock(mutex_);
    if (auto previous = requests_.find(definition.request_id); previous != requests_.end()) {
        if (previous->second.definition != immutable)
            throw std::runtime_error("IDEMPOTENCY_CONFLICT");
        return definition.request_id == last_request_ ? snapshot_ : previous->second.result;
    }
    if (active_)
        throw std::runtime_error("RUN_BUSY");
    if (requests_.size() >= 256)
        throw std::runtime_error("REQUEST_HISTORY_CAPACITY_EXCEEDED");
    // start 单独串行化；等待上一监督线程退出时不占用停止/快照命令锁。
    lock.unlock();
    if (supervisor_.joinable())
        supervisor_.join();
    lock.lock();
    const auto id = snapshot_.run_id + 1;
    // 目录尚未建立时 start 未成立；文件 I/O 不阻塞快照和停止命令。
    lock.unlock();
    auto lease = std::make_unique<platform::DeviceLease>(definition.policy.device_id);
    auto journal = std::make_shared<storage::EventJournal>(instance_id_, id, event_capacity_);
    journal->emit(1, "run.preparing", {}, true);
    auto store = std::make_unique<storage::RunStore>(data_root_, instance_id_, id, immutable);
    lock.lock();
    lease_ = std::move(lease);
    store_ = std::move(store);
    journal_ = std::move(journal);
    last_request_ = definition.request_id;
    snapshot_ = {id, 1, RunState::Preparing, "", false, false, 0, {}};
    requests_.emplace(last_request_, RequestRecord{std::move(immutable), snapshot_});
    stop_ = false;
    active_ = true;
    stopped_at_.reset();
    collected_generation_ = 0;
    last_result_ = {};
    business_.reset();
    current_definition_ = definition.initial;
    try {
        supervisor_ = std::thread(
            [this, definition = std::move(definition), backend = std::move(backend)]() mutable {
                drive(std::move(definition), std::move(backend));
            });
    } catch (const std::exception &e) {
        lock.unlock();
        record_failure("RUN_THREAD_START_FAILED");
        record_failure(e.what());
        finish();
        return snapshot();
    } catch (...) {
        lock.unlock();
        record_failure("RUN_THREAD_START_FAILED");
        finish();
        return snapshot();
    }
    return snapshot_;
}
void RunCoordinator::record_failure(const std::string &reason) {
    std::lock_guard lock(mutex_);
    if (snapshot_.reason.empty())
        snapshot_.reason = reason;
    else
        remember_secondary(snapshot_, reason);
    if (reason.starts_with("STORAGE_"))
        snapshot_.storage_error = reason;
    snapshot_.state = RunState::Failed;
}
void RunCoordinator::request_stop() {
    std::lock_guard lock(mutex_);
    if (!active_ || snapshot_.quiescent || stop_.exchange(true))
        return;
    stopped_at_ = std::chrono::steady_clock::now();
    if (session_)
        session_->request_stop();
    if (snapshot_.reason.empty())
        snapshot_.state = RunState::StopRequested;
    // 仅追加内存事件，与终态快照串行；这里不进行磁盘写入。
    try {
        journal_->emit(snapshot_.generation, "run.stop_requested", {}, true);
    } catch (const std::exception &e) {
        if (snapshot_.reason.empty())
            snapshot_.reason = e.what();
        else
            remember_secondary(snapshot_, e.what());
        snapshot_.state = RunState::Failed;
    }
}
RunSnapshot RunCoordinator::snapshot() const {
    std::lock_guard lock(mutex_);
    auto result = snapshot_;
    if (session_ && collected_generation_ != snapshot_.generation) {
        const auto current = session_->counts();
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
    std::shared_ptr<storage::EventJournal> journal;
    {
        std::lock_guard lock(mutex_);
        journal = journal_;
    }
    return journal ? journal->read(after) : nlohmann::json::object();
}
std::filesystem::path RunCoordinator::run_directory() const {
    std::lock_guard lock(mutex_);
    return store_ ? store_->directory() : std::filesystem::path{};
}
void RunCoordinator::wait_session(const std::shared_ptr<ExecutionSession> &session,
                                  const SessionDefinition &definition, bool report_events) {
    const auto started = std::chrono::steady_clock::now();
    bool timeout_reported = false;
    while (!session->wait_for(5ms)) {
        bool deadline = false, timed_out = false;
        std::uint64_t generation{};
        {
            std::lock_guard lock(mutex_);
            generation = snapshot_.generation;
            const auto now = std::chrono::steady_clock::now();
            if (!stop_ && now - started > definition.time_limit) {
                stop_ = true;
                stopped_at_ = now;
                session->request_stop();
                if (snapshot_.reason.empty())
                    snapshot_.reason = "SESSION_TIME_LIMIT";
                snapshot_.state = RunState::StopRequested;
                deadline = true;
            }
            if (stop_ && stopped_at_ && now - *stopped_at_ > definition.stop_timeout &&
                !timeout_reported) {
                // 超时是未能正常静止，不以稍后的成功或 UserStopped 覆盖它。
                if (snapshot_.reason.empty() || snapshot_.reason == "SESSION_TIME_LIMIT") {
                    const auto previous = snapshot_.reason;
                    snapshot_.reason = "STOP_TIMEOUT";
                    remember_secondary(snapshot_, previous);
                } else
                    remember_secondary(snapshot_, "STOP_TIMEOUT");
                snapshot_.state = RunState::Failed;
                snapshot_.quiescent = false;
                timeout_reported = true;
                timed_out = true;
            } else if (!stop_ && snapshot_.reason.empty() && session->running())
                snapshot_.state = RunState::Running;
        }
        if (report_events && deadline)
            journal_->emit(generation, "run.deadline_stop", {}, true);
        if (report_events && timed_out) {
            journal_->emit(generation, "run.stop_timeout", {{"quiescent", false}}, true);
            store_->save_events(*journal_);
        }
    }
}
void RunCoordinator::collect_session(const std::shared_ptr<ExecutionSession> &session) {
    const auto result = session->join();
    if (!result.quiescent)
        throw std::runtime_error("SESSION_NOT_QUIESCENT");
    {
        std::lock_guard lock(mutex_);
        if (collected_generation_ != snapshot_.generation) {
            snapshot_.inputs.attempted += result.inputs.attempted;
            snapshot_.inputs.accepted += result.inputs.accepted;
            snapshot_.inputs.rejected += result.inputs.rejected;
            snapshot_.inputs.backend_called += result.inputs.backend_called;
            snapshot_.inputs.cleanup_called += result.inputs.cleanup_called;
            collected_generation_ = snapshot_.generation;
            last_result_ = result;
            snapshot_.engine_status = result.engine_status;
            snapshot_.sessions.push_back(
                {{"generation", snapshot_.generation},
                 {"definition", session_definition_json(current_definition_, false)},
                 {"engine_status", result.engine_status},
                 {"reason", result.reason},
                 {"quiescent", result.quiescent}});
            if (business_) {
                snapshot_.business = business_->summary();
                snapshot_.sessions.back()["business"] = snapshot_.business;
                snapshot_.sessions.back()["checkpoint"] = {
                    {"task_id", result.checkpoint.task_id},
                    {"generation", result.checkpoint.generation},
                    {"depth", result.checkpoint.depth},
                    {"node", result.checkpoint.node}};
            }
        }
        session_.reset();
    }
    if (result.end == SessionEnd::Failed)
        record_failure(result.reason);
}
void RunCoordinator::drive(RunDefinition definition,
                           std::shared_ptr<devices::DeviceBackend> backend) noexcept {
    try {
        auto next = definition.initial;
        std::size_t recovered = 0;
        std::size_t unit_index = 0;
        auto boundary = SegmentBoundary::Initial;
        if (definition.state_factory)
            business_ = registry_->create_state(*definition.state_factory,
                                                {instance_id_, snapshot().run_id, clock_});
        while (true) {
            if (stop_)
                break;
            if (business_)
                business_->enter_segment(boundary, snapshot().generation, unit_index);
            auto policy = definition.policy;
            policy.pack_revision = next.bundle.revision;
            auto session = std::make_shared<ExecutionSession>(
                next, *backend, std::move(policy), snapshot().run_id, snapshot().generation,
                *journal_, registry_, business_.get());
            {
                std::lock_guard lock(mutex_);
                current_definition_ = next;
                if (stop_)
                    break;
                session_ = session;
                snapshot_.state = RunState::Preparing;
                // start 只创建工作线程，不等待 SDK。与 request_stop 同锁建立唯一先后顺序。
                session->start();
            }
            wait_session(session, next, true);
            collect_session(session);
            journal_->emit(snapshot().generation, "session.quiescent", {{"quiescent", true}}, true);
            store_->save_events(*journal_);
            if (last_result_.end == SessionEnd::Completed && business_) {
                {
                    std::lock_guard lock(mutex_);
                    ++snapshot_.completed_business_units;
                }
                if (!stop_ && unit_index < definition.continuation_units.size()) {
                    next = definition.continuation_units.at(unit_index++);
                    std::uint64_t generation;
                    {
                        std::lock_guard lock(mutex_);
                        generation = ++snapshot_.generation;
                    }
                    boundary = SegmentBoundary::Continuation;
                    journal_->emit(generation, "session.continuation_boundary",
                                   {{"unit_index", unit_index},
                                    {"definition", session_definition_json(next, false)},
                                    {"business", business_->summary()}},
                                   true);
                    continue;
                }
            }
            if (!stop_ && last_result_.end == SessionEnd::RecoveryRequired && definition.recover &&
                recovered < definition.recovery_limit) {
                {
                    std::lock_guard lock(mutex_);
                    snapshot_.state = RunState::Recovering;
                }
                auto decision = registry_->recover(*definition.recover, last_result_, next);
                if (decision) {
                    if (decision->entry.empty() || decision->terminal_node.empty() ||
                        decision->time_limit <= 0ms || decision->stop_timeout <= 0ms)
                        throw std::runtime_error("RECOVERY_DEFINITION_INVALID");
                    next = std::move(*decision);
                    if (business_ && (next.bundle.revision != definition.initial.bundle.revision ||
                                      next.checkpoint_node.empty()))
                        throw std::runtime_error("BUSINESS_RECOVERY_DEFINITION_INVALID");
                    boundary = SegmentBoundary::Recovery;
                    ++recovered;
                    std::uint64_t generation;
                    {
                        std::lock_guard lock(mutex_);
                        generation = ++snapshot_.generation;
                    }
                    journal_->emit(generation, "session.recovery_boundary",
                                   {{"definition", session_definition_json(next, false)}}, true);
                    continue;
                }
            }
            break;
        }
    } catch (const std::exception &e) {
        record_failure(e.what());
    } catch (...) {
        record_failure("COORDINATOR_EXCEPTION");
    }
    finish();
}
void RunCoordinator::finish() noexcept {
    // 所有正常/异常出口只到这里一次。先真实静止，再按 generation 汇总，最后只提交一次。
    std::shared_ptr<ExecutionSession> pending;
    {
        std::lock_guard lock(mutex_);
        pending = session_;
    }
    if (pending) {
        {
            std::lock_guard lock(mutex_);
            stop_ = true;
            if (!stopped_at_)
                stopped_at_ = std::chrono::steady_clock::now();
        }
        pending->request_stop();
        wait_session(pending, current_definition_, false);
        try {
            collect_session(pending);
        } catch (const std::exception &error) {
            record_failure(error.what());
            // 无法证明静止时保留所有权，不提交一个假终态。
            return;
        }
        pending.reset();
    }
    RunSnapshot candidate;
    {
        std::lock_guard lock(mutex_);
        snapshot_.quiescent = true;
        candidate = snapshot_;
        if (!candidate.reason.empty())
            candidate.state = RunState::Failed;
        else if (stop_) {
            candidate.state = RunState::UserStopped;
            candidate.reason = "USER_STOP";
        } else if (last_result_.end == SessionEnd::Completed)
            candidate.state = RunState::Completed;
        else if (last_result_.end == SessionEnd::RecoveryRequired) {
            candidate.state = RunState::Interrupted;
            candidate.reason = "RECOVERY_REQUIRED";
        } else {
            candidate.state = RunState::Failed;
            candidate.reason = last_result_.reason;
        }
        candidate.result_saved = true;
    }
    try {
        journal_->commit_terminal(candidate.generation, storage::snapshot_json(candidate),
                                  [&](const nlohmann::json &events) {
                                      store_->save_terminal(candidate, last_result_, events);
                                  });
        std::lock_guard lock(mutex_);
        snapshot_ = std::move(candidate);
    } catch (const std::exception &e) {
        record_failure(e.what());
        std::lock_guard lock(mutex_);
        snapshot_.result_saved = false;
    } catch (...) {
        record_failure("STORAGE_TERMINAL_COMMIT_FAILED");
        std::lock_guard lock(mutex_);
        snapshot_.result_saved = false;
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
