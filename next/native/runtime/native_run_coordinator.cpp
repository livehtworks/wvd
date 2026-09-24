#include "native_run_coordinator.hpp"
#include "devices/lifecycle_execution.hpp"
#include <algorithm>

namespace wvd::runtime {
namespace {
using namespace std::chrono_literals;
std::atomic<std::uint64_t> next_run_id{1};

void require(bool value, const char *code) {
    if (!value) throw std::runtime_error(code);
}
contracts::SessionResult session_result(const NativeExecutionResult &result,
                                        const workflow::FlowProgram &program,
                                        std::uint64_t generation,
                                        const std::string &checkpoint,
                                        const contracts::BusinessRunState &business) {
    contracts::SessionResult session;
    session.end = result.flow.state == TickState::Completed
        ? contracts::SessionEnd::Completed
        : result.flow.state == TickState::Cancelled
            ? contracts::SessionEnd::UserStopped
            : result.flow.state == TickState::ExternalBlocked
                ? contracts::SessionEnd::ExternalBlocked
                : contracts::SessionEnd::Failed;
    session.reason = result.flow.code;
    session.outcome_category = result.flow.state == TickState::ExternalBlocked
        ? "external_blocked" : result.flow.state == TickState::Cancelled
            ? "user_stopped" : result.flow.state == TickState::Completed
                ? "completed" : "failed";
    session.inputs = result.inputs;
    session.quiescent = result.inputs_released;
    session.terminal = {generation, result.flow.source_path};
    session.checkpoint = {generation, checkpoint};
    session.business = business.summary();
    (void)program;
    return session;
}
}

NativeRunCoordinator::NativeRunCoordinator(std::filesystem::path data_root,
                                           std::size_t event_capacity)
    : data_root_(std::move(data_root)), instance_id_(platform::unique_id()),
      event_capacity_(event_capacity) {
    require(data_root_.is_absolute() && event_capacity_ >= 8 && event_capacity_ <= 65536,
            "NATIVE_COORDINATOR_CONFIG_INVALID");
}

NativeRunCoordinator::~NativeRunCoordinator() {
    request_stop();
    if (worker_.joinable()) worker_.join();
}

contracts::RunSnapshot NativeRunCoordinator::start(
    NativeRunDefinition definition, std::shared_ptr<devices::DeviceBackend> backend) {
    std::lock_guard starting(start_mutex_);
    require(backend && backend->verified_access(), "NATIVE_DEVICE_NOT_VERIFIED");
    require(!definition.request_id.empty() && !definition.units.empty() &&
            definition.units.size() <= 256 && definition.create_state && definition.operations &&
            definition.total_time_limit > 0ms &&
            definition.total_time_limit <= std::chrono::hours{24},
            "NATIVE_RUN_DEFINITION_INVALID");
    for (const auto &unit : definition.units) {
        unit.program.validate();
        require(unit.program.revision == unit.bundle.revision &&
                !unit.checkpoint_source_path.empty() && unit.time_limit > 0ms &&
                unit.time_limit <= std::chrono::minutes{30},
                "NATIVE_RUN_UNIT_INVALID");
    }
    if (definition.startup)
        require(devices::initial_lifecycle_plan(*definition.startup),
                "NATIVE_INITIAL_LIFECYCLE_INVALID");
    {
        std::lock_guard lock(mutex_);
        if (definition.request_id == request_id_ && snapshot_.run_id)
            return snapshot_;
        require(!active_, "NATIVE_RUN_ACTIVE_OR_CLEANUP_PENDING");
    }
    if (worker_.joinable()) worker_.join();
    auto lease = std::make_unique<platform::DeviceLease>(definition.policy.device_id);
    const auto id = next_run_id.fetch_add(1);
    const nlohmann::json frozen{{"engine_kind", "wvd_native"},
                                {"request_id", definition.request_id},
                                {"unit_count", definition.units.size()},
                                {"handoff_parent", definition.handoff_parent},
                                {"program_revision", definition.units.front().program.revision},
                                {"device_id", definition.policy.device_id},
                                {"game_id", definition.policy.game_id},
                                {"pack_revision", definition.policy.pack_revision},
                                {"viewport", definition.policy.viewport_id},
                                {"observed_read_only_viewport", definition.policy.observed_read_only_viewport}};
    auto journal = std::make_shared<storage::EventJournal>(instance_id_, id, event_capacity_);
    auto store = std::make_unique<storage::RunStore>(data_root_, instance_id_, id, frozen);
    {
        std::lock_guard lock(mutex_);
        require(!active_, "NATIVE_RUN_ACTIVE_OR_CLEANUP_PENDING");
        stop_ = false;
        active_ = true;
        execution_finished_ = false;
        terminal_recorded_ = false;
        snapshot_ = {};
        snapshot_.run_id = id;
        snapshot_.state = contracts::RunState::Preparing;
        snapshot_.quiescent = false;
        request_id_ = definition.request_id;
        lease_ = std::move(lease);
        journal_ = std::move(journal);
        store_ = std::move(store);
    }
    try {
        worker_ = std::jthread([this, definition = std::move(definition),
                                backend = std::move(backend)] {
            drive(definition, backend);
        });
    } catch (...) {
        std::lock_guard lock(mutex_);
        active_ = false;
        terminal_recorded_ = true;
        snapshot_.state = contracts::RunState::Failed;
        snapshot_.reason = "NATIVE_WORKER_START_FAILED";
        snapshot_.quiescent = true;
        lease_.reset();
        complete_.notify_all();
        throw;
    }
    return snapshot();
}

void NativeRunCoordinator::publish_state(contracts::RunState state, std::string reason) {
    std::lock_guard lock(mutex_);
    snapshot_.state = state;
    if (!reason.empty()) snapshot_.reason = std::move(reason);
}

void NativeRunCoordinator::drive(NativeRunDefinition definition,
                                 std::shared_ptr<devices::DeviceBackend> backend) noexcept {
    contracts::SessionResult last;
    std::unique_ptr<contracts::BusinessRunState> business;
    std::string failure;
    bool quiescent = true;
    const auto run_id = snapshot().run_id;
    const auto total_deadline = std::chrono::steady_clock::now() + definition.total_time_limit;
    try {
        business = definition.create_state({instance_id_, run_id,
            std::make_shared<contracts::SteadyClock>()});
        require(bool(business), "NATIVE_BUSINESS_STATE_MISSING");
        publish_state(contracts::RunState::Running);
        if (definition.startup) {
            auto *lifecycle = backend->lifecycle_port();
            require(lifecycle, "NATIVE_LIFECYCLE_PORT_MISSING");
            const auto outcome = devices::execute_lifecycle_plan(*definition.startup,
                *lifecycle, [this] { return stop_.load(); },
                [this](const std::string &type, const nlohmann::json &payload) {
                    journal_->emit(0, type, payload);
                });
            require(outcome == devices::LifecycleEnd::ReadyForBoot || stop_,
                "NATIVE_INITIAL_LIFECYCLE_UNCONFIRMED");
        }
        std::uint64_t generation = 0;
        for (std::size_t index = 0; index < definition.units.size() && !stop_; ++index) {
            const auto &unit = definition.units[index];
            business->enter_segment(index ? contracts::SegmentBoundary::Continuation :
                contracts::SegmentBoundary::Initial, generation + 1, index);
            for (unsigned recovery_attempt = 0; !stop_; ++recovery_attempt) {
                ++generation;
                const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                    total_deadline - std::chrono::steady_clock::now());
                require(remaining > 0ms, "NATIVE_RUN_TOTAL_DEADLINE");
                recognition::Service recognizer(unit.bundle, unit.recognizers);
                bool checkpoint_seen = false;
                auto event = [this, generation](const std::string &type, const nlohmann::json &data) {
                    journal_->emit(generation, type, data);
                };
                auto checkpoint = [this, &checkpoint_seen, &unit, generation](const std::string &source) {
                    const bool root = source == unit.checkpoint_source_path;
                    if (root) checkpoint_seen = true;
                    journal_->emit(generation, root ? "business_checkpoint" : "subflow_checkpoint",
                        {{"source_path", source}});
                };
                auto factory = definition.operations(*business, event, checkpoint);
                auto session = std::make_shared<NativeExecutionSession>(unit.program,
                    *backend, recognizer, *business, definition.policy, generation,
                    std::min(unit.time_limit, remaining), std::move(factory),
                    [this, generation](const std::string &id, const std::string &source) {
                        auto source_path = nlohmann::json::parse(source, nullptr, false);
                        if (source_path.is_discarded()) source_path = source;
                        journal_->emit(generation, "step", {{"node_id", id},
                            {"source_path", source_path}});
                    });
                {
                    std::lock_guard lock(mutex_);
                    session_ = session;
                    snapshot_.generation = generation;
                }
                if (stop_) session->request_stop();
                const auto result = session->run();
                {
                    std::lock_guard lock(mutex_);
                    session_.reset();
                    snapshot_.inputs.attempted += result.inputs.attempted;
                    snapshot_.inputs.accepted += result.inputs.accepted;
                    snapshot_.inputs.rejected += result.inputs.rejected;
                    snapshot_.inputs.backend_called += result.inputs.backend_called;
                    snapshot_.inputs.cleanup_called += result.inputs.cleanup_called;
                }
                quiescent = quiescent && result.inputs_released;
                last = session_result(result, unit.program, generation,
                    checkpoint_seen ? unit.checkpoint_source_path : std::string{}, *business);
                if (result.flow.state == TickState::Completed && checkpoint_seen &&
                    !result.unresolved_input && result.inputs_released) {
                    std::lock_guard lock(mutex_);
                    ++snapshot_.completed_business_units;
                    snapshot_.business = business->summary();
                    snapshot_.sessions.push_back({{"generation", generation},
                        {"engine_kind", "wvd_native"}, {"outcome", "Completed"},
                        {"checkpoint", unit.checkpoint_source_path}});
                    break;
                }
                failure = result.flow.state == TickState::Completed && !checkpoint_seen
                    ? "NATIVE_BUSINESS_CHECKPOINT_MISSING"
                    : result.unresolved_input ? "NATIVE_INPUT_RESULT_UNCONFIRMED"
                    : !result.inputs_released ? "NATIVE_INPUT_CLEANUP_PENDING"
                    : result.flow.code;
                if (stop_ || !quiescent || result.unresolved_input || !definition.recovery ||
                    recovery_attempt >= 3) break;
                auto plan = definition.recovery(last, *business, recovery_attempt + 1);
                if (!plan) break;
                devices::validate_lifecycle_plan(*plan);
                const auto expected = plan->target;
                require(expected.device_id == definition.policy.device_id &&
                    plan->operations == (expected.vpn_required
                        ? std::vector{devices::LifecycleOperation::EnsureVpn,
                            devices::LifecycleOperation::StopApplication,
                            devices::LifecycleOperation::StartApplication}
                        : std::vector{devices::LifecycleOperation::StopApplication,
                            devices::LifecycleOperation::StartApplication}),
                    "NATIVE_RECOVERY_PLAN_UNSAFE");
                if (plan->defer_for > 0ms) {
                    const auto wake_at = std::chrono::steady_clock::now() + plan->defer_for;
                    require(wake_at <= total_deadline, "NATIVE_RECOVERY_EXCEEDS_RUN_DEADLINE");
                    event("recovery.deferred", {{"duration_ms", plan->defer_for.count()}});
                    std::unique_lock lock(mutex_);
                    complete_.wait_until(lock, wake_at, [this] { return stop_.load(); });
                    if (stop_) break;
                }
                auto *lifecycle = backend->lifecycle_port();
                require(lifecycle, "NATIVE_RECOVERY_PORT_MISSING");
                auto executable_plan = *plan;
                executable_plan.defer_for = 0ms;
                const auto outcome = devices::execute_lifecycle_plan(executable_plan, *lifecycle,
                    [this] { return stop_.load(); }, event);
                if (outcome != devices::LifecycleEnd::ReadyForBoot) {
                    failure = "NATIVE_RECOVERY_UNCONFIRMED";
                    break;
                }
                business->enter_segment(contracts::SegmentBoundary::LifecycleRecovery,
                    generation + 1, index);
                failure.clear();
            }
            if (!failure.empty()) break;
        }
    } catch (const std::exception &error) {
        failure = error.what();
    } catch (...) {
        failure = "NATIVE_RUN_EXCEPTION";
    }
    {
        std::lock_guard lock(mutex_);
        execution_finished_ = true;
    }
    try {
        if (!backend->release_owned_inputs()) quiescent = false;
    } catch (...) {
        quiescent = false;
    }
    contracts::RunSnapshot terminal;
    {
        std::lock_guard lock(mutex_);
        terminal = snapshot_;
    }
    terminal.quiescent = quiescent;
    try {
        terminal.business = business ? business->summary() : nlohmann::json(nullptr);
    } catch (...) {
        terminal.business = nullptr;
        terminal.secondary_errors.push_back("NATIVE_BUSINESS_SUMMARY_FAILED");
    }
    terminal.reason = std::move(failure);
    terminal.state = !quiescent ? contracts::RunState::Interrupted
        : stop_ ? contracts::RunState::UserStopped
        : !terminal.reason.empty() ? contracts::RunState::Failed
        : terminal.completed_business_units == definition.units.size()
            ? contracts::RunState::Completed : contracts::RunState::Failed;
    if (quiescent && business && definition.handoff_ready) {
        try {
            if (definition.handoff_ready(last, *business)) {
                terminal.state = contracts::RunState::Interrupted;
                terminal.outcome_category = "handoff_ready";
            }
        } catch (const std::exception &error) {
            terminal.secondary_errors.push_back(std::string("HANDOFF_VALIDATION:") + error.what());
        }
    }
    if (last.end == contracts::SessionEnd::ExternalBlocked) {
        terminal.state = contracts::RunState::Interrupted;
        terminal.outcome_category = "external_blocked";
    }
    if (!quiescent && terminal.reason.empty()) terminal.reason = "NATIVE_CLEANUP_PENDING";
    terminal.result_saved = true;
    try {
        journal_->commit_terminal(terminal.generation, storage::snapshot_json(terminal),
            [&](const nlohmann::json &events) {
                store_->save_terminal(terminal, last, events);
            });
    } catch (const std::exception &error) {
        terminal.result_saved = false;
        terminal.storage_error = error.what();
    } catch (...) {
        terminal.result_saved = false;
        terminal.storage_error = "NATIVE_TERMINAL_SAVE_FAILED";
    }
    {
        std::lock_guard lock(mutex_);
        snapshot_ = std::move(terminal);
        if (snapshot_.quiescent) {
            lease_.reset();
            active_ = false;
        }
        terminal_recorded_ = true;
    }
    complete_.notify_all();
}

