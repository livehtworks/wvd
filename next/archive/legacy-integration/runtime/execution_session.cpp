#include "execution_session.hpp"
#include "guarded_action.hpp"
#include "flow_events.hpp"
#include "devices/lifecycle_execution.hpp"
#include <thread>

namespace wvd::runtime {
using namespace contracts;
using namespace std::chrono_literals;
namespace {
struct LifecycleNotReady {};
}
ExecutionSession::ExecutionSession(SessionDefinition definition, devices::DeviceBackend &backend,
                                   InputPolicy policy, std::uint64_t run, std::uint64_t generation,
                                   storage::EventJournal &events,
                                   std::shared_ptr<const BehaviorRegistry> registry,
                                   contracts::BusinessRunState *business, contracts::SegmentBoundary boundary,
                                   storage::RunStore *diagnostic_store, std::size_t unit_index)
    : definition_(std::move(definition)), registry_(std::move(registry)), business_(business),
      events_(events), diagnostic_store_(diagnostic_store), unit_index_(unit_index),
      gate_(backend, policy, run, generation, events), backend_(backend) {
    if (!registry_)
        throw std::runtime_error("REGISTRY_REQUIRED");
    registry_->validate(definition_);
    if (definition_.lifecycle) {
        const auto &plan = *definition_.lifecycle;
        const auto &target = plan.target;
        const bool initial_vpn = boundary == contracts::SegmentBoundary::Initial &&
            devices::initial_lifecycle_plan(plan);
        if ((!initial_vpn && boundary != contracts::SegmentBoundary::LifecycleRecovery) ||
            (!backend.offline() && !backend.verified_access()) ||
            policy.observed_read_only_viewport || target.device_id != policy.device_id ||
            target.application_id != policy.application_id || !backend.lifecycle_port())
            throw std::runtime_error("LIFECYCLE_NOT_AUTHORIZED");
    }
}
ExecutionSession::~ExecutionSession() {
    request_stop();
    if (worker_.joinable())
        worker_.join();
}
nlohmann::json ExecutionSession::event_status() const {
    std::shared_ptr<FlowEvents> events;
    {
        std::lock_guard lock(mutex_);
        events = flow_events_;
    }
    return events ? events->status() : nlohmann::json(nullptr);
}
void ExecutionSession::start() {
    // join 后线程不再 joinable，但会话的历史/门禁不能因此重新用于另一轮执行。
    if (started_.exchange(true))
        throw std::runtime_error("SESSION_ALREADY_STARTED");
    try {
        worker_ = std::thread([this] { execute(); });
    } catch (...) {
        std::lock_guard lock(mutex_);
        result_.reason = "SESSION_THREAD_START_FAILED";
        result_.quiescent = true;
        done_ = true;
        cv_.notify_all();
        throw;
    }
}
void ExecutionSession::request_stop() {
    user_stop_ = true;
    gate_.close();
}
bool ExecutionSession::cancelled() const { return user_stop_ || abort_ || recovery_ || external_blocked_; }
void ExecutionSession::fail(const std::string &reason) {
    if ((external_blocked_ || user_stop_) &&
        (reason == "INPUT_CLOSED" || reason == "SESSION_CANCELLED"))
        return;
    gate_.close();
    abort_ = true;
    try {
        events_.emit(gate_.generation(), "session.failure", {{"reason", reason}});
    } catch (...) {
    }
    std::lock_guard lock(mutex_);
    if (result_.reason.empty())
        result_.reason = reason;
}
bool ExecutionSession::wait_for(std::chrono::milliseconds duration) {
    std::unique_lock lock(mutex_);
    return cv_.wait_for(lock, duration, [&] { return done_; });
}
SessionResult ExecutionSession::join() {
    if (worker_.joinable())
        worker_.join();
    std::lock_guard lock(mutex_);
    return result_;
}
void ExecutionSession::prepare_lifecycle() {
    if (!definition_.lifecycle)
        return;
    auto *port = backend_.lifecycle_port();
    if (!port)
        throw std::runtime_error("LIFECYCLE_PORT_UNAVAILABLE");
    if (cancelled()) throw LifecycleNotReady{};
    // 所有已授权生命周期会话均先恢复本进程的连接对象，普通首段与恢复段相同。
    // connect 只连接，不以缺少 Controller 推断游戏崩溃，也不重启实例。
    if (!backend_.connect())
        throw std::runtime_error("LIFECYCLE_DEVICE_CONNECT_FAILED");
    if (cancelled()) throw LifecycleNotReady{};
    const auto outcome = devices::execute_lifecycle_plan(*definition_.lifecycle, *port,
        [this] { return cancelled(); }, [this](const auto &type, const auto &payload) {
            events_.emit(gate_.generation(), type, payload, true);
        });
    if (outcome == devices::LifecycleEnd::ReadyForBoot)
        return;
    if (outcome == devices::LifecycleEnd::RetryRequired) {
        recovery_ = true;
        std::lock_guard lock(mutex_);
        result_.reason = "LIFECYCLE_RETRY_REQUIRED";
    }
    throw LifecycleNotReady{};
}
void ExecutionSession::execute() noexcept {
    try {
        // 上一代次由协调器 join 后才创建本段；此处尚未创建 SDK Controller。
        // 生命周期成功只允许进入启动识别图，不能直接形成根终态。
        prepare_lifecycle();
        auto flow_events = std::make_shared<FlowEvents>(definition_.event_scopes, gate_, events_, [this](const std::string &reason) {
            gate_.close();
            external_blocked_ = true;
            std::lock_guard lock(mutex_);
            if (result_.reason.empty()) result_.reason = reason;
            result_.outcome_category = "external_blocked";
        });
        {
            std::lock_guard lock(mutex_);
            flow_events_ = flow_events;
        }
        maafw::GatewayHooks hooks;
        hooks.cancelled = [this] { return cancelled(); };
        hooks.failure = [this](const auto &reason) { fail(reason); };
        hooks.event = [this](const auto &type, const auto &data) {
            events_.emit(gate_.generation(), type, data);
        };
        hooks.event_check = [flow_events](maafw::Context &context,
            const contracts::FrameEnvelope &frame, contracts::FlowEventPhase phase,
            const std::string &source_node) {
            return flow_events->check(context, frame, phase, source_node);
        };
        hooks.event_replan_pending = [flow_events](const std::string &source_node) {
            return flow_events->route_pending_for_source(source_node);
        };
        hooks.event_scope_enabled = [flow_events](const std::string &source_node) {
            return flow_events->scope_enabled(source_node);
        };
        if (diagnostic_store_) {
            // store固定属于本Run，由协调器持有到全部回调结束、Session join及终态保存后。
            hooks.diagnostic = [this](const contracts::FrameEnvelope *frame, storage::DiagnosticRequest request) {
                request.unit_index = unit_index_;
                try {
                    const auto receipt = diagnostic_store_->save_diagnostic(frame, request);
                    const auto status = receipt.value("status", "");
                    // save已释放专属mutex；节流/配额只累计，不能把被抑制的请求转成无限事件。
                    if (status == "saved" || status == "failed")
                        events_.emit(gate_.generation(), "diagnostic." + status, receipt);
                } catch (...) {
                    diagnostic_store_->note_diagnostic_hook_failure();
                }
            };
        }
        hooks.child = [this](const maafw::ChildResult &child) {
            events_.emit(gate_.generation(), "child.result",
                         {{"id", child.id}, {"valid", child.valid}, {"status", child.status}});
            if (!external_blocked_ && !user_stop_ && (!child.valid || child.status != MaaStatus_Succeeded))
                fail("CHILD_FAILED");
        };
        auto actions = registry_->bind(definition_.actions);
        auto add = [&](const std::string &name, maafw::CustomAction action) {
            if (!actions.emplace(name, std::move(action)).second)
                throw std::runtime_error("RESERVED_ACTION_OVERRIDE");
        };
        add("RootTerminal", [this](maafw::Context &context, const auto &) {
            RootEvidence evidence{context.task_id(), gate_.generation(), context.depth(),
                                  context.node()};
            events_.emit(
                gate_.generation(), "root.evidence",
                {{"task_id", evidence.task_id}, {"depth", evidence.depth}, {"node", evidence.node}},
                true);
            if (evidence.depth == 0 && evidence.node == definition_.terminal_node) {
                std::lock_guard lock(mutex_);
                result_.terminal = std::move(evidence);
            }
            return true;
        });
        add("BusinessCheckpoint", [this](maafw::Context &context, const auto &) {
            if (!business_ || definition_.checkpoint_node.empty())
                throw std::runtime_error("BUSINESS_CHECKPOINT_NOT_DECLARED");
            if (cancelled())
                return false;
            // 子流程不能替父业务段确认检查点；task_id 在原生根调用返回后再次核对。
            if (context.depth() == 0 && context.node() == definition_.checkpoint_node) {
                std::lock_guard lock(mutex_);
                result_.checkpoint = {context.task_id(), gate_.generation(), context.depth(),
                                      context.node()};
            }
            return true;
        });
        add("CancelableWait", [](maafw::Context &context, const auto &parameters) {
            const auto duration = parameters.at("duration_ms").get<int>();
            if (duration < 1 || duration > 10000)
                throw std::runtime_error("WAIT_DURATION_INVALID");
            auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(duration);
            if (!context.has_event_scope()) {
                while (!context.cancelled() && std::chrono::steady_clock::now() < deadline)
                    std::this_thread::sleep_for(25ms);
                return !context.cancelled();
            }
            auto next_check = std::chrono::steady_clock::now();
            while (!context.cancelled() && std::chrono::steady_clock::now() < deadline) {
                if (std::chrono::steady_clock::now() >= next_check) {
                    const auto frame = context.capture();
                    for (const auto phase : {contracts::FlowEventPhase::Overlay,
                                             contracts::FlowEventPhase::Encounter}) {
                        const auto event = context.check_events(frame, phase);
                        if (event.outcome == contracts::FlowEventOutcome::ExternalBlocked ||
                            event.outcome == contracts::FlowEventOutcome::Cancelled) return false;
                        if (event.outcome == contracts::FlowEventOutcome::Replan) return true;
                        if (event.outcome == contracts::FlowEventOutcome::Handled) {
                            deadline += event.elapsed;
                            break;
                        }
                        if (event.outcome == contracts::FlowEventOutcome::Reobserve) break;
                    }
                    next_check = std::chrono::steady_clock::now() + 100ms;
                }
                std::this_thread::sleep_for(25ms);
            }
            return !context.cancelled();
        });
        add("RunChild", [](maafw::Context &context, const auto &parameters) {
            auto child = context.run_child(parameters.at("entry"),
                                           parameters.value("overrides", nlohmann::json::object()),
                                           parameters.value("clone", false),
                                           parameters.value("reset_hit_counts", std::vector<std::string>{}));
            return child.valid && child.status == MaaStatus_Succeeded;
        });
        add("RequireRecovery", [this](maafw::Context &context, const auto &parameters) {
            const auto reason = parameters.is_null()
                                    ? std::string("unspecified")
                                    : parameters.value("reason", std::string("unspecified"));
            if (reason.size() > 256)
                throw std::runtime_error("RECOVERY_REASON_INVALID");
            {
                std::lock_guard lock(mutex_);
                if (result_.reason.empty()) result_.reason = reason;
            }
            // capture的底层异常会先调用fail；提前保留首因，但不能提前关门/设recovery_。
            // 这里只记录恢复入口的新图，不声称它就是触发恢复的原因帧。
            if (diagnostic_store_ && reason != "leap.wait_boundary" && !cancelled()) {
                try {
                    const auto frame = context.capture();
                    gate_.close();
                    context.save_diagnostic(&frame, reason, "recovery_entry");
                } catch (const std::exception &error) {
                    gate_.close();
                    context.save_diagnostic(nullptr, reason, "recovery_entry", {}, error.what());
                } catch (...) {
                    gate_.close();
                    context.save_diagnostic(nullptr, reason, "recovery_entry", {}, "DIAGNOSTIC_CAPTURE_FAILED");
                }
            }
            recovery_ = true;
            gate_.close();
            events_.emit(gate_.generation(), "session.recovery_required", {{"reason", reason}},
                         true);
            return false;
        });
        add("GuardedAction", [this](maafw::Context &context, const auto &parameters) {
            return GuardedAction::execute(context, gate_, events_, parameters);
        });
        add("DispatchEvent", [flow_events](maafw::Context &context, const auto &parameters) {
            const auto frame = context.capture();
            const auto kind = parameters.at("class").get<std::string>();
            const auto result = flow_events->check(context, frame,
                kind == "overlay" ? contracts::FlowEventPhase::Overlay : contracts::FlowEventPhase::Encounter,
                parameters.at("source_node").get<std::string>(),
                parameters.at("event_id").get<std::string>());
            return result.outcome != contracts::FlowEventOutcome::ExternalBlocked &&
                   result.outcome != contracts::FlowEventOutcome::Cancelled &&
                   result.outcome != contracts::FlowEventOutcome::Error;
        });
        add("ConsumeEventRoute", [this, flow_events](maafw::Context &context, const auto &parameters) {
            if (context.cancelled()) return false;
            const auto source = parameters.at("source_node").template get<std::string>();
            const auto event = parameters.at("event_id").template get<std::string>();
            const auto target = parameters.at("target").template get<std::string>();
            flow_events->consume_route(source, event, target);
            events_.emit(gate_.generation(), "flow_event.replanned",
                         {{"source_node", source}, {"event_id", event}, {"target", target}}, true);
            return true;
        });
        add("AwaitTransition", [this](maafw::Context &context, const auto &parameters) {
            return GuardedAction::await_transition(context, gate_, events_, parameters);
        });
        add("BeginObservationPhase", [this](maafw::Context &context, const auto &parameters) {
            if (context.cancelled()) return false;
            gate_.begin_observation_phase(context.task_id(),
                parameters.at("phase").template get<std::string>(),
                std::chrono::milliseconds(parameters.at("budget_ms").template get<std::int64_t>()));
            return true;
        });
        add("EndObservationPhase", [this](maafw::Context &context, const auto &parameters) {
            if (context.cancelled()) return false;
            gate_.end_observation_phase(context.task_id(),
                parameters.at("phase").template get<std::string>());
            return true;
        });
        auto recognitions = registry_->bind_recognitions(definition_.recognitions);
        if (!recognitions.emplace("EventRoute", [flow_events](const maafw::Bundle &,
            maafw::RecognitionPixels, const nlohmann::json &parameters,
            const maafw::CustomRecognitionScope &, maafw::RecognitionCache &) {
            const auto hit = flow_events->route_pending(
                parameters.at("source_node").get<std::string>(),
                parameters.at("event_id").get<std::string>(),
                parameters.at("target").get<std::string>());
            return nlohmann::json{{"schema", 1}, {"outcome", hit ? "Hit" : "NoHit"},
                {"box", hit ? nlohmann::json::array({0, 0, 1, 1}) : nlohmann::json(nullptr)},
                {"target", false}, {"evidence", nlohmann::json::object()}};
        }).second) throw std::runtime_error("RESERVED_RECOGNITION_OVERRIDE");
        maafw::MaaGateway gateway(definition_.bundle, &gate_, std::move(hooks), std::move(actions),
                                  std::move(recognitions),
                                  business_);
        gateway.initialize();
        running_ = true;
        auto id = gateway.post(definition_.entry);
        {
            std::lock_guard lock(mutex_);
            result_.root_task_id = id;
        }
        events_.emit(gate_.generation(), "session.root_started", {{"task_id", id}}, true);
        while (gateway.running() || gateway.status(id) == MaaStatus_Pending ||
               gateway.status(id) == MaaStatus_Running) {
            // Boot 被内联进长任务后仍保留自身总预算；不能因每次回入口重置。
            if (!cancelled() && std::chrono::steady_clock::now() > gate_.observation_deadline())
                fail("OBSERVATION_PHASE_TIMEOUT");
            if (cancelled())
                gateway.request_stop();
            std::this_thread::sleep_for(5ms);
        }
        {
            std::lock_guard lock(mutex_);
            result_.engine_status = gateway.status(id);
        }
        gate_.close();
        while (gateway.active_callbacks())
            std::this_thread::sleep_for(5ms);
        while (!gate_.release_held()) {
            fail("INPUT_RELEASE_FAILED");
            std::this_thread::sleep_for(100ms);
        }
        gateway.close();
    } catch (const LifecycleNotReady &) {
        // 仍走下面的统一收尾与真实静止检查，不能在准备阶段提前 return。
    } catch (const std::exception &error) {
        try {
            fail(error.what());
        } catch (...) {
            abort_ = true;
        }
    } catch (...) {
        try {
            fail("SESSION_EXCEPTION");
        } catch (...) {
            abort_ = true;
        }
    }
    gate_.close();
    // 即使初始化或 Custom 抛异常，已按下的键/触点也必须真实释放后才能报告静止。
    while (!gate_.quiescent()) {
        try {
            gate_.release_held();
        } catch (...) {
        }
        std::this_thread::sleep_for(100ms);
    }
    gate_.disconnect_backend();
    running_ = false;
    {
        std::lock_guard lock(mutex_);
        result_.inputs = gate_.counts();
        result_.quiescent = true;
        const auto &root = result_.terminal;
        const auto &checkpoint = result_.checkpoint;
        const bool checkpoint_valid =
            !business_ || (checkpoint.task_id == result_.root_task_id &&
                           checkpoint.generation == gate_.generation() && checkpoint.depth == 0 &&
                           checkpoint.node == definition_.checkpoint_node);
        if (abort_)
            result_.end = SessionEnd::Failed;
        else if (user_stop_)
            result_.end = SessionEnd::UserStopped;
        else if (external_blocked_)
            result_.end = SessionEnd::ExternalBlocked;
        else if (recovery_)
            result_.end = SessionEnd::RecoveryRequired;
        else if (!abort_ && result_.engine_status == MaaStatus_Succeeded &&
                 root.task_id == result_.root_task_id && root.generation == gate_.generation() &&
                 root.depth == 0 && root.node == definition_.terminal_node && checkpoint_valid &&
                 !gate_.has_pending_submission() && gate_.event_depth() == 0 &&
                 !(flow_events_ && flow_events_->has_pending_route()))
            result_.end = SessionEnd::Completed;
        else {
            result_.end = SessionEnd::Failed;
            if (result_.reason.empty())
                result_.reason = gate_.has_pending_submission() ? "PENDING_INPUT_RESULT_UNCONFIRMED" :
                    flow_events_ && flow_events_->has_pending_route() ? "EVENT_REPLAN_ROUTE_UNCONSUMED" :
                    checkpoint_valid ? "ROOT_TERMINAL_MISSING" : "BUSINESS_CHECKPOINT_MISSING";
        }
        done_ = true;
    }
    cv_.notify_all();
}
} // namespace wvd::runtime
