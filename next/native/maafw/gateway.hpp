#pragma once
#include "buffers.hpp"
#include "custom_recognition.hpp"
#include "guarded_controller.hpp"
#include "recognition.hpp"
#include "contracts/business_state.hpp"
#include <map>
#include <vector>

namespace wvd::maafw {
class MaaGateway;
struct ChildResult {
    std::int64_t id{};
    int status{};
    bool valid{};
};
class Context {
  public:
    std::int64_t task_id() const { return task_; }
    const std::string &node() const { return node_; }
    int depth() const;
    bool cancelled() const;
    contracts::FrameEnvelope capture();
    contracts::Observation recognize(const contracts::FrameEnvelope &frame,
                                     const RecognitionRequest &request);
    ChildResult run_child(const std::string &entry,
                          const nlohmann::json &overrides = nlohmann::json::object(),
                          bool clone = false, const std::vector<std::string> &reset_hit_counts = {});
    bool native_action(const std::string &type, const nlohmann::json &parameters);
    bool controller_action(const contracts::Command &command);
    nlohmann::json node_data(const std::string &name) const;
    bool with_business_state(const std::function<bool(contracts::BusinessRunState &)> &operation);
    void business_event(const std::string &type, const nlohmann::json &payload);

  private:
    friend class MaaGateway;
    Context(MaaGateway &gateway, MaaContext *context, MaaTaskId task, std::string node)
        : gateway_(gateway), context_(context), task_(task), node_(std::move(node)) {}
    MaaGateway &gateway_;
    MaaContext *context_;
    MaaTaskId task_;
    std::string node_;
};
using CustomAction = std::function<bool(Context &, const nlohmann::json &)>;
using ActionRegistry = std::map<std::string, CustomAction>;
struct GatewayHooks {
    std::function<bool()> cancelled = [] { return false; };
    std::function<void(const std::string &)> failure = [](const auto &) {};
    std::function<void(const std::string &, const nlohmann::json &)> event = [](const auto &,
                                                                                const auto &) {};
    std::function<void(const ChildResult &)> child = [](const auto &) {};
};
class MaaGateway {
  public:
    MaaGateway(Bundle bundle, devices::InputGate *gate = nullptr, GatewayHooks hooks = {},
               ActionRegistry actions = {}, RecognitionHandlers recognitions = {},
               contracts::BusinessRunState *business = nullptr);
    ~MaaGateway();
    MaaGateway(const MaaGateway &) = delete;
    MaaGateway &operator=(const MaaGateway &) = delete;
    void initialize();
    std::int64_t post(const std::string &entry);
    int status(std::int64_t id) const;
    bool running() const;
    void request_stop();
    void close() noexcept;
    nlohmann::json bundle_status() const;
    unsigned active_callbacks() const { return activity_.count.load(); }
    contracts::FrameEnvelope capture();
    bool controller_action(const contracts::Command &command);
    contracts::Observation recognize(const contracts::FrameEnvelope &frame,
                                     const contracts::FrameIdentity &current,
                                     const RecognitionRequest &request,
                                     MaaContext *context = nullptr);

  private:
    friend class Context;
    static MaaBool action_callback(MaaContext *, MaaTaskId, const char *, const char *,
                                   const char *, MaaRecoId, const MaaRect *, void *) noexcept;
    static void event_callback(void *, const char *, const char *, void *) noexcept;
    static MaaBool recognition_callback(MaaContext *, MaaTaskId, const char *, const char *,
                                        const char *, const MaaImageBuffer *, const MaaRect *,
                                        void *, MaaRect *, MaaStringBuffer *) noexcept;
    Bundle bundle_;
    devices::InputGate *gate_;
    GatewayHooks hooks_;
    ActionRegistry actions_;
    RecognitionHandlers recognitions_;
    contracts::BusinessRunState *business_;
    RecognitionCache recognition_cache_;
    std::mutex recognition_mutex_;
    std::mutex direct_recognition_mutex_;
    std::uint64_t recognition_invocation_{};
    struct VerifiedInvocation {
        std::string token, binding;
        nlohmann::json parameters;
        contracts::Box roi;
        std::uint64_t id{};
        bool used{};
    };
    std::optional<VerifiedInvocation> verified_invocation_;
    CallbackActivity activity_;
    std::atomic<int> depth_{};
    Handle<MaaResource, MaaResourceDestroy> resource_{nullptr, MaaResourceDestroy};
    std::unique_ptr<GuardedController> controller_callbacks_;
    Handle<MaaController, MaaControllerDestroy> controller_{nullptr, MaaControllerDestroy};
    Handle<MaaTasker, MaaTaskerDestroy> tasker_{nullptr, MaaTaskerDestroy};
    MaaSinkId sink_{}, context_sink_{};
    bool stop_posted_{}, initialized_{}, initialization_started_{};
};
} // namespace wvd::maafw
