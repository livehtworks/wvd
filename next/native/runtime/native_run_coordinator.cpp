#include "native_run_coordinator.hpp"
#include "devices/lifecycle_execution.hpp"
#include "platform/windows/memory_diagnostics.hpp"
#include "platform/windows/file_digest.hpp"
#include "platform/windows/path_utf8.hpp"
#include <algorithm>
#include <functional>
#include <iterator>

namespace wvd::runtime {
namespace {
using namespace std::chrono_literals;
std::atomic<std::uint64_t> next_run_id{1};

nlohmann::json executable_digest() {
    static std::once_flag once;
    static std::string digest;
    std::call_once(once, [] {
        try {
            wchar_t path[32768]{};
            const auto length = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
            if (length && length < std::size(path))
                digest = platform::file_sha256(std::filesystem::path(path));
        } catch (...) {}
    });
    return digest.empty() ? nlohmann::json(nullptr) : nlohmann::json(digest);
}

struct ScopeExit {
    std::function<void()> action;
    ~ScopeExit() noexcept { try { if (action) action(); } catch (...) {} }
};

nlohmann::json memory_record(const platform::MemorySample &memory) {
    nlohmann::json lifetimes = nlohmann::json::object();
    const auto counts = platform::MemoryOwnerLifetime::counts();
    const std::array<const char *, 3> names{"recognizers", "ocr_engines", "execution_sessions"};
    for (std::size_t i = 0; i < names.size(); ++i)
        lifetimes[names[i]] = {{"created", counts[i].created}, {"destroyed", counts[i].destroyed},
            {"live", counts[i].live}, {"ready", counts[i].ready}};
    return {{"object_lifetimes", lifetimes}, {"object_lifetimes_scope", "process"}, {"process_id", memory.process_id},
            {"process_created_100ns", memory.process_created_100ns},
            {"process_memory_available", memory.process_ok},
            {"private_bytes", memory.process_ok ? nlohmann::json(memory.private_bytes) : nullptr},
            {"working_set_bytes", memory.process_ok ? nlohmann::json(memory.working_set_bytes) : nullptr},
            {"handle_count", memory.handle_count},
            {"system_memory_available", memory.system_ok},
            {"system_commit_bytes", memory.system_ok ? nlohmann::json(memory.commit_total_pages * memory.page_size) : nullptr},
            {"system_commit_limit_bytes", memory.system_ok ? nlohmann::json(memory.commit_limit_pages * memory.page_size) : nullptr},
            {"system_physical_available_bytes", memory.system_ok ? nlohmann::json(memory.physical_available_pages * memory.page_size) : nullptr}};
}

nlohmann::json memory_owners_record(const platform::MemorySample &memory) {
    const auto sample = platform::sample_memory_owners();
    auto result = memory_record(memory);
    result["owners_available"] = sample.available;
    result["owners_truncated"] = sample.truncated;
    result["owners_examined"] = sample.examined;
    result["owners_unreadable"] = sample.unreadable;
    result["owners_elapsed_ms"] = sample.elapsed_ms;
    // This is only the readable process subset, not a reconciliation of system commit.
    result["readable_process_private_bytes"] = sample.readable_private_bytes;
    result["top_private_processes"] = nlohmann::json::array();
    for (unsigned i = 0; i < sample.count; ++i) {
        const auto &owner = sample.top[i];
        result["top_private_processes"].push_back({{"pid", owner.process_id},
            {"created_100ns", owner.created_100ns}, {"name", platform::utf8(owner.name.data())},
            {"private_bytes", owner.private_bytes}, {"working_set_bytes", owner.working_set_bytes}});
    }
    return result;
}

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
        : result.flow.state == TickState::BusinessFailed
            ? contracts::SessionEnd::BusinessFailed
        : result.flow.state == TickState::Cancelled
            ? contracts::SessionEnd::UserStopped
            : result.flow.state == TickState::ExternalBlocked
                ? contracts::SessionEnd::ExternalBlocked
                : contracts::SessionEnd::Failed;
    session.reason = result.flow.code;
    session.outcome_category = result.flow.state == TickState::ExternalBlocked
        ? "external_blocked" : result.flow.state == TickState::Cancelled
            ? "user_stopped" : result.flow.state == TickState::Completed
                ? "completed" : result.flow.state == TickState::BusinessFailed
                    ? "business_failed" : "failed";
    session.inputs = result.inputs;
    session.quiescent = result.inputs_released;
    session.terminal = {generation, result.flow.source_path};
    session.checkpoint = {generation, checkpoint};
    session.business = business.summary();
    session.unresolved_inputs = result.unresolved_inputs;
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
    join_worker();
}

void NativeRunCoordinator::join_worker() {
    if (!worker_.joinable()) return;
    worker_.join();
    record_memory_boundary("worker_joined");
    // The definition and all session owners have already been destroyed. Keep
    // the original pre-maintenance sample so reclamation cannot hide growth.
    const bool measure = store_ && store_->memory_logging_enabled();
    const auto heap = platform::optimize_idle_heap(measure);
    if (!measure) return;
    try {
        auto sample = memory_record(heap.after);
        const auto usage = [](const platform::HeapMaintenance::Usage &value) {
            auto heaps = nlohmann::json::array();
            for (std::size_t i = 0; i < std::min<std::size_t>(value.heaps, value.detail.size()); ++i) {
                const auto &entry = value.detail[i];
                heaps.push_back({{"address", entry.address}, {"process_heap", entry.process_heap},
                    {"complete", entry.complete}, {"win32_error", entry.error}, {"elapsed_us", entry.elapsed_us},
                    {"allocated_bytes", entry.complete ? nlohmann::json(entry.allocated) : nullptr},
                    {"committed_bytes", entry.complete ? nlohmann::json(entry.committed) : nullptr},
                    {"reserved_bytes", entry.complete ? nlohmann::json(entry.reserved) : nullptr}});
            }
            return nlohmann::json{{"available", value.available}, {"complete", value.complete}, {"per_heap", std::move(heaps)},
                {"heaps", value.heaps}, {"failed", value.failed}, {"allocated_bytes", value.allocated},
                {"committed_bytes", value.committed}, {"reserved_bytes", value.reserved},
                {"elapsed_us", value.elapsed_us}};
        };
        sample["heap_maintenance"] = {{"api", "HeapOptimizeResources"},
            {"succeeded", heap.succeeded}, {"win32_error", heap.error},
            {"elapsed_us", heap.elapsed_us}, {"before_process_ok", heap.before.process_ok},
            {"before_private_bytes", heap.before.private_bytes},
            {"heap_summary_before", usage(heap.heap_before)}, {"heap_summary_after", usage(heap.heap_after)}};
        if (store_) store_->record_memory_boundary("heap_resources_optimized", sample);
    } catch (...) {
        if (store_) store_->note_diagnostic_hook_failure();
    }
}

void NativeRunCoordinator::record_memory_boundary(const char *phase) noexcept {
    try {
        if (store_) store_->record_memory_boundary(phase, memory_record(platform::sample_memory()));
    } catch (...) {
        // JSON构造也可能在低内存下失败，不能让辅助采样使已完成的任务线程终止进程。
        if (store_) store_->note_diagnostic_hook_failure();
    }
}

bool NativeRunCoordinator::collect_finished_worker() {
    std::lock_guard starting(start_mutex_);
    { std::lock_guard lock(mutex_); if (!terminal_recorded_) return false; }
    // 终态已落盘之后才join；不会在运行中阻塞停止接口，也不把收尾采样称为线程退出。
    join_worker();
    return true;
}

void NativeRunCoordinator::record_batch_release() {
    std::lock_guard starting(start_mutex_);
    { std::lock_guard lock(mutex_); if (!terminal_recorded_ || active_) return; }
    join_worker();
    record_memory_boundary("batch_payloads_released");
}

void NativeRunCoordinator::record_preparation(const nlohmann::json &metrics) {
    std::lock_guard lock(mutex_);
    if (store_) store_->append_log(snapshot_.generation, storage::LogLevel::Info,
                                  "performance", "preparation.completed", metrics);
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
    std::set<const workflow::FlowProgram *> validated;
    nlohmann::json program_owners = nlohmann::json::array();
    std::map<const workflow::FlowProgram *, std::size_t> owner_ids;
    for (const auto &unit : definition.units) {
        require(bool(unit.program), "NATIVE_PROGRAM_OWNER_MISSING");
        if (validated.insert(unit.program.get()).second) unit.program->validate();
        const auto [owner, inserted] = owner_ids.emplace(unit.program.get(), owner_ids.size());
        (void)inserted;
        program_owners.push_back(owner->second);
        require(unit.program->revision == unit.bundle.revision &&
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
    join_worker();
    auto lease = std::make_unique<platform::DeviceLease>(definition.policy.device_id);
    const auto id = next_run_id.fetch_add(1);
    const nlohmann::json frozen{{"engine_kind", "wvd_native"},
                                {"request_id", definition.request_id},
                                {"unit_count", definition.units.size()},
                                {"program_schema", workflow::FlowProgram::schema},
                                {"unique_program_count", owner_ids.size()},
                                {"unit_program_owners", program_owners},
                                {"handoff_parent", definition.handoff_parent},
                                {"program_revision", definition.units.front().program->revision},
                                {"executable_sha256", executable_digest()},
                                {"device_id", definition.policy.device_id},
                                {"game_id", definition.policy.game_id},
                                {"pack_revision", definition.policy.pack_revision},
                                {"logging", definition.logging.json()},
                                {"viewport", definition.policy.viewport_id},
                                {"observed_read_only_viewport", definition.policy.observed_read_only_viewport}};
    auto store = std::make_unique<storage::RunStore>(data_root_, instance_id_, id, frozen,
        std::make_shared<contracts::SteadyClock>(), storage::DiagnosticLimits{}, definition.logging);
    auto journal = std::make_shared<storage::EventJournal>(instance_id_, id, event_capacity_,
        [target = store.get()](const nlohmann::json &event) { target->append_event(event); });
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
                                backend = std::move(backend)]() mutable {
            // 参数转换和最后的落盘也在边界内；异常不能逸出 jthread 入口。
            try { drive(definition, backend); }
            catch (...) { worker_failed(backend); }
            // 整图、bundle和回调捕获的释放边界独立于Session，采样后仍待外层join。
            definition = NativeRunDefinition{};
            backend.reset();
            record_memory_boundary("worker_definition_released");
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

void NativeRunCoordinator::worker_failed(
    const std::shared_ptr<devices::DeviceBackend> &backend) noexcept {
    stop_.store(true);
    bool released = false;
    try {
        std::shared_ptr<NativeExecutionSession> session;
        { std::lock_guard lock(mutex_); session = session_; }
        if (session) session->request_stop();
    } catch (...) {}
    try { released = backend && backend->release_owned_inputs(); } catch (...) {}
    try { if (store_) store_->finish_recent_frames(); } catch (...) {}
    try {
        std::lock_guard lock(mutex_);
        snapshot_.state = contracts::RunState::Failed;
        snapshot_.quiescent = released;
        snapshot_.result_saved = false;
        snapshot_.details_complete = false;
        // 不在极端内存不足路径组装 JSON；既有未决输入快照原样保留。
        try { snapshot_.reason = "WORKER_ABORT"; } catch (...) {}
        execution_finished_ = true;
        terminal_recorded_ = true; // 工作线程已结束；result_saved=false 单独表达未落盘。
        active_ = !released;
        if (released) lease_.reset();
        session_.reset();
    } catch (...) { /* 控制面不得被异常穿透；没有写出 Completed。 */ }
    complete_.notify_all();
}

void NativeRunCoordinator::publish_state(contracts::RunState state, std::string reason) {
    std::lock_guard lock(mutex_);
    snapshot_.state = state;
    if (!reason.empty()) snapshot_.reason = std::move(reason);
}

void NativeRunCoordinator::drive(const NativeRunDefinition &definition,
                                 const std::shared_ptr<devices::DeviceBackend> &backend) {
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
                if (definition.logging.memory && definition.logging.accepts(storage::LogLevel::Info)) {
                    auto before = memory_record(platform::sample_memory());
                    before["unit_index"] = index;
                    before["recovery_attempt"] = recovery_attempt;
                    store_->append_log(generation, storage::LogLevel::Info, "memory", "before_session", before);
                }
                std::weak_ptr<recognition::Service> weak_recognizer;
                std::weak_ptr<NativeExecutionSession> weak_session;
                ScopeExit after_release{[this, generation, index, &definition,
                                         &weak_recognizer, &weak_session] {
                    if (!definition.logging.memory || !definition.logging.accepts(storage::LogLevel::Info)) return;
                    auto after = memory_record(platform::sample_memory());
                    after["unit_index"] = index;
                    after["recognizer_released"] = weak_recognizer.expired();
                    after["session_released"] = weak_session.expired();
                    store_->append_log(generation, storage::LogLevel::Info, "memory",
                                       "session_owners_released", after);
                }};
                const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                    total_deadline - std::chrono::steady_clock::now());
                require(remaining > 0ms, "NATIVE_RUN_TOTAL_DEADLINE");
                auto recognizer = std::make_shared<recognition::Service>(
                    unit.bundle, unit.recognizers, definition.match_budget,
                    store_->directory() / "recognition-memory.log", run_id, generation,
                    definition.logging);
                weak_recognizer = recognizer;
                bool checkpoint_seen = false;
                auto recovery_frame = std::make_shared<std::optional<contracts::FrameEnvelope>>();
                auto capture_after_input = std::make_shared<std::uint64_t>(0);
                auto combat_diagnostic = std::make_shared<CombatDiagnosticState>();
                auto event = [this, generation, index, recovery_frame, combat_diagnostic]
                    (const std::string &type, const nlohmann::json &data) {
                    record_combat_diagnostic(generation, index, type, data,
                        recovery_frame->has_value() ? &**recovery_frame : nullptr, *combat_diagnostic);
                    if (type == "combat" && recovery_frame->has_value() &&
                        data.value("frame_id", 0ULL) == (**recovery_frame).identity.frame_id) {
                        const auto &frame = **recovery_frame;
                        journal_->emit(generation, "recent_frame.action", {
                            {"frame_id", frame.identity.frame_id}, {"stage", data.value("operation", "")},
                            {"queued", store_->save_recent_frame(frame, true)}});
                    }
                    journal_->emit(generation, type, data);
                };
                auto checkpoint = [this, &checkpoint_seen, &unit, generation](const std::string &source) {
                    const bool root = source == unit.checkpoint_source_path;
                    if (root) checkpoint_seen = true;
                    journal_->emit(generation, root ? "business_checkpoint" : "subflow_checkpoint",
                        {{"source_path", source}});
                };
                auto factory = definition.operations(*business, event, checkpoint);
                // 共享最后一帧的既有缓冲，不额外取图/解码；只供同一工作线程重启前落盘。
                auto session = std::make_shared<NativeExecutionSession>(unit.program,
                    *backend, recognizer, *business, definition.policy, generation,
                    std::min(unit.time_limit, remaining), std::move(factory),
                    [this, generation, index, recovery_frame, restart_diagnostic_started = 0ULL]
                    (const nlohmann::json &progress) mutable {
                        bool step_changed = false;
                        bool recovery_changed = false;
                        nlohmann::json previous_pending = nlohmann::json::array();
                        nlohmann::json current_pending = progress.value("pending_inputs", nlohmann::json::array());
                        {
                            std::lock_guard lock(mutex_);
                            if (snapshot_.generation != generation) return;
                            step_changed = snapshot_.execution.is_null() ||
                                snapshot_.execution.value("step_id", "") != progress.value("step_id", "");
                            previous_pending = snapshot_.unresolved_inputs;
                            recovery_changed = snapshot_.execution.is_null() ||
                                snapshot_.execution.value("observation_recovery", nlohmann::json(nullptr)) !=
                                progress.value("observation_recovery", nlohmann::json(nullptr));
                            snapshot_.execution = progress;
                            snapshot_.active_event = progress.value("active_event", nlohmann::json(nullptr));
                            snapshot_.unresolved_inputs = current_pending;
                        }
                        if (recovery_changed && !progress.value("observation_recovery", nlohmann::json(nullptr)).is_null())
                            journal_->emit(generation, "observation.recovery", progress.at("observation_recovery"));
                        const auto recovery = progress.value("observation_recovery", nlohmann::json(nullptr));
                        if (recovery.is_object() && recovery.value("active", false) &&
                            recovery.value("code", "") == "CONTINUOUS_EXCEPTION_TIMEOUT" &&
                            recovery.value("started_at_ns", 0ULL) != restart_diagnostic_started) {
                            restart_diagnostic_started = recovery.value("started_at_ns", 0ULL);
                            save_application_restart_diagnostic(generation, index, progress,
                                recovery_frame->has_value() ? &**recovery_frame : nullptr);
                        }
                        if (step_changed) {
                            auto source = nlohmann::json::parse(progress.value("source_path", ""), nullptr, false);
                            if (source.is_discarded()) source = progress.value("source_path", "");
                            journal_->emit(generation, "step", {{"node_id", progress.value("step_id", "")},
                                {"source_path", source}});
                        }
                        for (const auto &before : previous_pending) {
                            bool remains = false;
                            for (const auto &after : current_pending)
                                if (after.value("source_path", "") == before.value("source_path", "") &&
                                    after.value("action_epoch", 0ULL) == before.value("action_epoch", 0ULL))
                                    remains = true;
                            if (!remains) journal_->emit(generation, "input.pending_removed",
                                {{"source_path", before.value("source_path", "")},
                                 {"action_epoch", before.value("action_epoch", 0ULL)}});
                        }
                        // 只在值改变时发一条轻量事实；不含帧字节与识别矩阵。
                        if (!step_changed) journal_->emit(generation, "execution.changed", progress);
                    },
                    [this, generation, index, recovery_frame, capture_after_input, combat_diagnostic]
                    (const nlohmann::json &input) {
                        record_combat_diagnostic(generation, index, "input", input,
                            recovery_frame->has_value() ? &**recovery_frame : nullptr, *combat_diagnostic);
                        const auto type = input.value("state", "") == "result" ? "input.result" : "input.attempt";
                        store_->append_timing(generation, type, input);
                        journal_->emit(generation, type, input);
                        const auto state = input.value("state", "");
                        if (state == "attempted" || state == "result") {
                            const auto wanted = input.value(state == "result" ? "observed_frame" : "basis_frame", 0ULL);
                            const bool available = recovery_frame->has_value() &&
                                (**recovery_frame).identity.frame_id == wanted;
                            journal_->emit(generation, "recent_frame.action", {
                                {"frame_id", wanted}, {"stage", state}, {"available", available},
                                {"queued", available && store_->save_recent_frame(**recovery_frame, true)}});
                        }
                        if (state == "accepted") *capture_after_input = input.value("basis_frame", 0ULL);
                    },
                    [this, generation, index, event, recovery_frame, capture_after_input, application = definition.policy.application_id,
                     logging = definition.logging, warned = false, next_memory_sample = std::chrono::steady_clock::time_point{}]
                    (const contracts::FrameEnvelope &frame) mutable {
                        *recovery_frame = frame;
                        // Outside the matcher: at most one bounded process scan per 30s,
                        // only at session start or high commit pressure. No extra screenshots.
                        if (logging.memory && logging.accepts(storage::LogLevel::Info) &&
                            std::chrono::steady_clock::now() >= next_memory_sample) {
                            const bool first = next_memory_sample == std::chrono::steady_clock::time_point{};
                            next_memory_sample = std::chrono::steady_clock::now() + 30s;
                            try {
                                const auto sample = platform::sample_memory();
                                const bool pressure = sample.system_ok && sample.commit_limit_pages &&
                                    static_cast<double>(sample.commit_total_pages) / sample.commit_limit_pages >= .90;
                                auto detail = first || pressure ? memory_owners_record(sample) : memory_record(sample);
                                detail["frame_id"] = frame.identity.frame_id;
                                store_->append_log(generation, pressure ? storage::LogLevel::Warn : storage::LogLevel::Info,
                                    "memory", pressure ? "system_pressure" : "runtime_sample", detail);
                            } catch (...) { store_->note_diagnostic_hook_failure(); }
                        }
                        if (logging.performance && logging.accepts(storage::LogLevel::Trace)) {
                            const auto capture_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                frame.identity.capture_finished_at - frame.identity.captured_at).count();
                            store_->append_log(generation, storage::LogLevel::Trace, "performance",
                                "capture.frame", {{"frame_id", frame.identity.frame_id},
                                    {"backend", frame.identity.backend}, {"capture_ns", capture_ns},
                                    {"raw_bytes", frame.raw_bgr ? frame.raw_bgr->size() : 0},
                                    {"encoded_bytes", frame.encoded_image.size()}});
                        }
                        try {
                            if (frame.identity.foreground_application != application && frame.raw_bgr) {
                                storage::DiagnosticRequest request;
                                { std::lock_guard lock(mutex_); request.run_id = snapshot_.run_id; }
                                request.generation = generation; request.unit_index = index;
                                request.node = "capture.foreground"; request.reason = "GAME_NOT_FOREGROUND";
                                request.stage = "recovery_entry"; request.evidence_kind = "foreground_lost_pixels";
                                const contracts::DiagnosticPixels pixels{frame.identity.raw_size, frame.raw_bgr,
                                    frame.identity.captured_at, frame.identity.device_id, frame.identity.backend};
                                event("diagnostic.foreground_lost", store_->save_diagnostic(nullptr, request, &pixels));
                            } else {
                                const bool after_input = *capture_after_input && frame.identity.frame_id > *capture_after_input;
                                const bool queued = store_->save_recent_frame(frame, after_input);
                                if (after_input) {
                                    journal_->emit(generation, "recent_frame.action", {
                                        {"frame_id", frame.identity.frame_id}, {"stage", "first_after_input"}, {"queued", queued}});
                                    *capture_after_input = 0;
                                }
                            }
                        }
                        catch (const std::exception &error) {
                            if (!warned) {
                                warned = true;
                                journal_->emit(generation, "recent_frame.write_failed",
                                    {{"error", error.what()}});
                            }
                        }
                    },
                    [this, generation](const nlohmann::json &timing) {
                        store_->append_timing(generation, "timing.segment", timing);
                    });
                weak_session = session;
                {
                    std::lock_guard lock(mutex_);
                    session_ = session;
                    snapshot_.generation = generation;
                    snapshot_.execution = nullptr;
                    snapshot_.active_event = nullptr;
                    snapshot_.unresolved_inputs = nlohmann::json::array();
                }
                if (stop_) session->request_stop();
                const auto result = session->run();
                if (const auto &pixels = session->failed_pixels(); pixels) {
                    try {
                        storage::DiagnosticRequest request;
                        { std::lock_guard lock(mutex_); request.run_id = snapshot_.run_id; }
                        request.generation = generation; request.unit_index = index;
                        request.node = session->diagnostic_node().substr(0, 256);
                        request.reason = "CAPTURE_METADATA_FAILURE";
                        request.stage = "recovery_entry";
                        request.evidence_kind = "metadata_invalid_pixels";
                        event("diagnostic.read_failure_pixels", store_->save_diagnostic(nullptr, request, &*pixels));
                    } catch (...) { store_->note_diagnostic_hook_failure(); }
                }
                // Session 已先停止输入并清理。诊断失败不能触发重放，也不能掩盖原 flow_code。
                // 这里保存的是“失败前最后有效帧”，不是故障瞬间画面，更不是当前现场。
                if (result.flow.state != TickState::Completed && result.flow.state != TickState::Cancelled) {
                    try {
                        if (definition.logging.memory && definition.logging.accepts(storage::LogLevel::Info)) {
                            auto memory = memory_owners_record(platform::sample_memory());
                            memory["flow_code"] = result.flow.code;
                            store_->append_log(generation, storage::LogLevel::Warn, "memory", "failure_snapshot", memory);
                        }
                        storage::DiagnosticRequest request;
                        { std::lock_guard lock(mutex_); request.run_id = snapshot_.run_id; }
                        request.generation = generation;
                        request.unit_index = index;
                        request.task_id = 0; // 原生内核没有 Maa task_id，不能伪造编号。
                        request.depth = std::max(0, session->diagnostic_depth());
                        request.node = session->diagnostic_node().substr(0, 256);
                        request.reason = (result.flow.code.empty() ? std::string("NATIVE_SESSION_INCOMPLETE") : result.flow.code).substr(0, 256);
                        request.stage = result.unresolved_input ? "postcondition" : "pre_action";
                        request.evidence_kind = "last_valid_before_failure";
                        const auto &frame = session->last_valid_frame();
                        const auto saved = store_->save_diagnostic(frame ? &*frame : nullptr, request);
                        event("diagnostic.failure_frame", saved);
                    } catch (...) { store_->note_diagnostic_hook_failure(); }
                }
                const auto resources = recognizer->resource_stats();
                const bool memory_enabled = definition.logging.memory &&
                    definition.logging.accepts(storage::LogLevel::Info);
                const auto memory = memory_enabled ? platform::sample_memory() : platform::MemorySample{};
                if (definition.logging.memory && definition.logging.accepts(storage::LogLevel::Info)) {
                    auto held = memory_record(memory);
                    held["unit_index"] = index;
                    held["recognizer_ownership"] = recognizer->ownership_snapshot();
                    held["execution_ownership"] = session->ownership_snapshot();
                    held["cache_retained_bytes"] = resources.retained_bytes;
                    held["cache_in_use_bytes"] = resources.in_use_bytes;
                    held["result_cache_estimated_bytes"] = resources.result_cache_estimated_bytes;
                    held["active_matches"] = resources.active_matches;
                    held["peak_estimated_workspace_bytes"] = resources.peak_estimated_workspace_bytes;
                    held["frame_bytes_scope"] = "session_and_recovery_frame_only";
                    std::uint64_t known_frame_bytes = 0;
                    const auto &last_frame = session->last_valid_frame();
                    const auto first = last_frame && last_frame->raw_bgr ? last_frame->raw_bgr.get() : nullptr;
                    if (first) known_frame_bytes += last_frame->raw_bgr->size();
                    if (recovery_frame->has_value() && (*recovery_frame)->raw_bgr &&
                        (*recovery_frame)->raw_bgr.get() != first)
                        known_frame_bytes += (*recovery_frame)->raw_bgr->size();
                    held["known_frame_unique_buffer_bytes"] = known_frame_bytes;
                    store_->append_log(generation, storage::LogLevel::Info, "memory", "session_owners_alive", held);
                }
                if (memory_enabled) event("recognition.resources", {{"cache_retained_bytes", resources.retained_bytes},
                    {"cache_in_use_bytes", resources.in_use_bytes},
                    {"cache_evictable_bytes", resources.evictable_bytes},
                    {"cache_live_bytes", resources.live_bytes},
                    {"decode_count", resources.decode_count},
                    {"mask_build_count", resources.mask_build_count},
                    {"cache_entries", resources.entries},
                    {"cache_maintenance_failed", resources.cache_maintenance_failed},
                    {"result_cache_entries", resources.result_cache_entries},
                    {"result_cache_estimated_bytes", resources.result_cache_estimated_bytes},
                    {"active_matches", resources.active_matches}, {"peak_matches", resources.peak_matches},
                    {"peak_estimated_workspace_bytes", resources.peak_estimated_workspace_bytes},
                    {"diagnostic_write_failed", recognizer->diagnostic_write_failed()},
                    {"process_memory_available", memory.process_ok},
                    {"private_bytes", memory.process_ok ? memory.private_bytes : 0},
                    {"working_set_bytes", memory.process_ok ? memory.working_set_bytes : 0},
                    {"peak_working_set_bytes", memory.process_ok ? memory.peak_working_set_bytes : 0},
                    {"system_memory_available", memory.system_ok},
                    {"commit_total_pages", memory.system_ok ? memory.commit_total_pages : 0},
                    {"commit_limit_pages", memory.system_ok ? memory.commit_limit_pages : 0},
                    {"commit_peak_pages", memory.system_ok ? memory.commit_peak_pages : 0},
                    {"physical_available_pages", memory.system_ok ? memory.physical_available_pages : 0},
                    {"page_size", memory.system_ok ? memory.page_size : 0}});
                if (recognizer->diagnostic_write_failed()) {
                    event("diagnostic.memory_write_failed", {{"generation", generation}});
                    store_->append_log(generation, storage::LogLevel::Error, "runtime",
                        "memory_diagnostic_write_failed", {{"generation", generation}});
                }
                store_->append_log(generation, storage::LogLevel::Info, "recognition",
                    "session_statistics", {{"decode_count", resources.decode_count},
                        {"mask_build_count", resources.mask_build_count},
                        {"cache_entries", resources.entries},
                        {"cache_retained_bytes", resources.retained_bytes},
                        {"result_cache_entries", resources.result_cache_entries},
                        {"result_cache_estimated_bytes", resources.result_cache_estimated_bytes},
                        {"peak_matches", resources.peak_matches},
                        {"peak_estimated_workspace_bytes", resources.peak_estimated_workspace_bytes}});
                {
                    std::lock_guard lock(mutex_);
                    session_.reset();
                    snapshot_.active_event = nullptr;
                    snapshot_.details_complete = snapshot_.details_complete && result.details_complete;
                    if (result.details_complete && !result.unresolved_input)
                        snapshot_.unresolved_inputs = nlohmann::json::array();
                    snapshot_.inputs.attempted += result.inputs.attempted;
                    snapshot_.inputs.accepted += result.inputs.accepted;
                    snapshot_.inputs.rejected += result.inputs.rejected;
                    snapshot_.inputs.backend_called += result.inputs.backend_called;
                    snapshot_.inputs.cleanup_called += result.inputs.cleanup_called;
                }
                quiescent = quiescent && result.inputs_released;
                last = session_result(result, *unit.program, generation,
                    checkpoint_seen ? unit.checkpoint_source_path : std::string{}, *business);
                event("session.ended", {{"flow_state", static_cast<int>(result.flow.state)},
                    {"flow_code", result.flow.code}, {"cleanup_error", result.cleanup_error},
                    {"inputs_released", result.inputs_released},
                    {"unresolved_input", result.unresolved_input},
                    {"performance", result.performance},
                    {"details_complete", result.details_complete}});
                if (result.flow.state != TickState::Completed)
                    store_->append_log(generation, storage::LogLevel::Warn, "runtime",
                        "session_incomplete", {{"flow_code", result.flow.code},
                            {"unresolved_input", result.unresolved_input},
                            {"inputs_released", result.inputs_released}});
                if (result.flow.state == TickState::Completed && checkpoint_seen &&
                    !result.unresolved_input && result.inputs_released && result.details_complete) {
                    std::lock_guard lock(mutex_);
                    ++snapshot_.completed_business_units;
                    snapshot_.business = business->summary();
                    snapshot_.sessions.push_back({{"generation", generation},
                        {"engine_kind", "wvd_native"}, {"outcome", "Completed"},
                        {"details_complete", result.details_complete}, {"performance", result.performance},
                        {"checkpoint", unit.checkpoint_source_path}});
                    break;
                }
                failure = !result.details_complete ? "NATIVE_RESULT_DETAILS_INCOMPLETE"
                    : result.flow.state == TickState::Completed && !checkpoint_seen
                    ? "NATIVE_BUSINESS_CHECKPOINT_MISSING"
                    : !result.flow.code.empty() ? result.flow.code
                    : result.unresolved_input ? "NATIVE_INPUT_RESULT_UNCONFIRMED"
                    : !result.inputs_released ? "NATIVE_INPUT_CLEANUP_PENDING"
                    : result.flow.code;
                {
                    std::lock_guard lock(mutex_);
                    snapshot_.sessions.push_back({{"generation", generation}, {"engine_kind", "wvd_native"},
                        {"outcome", "NotCompleted"}, {"flow_state", static_cast<int>(result.flow.state)},
                        {"flow_code", result.flow.code}, {"details_complete", result.details_complete},
                        {"unresolved_input", result.unresolved_input}, {"observation_recovery", result.observation_recovery},
                        {"cleanup_error", result.cleanup_error}, {"performance", result.performance}});
                }
                auto *lifecycle = backend->lifecycle_port();
                auto recovery_result = last;
                // 业务判定与基础设施故障分开：只有实际设备观察能选中重连/实例恢复。
                // 原始flow_code与session.ended保留，不能用恢复理由覆盖事故证据。
                // 输入未确认仍可只读记录设备真相；观察不授权重放，也不覆盖原错误。
                if (!stop_ && quiescent && lifecycle && !backend->offline()) try {
                    const auto observed = lifecycle->observe_lifecycle();
                    if (observed) {
                        require(observed->target.device_id == definition.policy.device_id,
                            "NATIVE_RECOVERY_DEVICE_MISMATCH");
                        if (observed->instance_exited) recovery_result.reason = "device.instance_exited";
                        else if (observed->instance_running && !observed->connected)
                            recovery_result.reason = "device.disconnected";
                        else if (observed->connected && !observed->application_running)
                            recovery_result.reason = "device.application_exited";
                        else if (observed->connected && !observed->application_foreground)
                            recovery_result.reason = "device.application_background";
                        event("recovery.device_observed", {{"reason", recovery_result.reason},
                            {"original_reason", last.reason}, {"instance_exited", observed->instance_exited},
                            {"connected", observed->connected}, {"application_running", observed->application_running},
                            {"application_foreground", observed->application_foreground},
                            {"unresolved_input", result.unresolved_input}});
                    }
                } catch (const std::exception &error) {
                    event("recovery.observation_failed", {{"reason", error.what()}, {"original_reason", last.reason}});
                }
                // 外部维护/副作用未知不授权整段重跑；同一Session的读图恢复先处理现场。
                if (stop_ || !quiescent || result.unresolved_input || !result.details_complete ||
                    result.flow.state == TickState::BusinessFailed ||
                    result.flow.state == TickState::ExternalBlocked || !definition.recovery) break;
                auto plan = definition.recovery(recovery_result, *business, recovery_attempt + 1);
                if (!plan) break;
                devices::validate_lifecycle_plan(*plan);
                const auto expected = plan->target;
                using O = devices::LifecycleOperation;
                std::vector<O> allowed;
                if (recovery_result.reason == "device.instance_exited") allowed.push_back(O::RestartInstance);
                if (recovery_result.reason == "device.disconnected") allowed.push_back(O::Reconnect);
                if (expected.vpn_required) allowed.push_back(O::EnsureVpn);
                if (!recovery_result.reason.starts_with("device.")) allowed.push_back(O::StopApplication);
                allowed.push_back(O::StartApplication);
                require(expected.device_id == definition.policy.device_id && plan->operations == allowed,
                    "NATIVE_RECOVERY_PLAN_UNSAFE");
                event("recovery.plan", devices::lifecycle_plan_json(*plan));
                if (plan->defer_for > 0ms) {
                    const auto wake_at = std::chrono::steady_clock::now() + plan->defer_for;
                    require(wake_at <= total_deadline, "NATIVE_RECOVERY_EXCEEDS_RUN_DEADLINE");
                    event("recovery.deferred", {{"duration_ms", plan->defer_for.count()}});
                    std::unique_lock lock(mutex_);
                    complete_.wait_until(lock, wake_at, [this] { return stop_.load(); });
                    if (stop_) break;
                }
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
        const bool final_release = backend->release_owned_inputs();
        if (!quiescent && final_release)
            journal_->emit(last.terminal.generation,
                "cleanup.recovered", {{"initial_release", false}, {"final_release", true}});
        quiescent = final_release;
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
    if (quiescent && failure == "NATIVE_INPUT_CLEANUP_PENDING") {
        terminal.secondary_errors.push_back("INITIAL_INPUT_CLEANUP_FAILED_FINAL_RELEASE_CONFIRMED");
        failure = last.reason.empty() ? "NATIVE_SESSION_INCOMPLETE" : last.reason;
    }
    terminal.reason = std::move(failure);
    terminal.state = !quiescent ? contracts::RunState::Interrupted
        : stop_ ? contracts::RunState::UserStopped
        : !terminal.reason.empty() ? contracts::RunState::Failed
        : terminal.completed_business_units == definition.units.size()
            ? contracts::RunState::Completed : contracts::RunState::Failed;
    if (last.end == contracts::SessionEnd::BusinessFailed)
        terminal.outcome_category = "business_failed";
    if (!stop_ && quiescent && terminal.details_complete && business && definition.handoff_ready) {
        try {
            if (definition.handoff_ready(last, *business)) {
                terminal.state = contracts::RunState::Interrupted;
                terminal.outcome_category = "handoff_ready";
            }
        } catch (const std::exception &error) {
            terminal.secondary_errors.push_back(std::string("HANDOFF_VALIDATION:") + error.what());
        }
    }
    if (!stop_ && last.end == contracts::SessionEnd::ExternalBlocked) {
        terminal.state = contracts::RunState::Interrupted;
        terminal.outcome_category = "external_blocked";
    }
    if (!quiescent && terminal.reason.empty()) terminal.reason = "NATIVE_CLEANUP_PENDING";
    terminal.result_saved = true;
    store_->finish_recent_frames();
    if (definition.logging.memory && definition.logging.accepts(storage::LogLevel::Info))
        store_->append_log(terminal.generation, storage::LogLevel::Info, "memory",
            "worker_finishing", memory_record(platform::sample_memory()));
    try {
        const auto diagnostics = store_->diagnostic_summary();
        if (!diagnostics.at("action_timing").value("complete", false))
            terminal.secondary_errors.push_back("ACTION_TIMING_INCOMPLETE");
        if (!diagnostics.value("complete", false))
            terminal.secondary_errors.push_back("DIAGNOSTIC_INCOMPLETE");
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

void NativeRunCoordinator::record_combat_diagnostic(std::uint64_t generation, std::size_t index,
    const std::string &type, const nlohmann::json &data, const contracts::FrameEnvelope *frame,
    CombatDiagnosticState &state) noexcept {
    try {
        using J = nlohmann::json;
        const auto open_detail = [](const J &value) {
            const auto source = value.value("source_path", "");
            if (source.size() > 4096) return false;
            const auto path = J::parse(source, nullptr, false);
            return path.is_array() && !path.empty() && path.back().is_object() &&
                path.back().value("flow_id", "") == "combat-open-detail" &&
                path.back().value("node_id", "") == "open";
        };
        bool defended = false;
        if (type == "combat") {
            const auto operation = data.value("operation", "");
            if (operation == "prepare") {
                state = CombatDiagnosticState{};
                if (data.value("generation", 0ULL) != generation) return;
                state.selection = data.value("selection", J::object());
                if (!state.selection.is_object() || state.selection.dump().size() > 4096) {
                    state.selection = {{"error", "COMBAT_SELECTION_CONTEXT_UNAVAILABLE"}};
                    store_->note_diagnostic_hook_failure();
                }
                return;
            }
            if (state.opening.is_null()) return;
            if (operation != "defend_fallback_confirmed" ||
                data.value("generation", 0ULL) != generation ||
                !data.value("action_confirmed", false) || data.value("skill_confirmed", true) ||
                data.value("consumed", true)) {
                state.opening = nullptr;
                return;
            }
            defended = true;
        } else if (type == "input" && open_detail(data)) {
            if (data.value("state", "") == "accepted") {
                state.submission = J::object();
                for (const auto *key : {"sequence", "action_epoch", "basis_frame", "position", "target_center"})
                    if (data.contains(key)) state.submission[key] = data.at(key);
                return;
            }
            if (data.value("state", "") != "result") return;
            if (data.value("outcome", "") != "no_progress" ||
                data.value("delivery_unknown", true)) {
                state.opening = nullptr;
                return;
            }
            const auto operation = "combat-open-detail:" + std::to_string(generation) + ":" +
                std::to_string(data.value("action_epoch", 0ULL)) + ":" +
                std::to_string(data.value("observed_frame", 0ULL));
            if (state.opening.is_object() && state.opening.value("operation_id", "") == operation) return;
            state.opening = {{"operation_id", operation}, {"selection", state.selection},
                {"input_result", data}, {"submission", nullptr}};
            if (state.submission.is_object() &&
                state.submission.value("action_epoch", 0ULL) == data.value("action_epoch", 0ULL))
                state.opening["submission"] = state.submission;
        } else return;

        storage::DiagnosticRequest request;
        { std::lock_guard lock(mutex_); request.run_id = snapshot_.run_id; }
        request.generation = generation; request.unit_index = index;
        request.node = defended ? "combat.defend_fallback_confirmed" : "combat-open-detail/open";
        request.reason = "combat.open_detail_no_progress";
        request.stage = "postcondition";
        request.evidence_kind = defended ? "combat_defend_fallback" : "combat_open_no_progress";
        request.operation_id = state.opening.at("operation_id").get<std::string>();
        request.operation_scoped = true;
        const auto wanted = data.value(defended ? "frame_id" : "observed_frame", 0ULL);
        const auto epoch = state.opening.at("input_result").value("action_epoch", 0ULL);
        const auto opening_frame = state.opening.at("input_result").value("observed_frame", 0ULL);
        const auto connection = state.opening.value("connection_generation", 0ULL);
        const bool available = frame && wanted && epoch && frame->identity.generation == generation &&
            frame->identity.frame_id == wanted &&
            (defended ? wanted > opening_frame && frame->identity.action_epoch > epoch &&
                (!connection || frame->identity.connection_generation == connection)
                : frame->identity.action_epoch == epoch);
        if (!defended) {
            state.opening["connection_generation"] = available ? frame->identity.connection_generation : 0ULL;
            state.opening["opening_frame_available"] = available;
        }
        request.context = state.opening;
        if (defended) {
            request.context["defend_result"] = data;
            request.context["same_connection_as_opening"] = connection && frame
                ? J(frame->identity.connection_generation == connection) : J(nullptr);
        }
        // 只保存回执所属的原帧。缺图/错代次按缺失记录，不补拍另一张冒充失败现场。
        request.context["expected_frame_id"] = wanted;
        request.context["frame_available"] = available;
        if (request.context.dump().size() > 16 * 1024) {
            request.context = {{"error", "COMBAT_DIAGNOSTIC_CONTEXT_TOO_LARGE"},
                {"expected_frame_id", wanted}, {"frame_available", available}};
            store_->note_diagnostic_hook_failure();
        }
        // 清除关联后才做I/O；落盘失败不能把下一行动串成同一次防御，也不能重试输入。
        if (defended) state.opening = nullptr;
        auto saved = store_->save_diagnostic(available ? frame : nullptr, request);
        saved["operation_id"] = request.operation_id;
        saved["unit_index"] = request.unit_index;
        saved["node"] = request.node;
        saved["reason"] = request.reason;
        saved["stage"] = request.stage;
        saved["evidence_kind"] = request.evidence_kind;
        saved["context"] = request.context;
        journal_->emit(generation, "diagnostic.combat_no_progress", std::move(saved));
    } catch (...) {
        state.opening = nullptr;
        store_->note_diagnostic_hook_failure();
    }
}

void NativeRunCoordinator::save_application_restart_diagnostic(std::uint64_t generation,
    std::size_t index, const nlohmann::json &progress, const contracts::FrameEnvelope *frame) noexcept {
    try {
        storage::DiagnosticRequest request;
        { std::lock_guard lock(mutex_); request.run_id = snapshot_.run_id; }
        request.generation = generation; request.unit_index = index;
        request.node = progress.value("step_id", ""); request.reason = "CONTINUOUS_EXCEPTION_TIMEOUT";
        request.stage = "recovery_entry"; request.evidence_kind = "before_application_restart";
        journal_->emit(generation, "diagnostic.application_restart", store_->save_diagnostic(frame, request));
    } catch (...) { store_->note_diagnostic_hook_failure(); }
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