void NativeRunCoordinator::request_stop() {
    std::shared_ptr<NativeExecutionSession> session;
    {
        std::lock_guard lock(mutex_);
        if (!active_ || execution_finished_) return;
        stop_ = true;
        session = session_;
    }
    if (session) session->request_stop();
    complete_.notify_all();
    {
        std::lock_guard lock(mutex_);
        if (active_ && !execution_finished_)
            snapshot_.state = contracts::RunState::StopRequested;
    }
}

contracts::RunSnapshot NativeRunCoordinator::snapshot() const {
    std::lock_guard lock(mutex_);
    return snapshot_;
}
std::optional<contracts::RunSnapshot> NativeRunCoordinator::request_snapshot(
    const std::string &request_id) const {
    std::lock_guard lock(mutex_);
    return request_id == request_id_ && snapshot_.run_id
        ? std::optional(snapshot_) : std::nullopt;
}
bool NativeRunCoordinator::wait_for(std::chrono::milliseconds duration) {
    std::unique_lock lock(mutex_);
    return complete_.wait_for(lock, duration, [this] { return !active_; });
}
bool NativeRunCoordinator::wait_for_worker(std::chrono::milliseconds duration) {
    std::unique_lock lock(mutex_);
    return complete_.wait_for(lock, duration, [this] {
        return terminal_recorded_;
    });
}
nlohmann::json NativeRunCoordinator::events(std::uint64_t after) const {
    std::lock_guard lock(mutex_);
    return journal_ ? journal_->read(after) : nlohmann::json{{"events", nlohmann::json::array()}};
}
nlohmann::json NativeRunCoordinator::diagnostics() const {
    std::lock_guard lock(mutex_);
    return store_ ? store_->diagnostic_summary() :
        nlohmann::json{{"entries", nlohmann::json::array()}};
}
std::filesystem::path NativeRunCoordinator::run_directory() const {
    std::lock_guard lock(mutex_);
    return store_ ? store_->directory() : std::filesystem::path{};
}
} // namespace wvd::runtime
