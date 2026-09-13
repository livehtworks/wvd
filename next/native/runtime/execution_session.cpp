#include "execution_session.hpp"
#include "guarded_action.hpp"

namespace wvd::runtime {
using namespace contracts;
using namespace std::chrono_literals;
ExecutionSession::ExecutionSession(SessionDefinition definition, devices::DeviceBackend &backend,
                                   InputPolicy policy, std::uint64_t run, std::uint64_t generation,
                                   storage::EventJournal &events)
    : definition_(std::move(definition)), events_(events),
      gate_(backend, std::move(policy), run, generation, events) {}
ExecutionSession::~ExecutionSession() {
    request_stop();
    if (worker_.joinable())
        worker_.join();
}
void ExecutionSession::start() {
    // join 后线程不再 joinable，但会话的历史/门禁不能因此重新用于另一轮执行。
    if (started_.exchange(true))
        throw std::runtime_error("SESSION_ALREADY_STARTED");
    worker_ = std::thread([this] { execute(); });
}
void ExecutionSession::request_stop() {
    user_stop_ = true;
    gate_.close();
}
bool ExecutionSession::cancelled() const {
    return user_stop_ || abort_ || recovery_;
}
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
void ExecutionSession::execute() noexcept {
    try {
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
        auto actions = definition_.actions;
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
        add("RunChild", [](maafw::Context &context, const auto &parameters) {
            auto child = context.run_child(parameters.at("entry"),
                                           parameters.value("overrides", nlohmann::json::object()),
                                           parameters.value("clone", false));
            return child.valid && child.status == MaaStatus_Succeeded;
        });
        add("RequireRecovery", [this](maafw::Context &, const auto &) {
            recovery_ = true;
            gate_.close();
            events_.emit(gate_.generation(), "session.recovery_required", {}, true);
            return false;
        });
        add("GuardedAction", [this](maafw::Context &context, const auto &parameters) {
            return GuardedAction::execute(context, gate_, events_, parameters);
        });
        maafw::MaaGateway gateway(definition_.bundle, &gate_, std::move(hooks), std::move(actions));
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
    running_ = false;
    {
        std::lock_guard lock(mutex_);
        result_.inputs = gate_.counts();
        result_.quiescent = true;
        const auto &root = result_.terminal;
        if (user_stop_)
            result_.end = SessionEnd::UserStopped;
        else if (recovery_)
            result_.end = SessionEnd::RecoveryRequired;
        else if (!abort_ && result_.engine_status == MaaStatus_Succeeded &&
                 root.task_id == result_.root_task_id && root.generation == gate_.generation() &&
                 root.depth == 0 && root.node == definition_.terminal_node)
            result_.end = SessionEnd::Completed;
        else {
            result_.end = SessionEnd::Failed;
            if (result_.reason.empty())
                result_.reason = "ROOT_TERMINAL_MISSING";
        }
        done_ = true;
    }
    cv_.notify_all();
}
} // namespace wvd::runtime
