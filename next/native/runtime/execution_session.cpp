#include "execution_session.hpp"
#include "guarded_action.hpp"
#include "devices/lifecycle_execution.hpp"

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
                                   contracts::BusinessRunState *business, contracts::SegmentBoundary boundary)
    : definition_(std::move(definition)), registry_(std::move(registry)), business_(business),
      events_(events), gate_(backend, policy, run, generation, events), backend_(backend) {
    if (!registry_)
        throw std::runtime_error("REGISTRY_REQUIRED");
    registry_->validate(definition_);
    if (definition_.lifecycle) {
        const auto &target = definition_.lifecycle->target;
        if (boundary != contracts::SegmentBoundary::LifecycleRecovery || !backend.offline() ||
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
bool ExecutionSession::cancelled() const { return user_stop_ || abort_ || recovery_; }
void ExecutionSession::fail(const std::string &reason) {
    gate_.close();
    abort_ = true;
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
        maafw::GatewayHooks hooks;
        hooks.cancelled = [this] { return cancelled(); };
        hooks.failure = [this](const auto &reason) { fail(reason); };
        hooks.event = [this](const auto &type, const auto &data) {
            events_.emit(gate_.generation(), type, data);
        };
        hooks.child = [this](const maafw::ChildResult &child) {
            events_.emit(gate_.generation(), "child.result",
                         {{"id", child.id}, {"valid", child.valid}, {"status", child.status}});
            if (!child.valid || child.status != MaaStatus_Succeeded)
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
        add("RunChild", [](maafw::Context &context, const auto &parameters) {
            auto child = context.run_child(parameters.at("entry"),
                                           parameters.value("overrides", nlohmann::json::object()),
                                           parameters.value("clone", false),
                                           parameters.value("reset_hit_counts", std::vector<std::string>{}));
            return child.valid && child.status == MaaStatus_Succeeded;
        });
        add("RequireRecovery", [this](maafw::Context &, const auto &parameters) {
            const auto reason = parameters.is_null()
                                    ? std::string("unspecified")
                                    : parameters.value("reason", std::string("unspecified"));
            if (reason.size() > 256)
                throw std::runtime_error("RECOVERY_REASON_INVALID");
            recovery_ = true;
            gate_.close();
            {
                std::lock_guard lock(mutex_);
                result_.reason = reason;
            }
            events_.emit(gate_.generation(), "session.recovery_required", {{"reason", reason}},
                         true);
            return false;
        });
        add("GuardedAction", [this](maafw::Context &context, const auto &parameters) {
            return GuardedAction::execute(context, gate_, events_, parameters);
        });
        maafw::MaaGateway gateway(definition_.bundle, &gate_, std::move(hooks), std::move(actions),
                                  registry_->bind_recognitions(definition_.recognitions),
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
        if (user_stop_)
            result_.end = SessionEnd::UserStopped;
        else if (recovery_)
            result_.end = SessionEnd::RecoveryRequired;
        else if (!abort_ && result_.engine_status == MaaStatus_Succeeded &&
                 root.task_id == result_.root_task_id && root.generation == gate_.generation() &&
                 root.depth == 0 && root.node == definition_.terminal_node && checkpoint_valid)
            result_.end = SessionEnd::Completed;
        else {
            result_.end = SessionEnd::Failed;
            if (result_.reason.empty())
                result_.reason =
                    checkpoint_valid ? "ROOT_TERMINAL_MISSING" : "BUSINESS_CHECKPOINT_MISSING";
        }
        done_ = true;
    }
    cv_.notify_all();
}
} // namespace wvd::runtime
