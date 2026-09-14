#include "gateway.hpp"
#include "preflight.hpp"
#include "storage/runtime_bundle.hpp"
#include "platform/windows/bundle_lease.hpp"
#include <algorithm>
#include <thread>

namespace wvd::maafw {
void Context::business_event(const std::string &type, const nlohmann::json &payload) {
    if (type.empty() || type.size() > 96 || !payload.is_object() || payload.dump().size() > 4096)
        throw std::runtime_error("BUSINESS_EVENT_INVALID");
    auto event = payload;
    event["node"] = node_;
    event["task_id"] = task_;
    gateway_.hooks_.event("business." + type, event);
}
using namespace std::chrono_literals;
namespace {
void require(bool value, const char *code) {
    if (!value)
        throw std::runtime_error(code);
}
bool pending(int status) { return status == MaaStatus_Pending || status == MaaStatus_Running; }
} // namespace
MaaGateway::MaaGateway(Bundle bundle, devices::InputGate *gate, GatewayHooks hooks,
                       ActionRegistry actions, RecognitionHandlers recognitions,
                       contracts::BusinessRunState *business)
    : bundle_(std::move(bundle)), gate_(gate), hooks_(std::move(hooks)),
      actions_(std::move(actions)), recognitions_(std::move(recognitions)), business_(business) {
    activity_.failure = hooks_.failure;
}
MaaGateway::~MaaGateway() { close(); }
void MaaGateway::initialize() {
    require(!initialization_started_, "GATEWAY_ALREADY_INITIALIZED");
    initialization_started_ = true;
    require(std::string(MaaVersion()) == "v5.13.0", "SDK_VERSION_MISMATCH");
    require(!bundle_.revision.empty() && !bundle_.files.empty(), "BUNDLE_MANIFEST_INVALID");
    bundle_ = storage::materialize_bundle(bundle_);
    hooks_.event("bundle.sealed", bundle_status());
    require(!hooks_.cancelled(), "SESSION_CANCELLED");
    resource_.reset(MaaResourceCreate());
    require(bool(resource_), "RESOURCE_CREATE_FAILED");
    MaaInferenceExecutionProvider cpu = MaaInferenceExecutionProvider_CPU;
    require(MaaResourceSetOption(resource_.get(), MaaResOption_InferenceExecutionProvider, &cpu,
                                 sizeof(cpu)),
            "CPU_PROVIDER_FAILED");
    auto load = MaaResourcePostBundle(resource_.get(), utf8(bundle_.root).c_str());
    require(load != MaaInvalidId, "BUNDLE_POST_FAILED");
    while (pending(MaaResourceStatus(resource_.get(), load)))
        std::this_thread::sleep_for(5ms);
    require(MaaResourceStatus(resource_.get(), load) == MaaStatus_Succeeded, "BUNDLE_LOAD_FAILED");
    require(!hooks_.cancelled(), "SESSION_CANCELLED");
    for (const auto &[name, action] : actions_) {
        require(bool(action), "CUSTOM_ACTION_EMPTY");
        require(
            MaaResourceRegisterCustomAction(resource_.get(), name.c_str(), action_callback, this),
            "CUSTOM_REGISTRATION_FAILED");
    }
    for (const auto &[name, recognition] : recognitions_)
        require(MaaResourceRegisterCustomRecognition(resource_.get(), name.c_str(),
                                                     recognition_callback, this),
                "CUSTOM_RECO_REGISTRATION_FAILED");
    if (gate_) {
        controller_callbacks_ = std::make_unique<GuardedController>(*gate_, activity_);
        controller_.reset(MaaCustomControllerCreate(controller_callbacks_->callbacks(),
                                                    controller_callbacks_.get()));
        require(bool(controller_), "CONTROLLER_CREATE_FAILED");
        int32_t short_side = std::min(gate_->policy().recognition_size.width,
                                      gate_->policy().recognition_size.height);
        bool raw = gate_->policy().observed_read_only_viewport;
        require(MaaControllerSetOption(controller_.get(), MaaCtrlOption_ScreenshotTargetShortSide,
                                       &short_side, sizeof(short_side)) &&
                    MaaControllerSetOption(controller_.get(), MaaCtrlOption_ScreenshotUseRawSize,
                                           &raw, sizeof(raw)),
                "CONTROLLER_SIZE_FAILED");
        auto connection = MaaControllerPostConnection(controller_.get());
        require(connection != MaaInvalidId, "CONTROLLER_CONNECTION_POST_FAILED");
        while (pending(MaaControllerStatus(controller_.get(), connection)))
            std::this_thread::sleep_for(5ms);
        require(MaaControllerStatus(controller_.get(), connection) == MaaStatus_Succeeded,
                "CONTROLLER_CONNECT_FAILED");
        require(!hooks_.cancelled(), "SESSION_CANCELLED");
    }
    tasker_.reset(MaaTaskerCreate());
    require(tasker_ && MaaTaskerBindResource(tasker_.get(), resource_.get()) &&
                (!controller_ || MaaTaskerBindController(tasker_.get(), controller_.get())) &&
                MaaTaskerInited(tasker_.get()),
            "TASKER_INITIALIZATION_FAILED");
    sink_ = MaaTaskerAddSink(tasker_.get(), event_callback, this);
    context_sink_ = MaaTaskerAddContextSink(tasker_.get(), event_callback, this);
    initialized_ = true;
}
MaaBool MaaGateway::recognition_callback(MaaContext *, MaaTaskId, const char *, const char *name,
                                         const char *parameters, const MaaImageBuffer *image,
                                         const MaaRect *roi, void *argument, MaaRect *output,
                                         MaaStringBuffer *detail) noexcept {
    auto &self = *static_cast<MaaGateway *>(argument);
    CallbackScope activity(self.activity_);
    std::lock_guard recognition_lock(self.recognition_mutex_);
    nlohmann::json result;
    try {
        require(!self.hooks_.cancelled(), "SESSION_CANCELLED");
        require(image && MaaImageBufferGetRawData(image) && MaaImageBufferType(image) == 16,
                "CUSTOM_IMAGE_INVALID");
        auto size = contracts::Size{MaaImageBufferWidth(image), MaaImageBufferHeight(image)};
        auto params = nlohmann::json::parse(parameters);
        require(roi && roi->x >= 0 && roi->y >= 0 && roi->width > 0 && roi->height > 0 &&
                    roi->width <= size.width && roi->height <= size.height &&
                    roi->x <= size.width - roi->width && roi->y <= size.height - roi->height,
                "CUSTOM_SCOPE_INVALID");
        std::uint64_t invocation{};
        bool shared_boundary = false;
        if (params.contains("_wvd_verified_invocation")) {
            auto token = params.at("_wvd_verified_invocation").get<std::string>();
            params.erase("_wvd_verified_invocation");
            auto &verified = self.verified_invocation_;
            require(verified && !verified->used && verified->token == token &&
                        verified->binding == name && verified->parameters == params &&
                        verified->roi.x == roi->x && verified->roi.y == roi->y &&
                        verified->roi.width == roi->width && verified->roi.height == roi->height,
                    "INTEGRITY_INVOCATION_INVALID");
            verified->used = true;
            invocation = verified->id;
            shared_boundary = true;
        } else {
            verify_bundle(self.bundle_);
            invocation = ++self.recognition_invocation_;
        }
        const CustomRecognitionScope scope({roi->x, roi->y, roi->width, roi->height}, invocation,
                                            self.business_);
        auto impl = self.recognitions_.find(name);
        require(impl != self.recognitions_.end(), "CUSTOM_RECO_NOT_REGISTERED");
        if (self.gate_) {
            auto frame = self.gate_->frame_identity();
            auto key = frame.device_id + ":" + frame.pack_revision + ":" +
                       std::to_string(frame.generation) + ":" + std::to_string(frame.frame_id) +
                       ":" + std::to_string(frame.action_epoch) + ":" +
                       std::to_string(frame.connection_generation);
            if (key != self.recognition_cache_.frame_key) {
                self.recognition_cache_.frame_key = key;
                self.recognition_cache_.results.clear();
            }
        }
        auto key = std::string(name) + ":" + params.dump() + ":" +
                   nlohmann::json({roi->x, roi->y, roi->width, roi->height}).dump() + ":" +
                   std::to_string(self.business_ ? self.business_->version() : 0);
        auto cached = self.recognition_cache_.results.find(key);
        if (self.gate_ && cached != self.recognition_cache_.results.end())
            result = cached->second;
        else {
            result =
                impl->second(self.bundle_,
                             {{static_cast<const std::uint8_t *>(MaaImageBufferGetRawData(image)),
                               std::size_t(size.width) * size.height * 3},
                              size},
                             params, scope, self.recognition_cache_);
            if (self.gate_ && self.recognition_cache_.results.size() < 512)
                self.recognition_cache_.results.emplace(key, result);
        }
        result["invocation_id"] = scope.invocation_id();
        result["allowed_roi"] = {roi->x, roi->y, roi->width, roi->height};
        result["integrity_boundary"] = shared_boundary ? "shared_direct" : "pipeline";
        require(result.value("schema", 0) == 1 && result.contains("outcome"),
                "CUSTOM_DETAIL_INVALID");
        if (result["outcome"] == "Hit") {
            auto box = result.at("box").get<std::vector<int>>();
            require(box.size() == 4 && box[0] >= 0 && box[1] >= 0 && box[2] > 0 && box[3] > 0 &&
                        box[0] <= size.width - box[2] && box[1] <= size.height - box[3],
                    "CUSTOM_BOX_INVALID");
            require(box[0] >= roi->x && box[1] >= roi->y &&
                        box[0] + box[2] <= roi->x + roi->width &&
                        box[1] + box[3] <= roi->y + roi->height,
                    "CUSTOM_BOX_OUTSIDE_SCOPE");
            *output = {box[0], box[1], box[2], box[3]};
        }
    } catch (const std::exception &error) {
        result = {{"schema", 1}, {"outcome", "Error"}, {"error", error.what()}};
    } catch (...) {
        result = {{"schema", 1}, {"outcome", "Error"}, {"error", "CUSTOM_RECO_EXCEPTION"}};
    }
    try {
        MaaStringBufferSet(detail, result.dump().c_str());
        self.hooks_.event("recognition.custom", result);
        if (result["outcome"] == "Error")
            self.hooks_.failure(result.value("error", std::string("CUSTOM_RECO_ERROR")));
        return result["outcome"] == "Hit";
    } catch (...) {
        return false;
    }
}
std::int64_t MaaGateway::post(const std::string &entry) {
    require(initialized_ && !hooks_.cancelled(), "SESSION_NOT_RUNNING");
    auto id = MaaTaskerPostTask(tasker_.get(), entry.c_str(), "{}");
    require(id != MaaInvalidId, "TASK_POST_FAILED");
    return id;
}
int MaaGateway::status(std::int64_t id) const { return MaaTaskerStatus(tasker_.get(), id); }
bool MaaGateway::running() const { return tasker_ && MaaTaskerRunning(tasker_.get()); }
void MaaGateway::request_stop() {
    if (tasker_ && !stop_posted_) {
        stop_posted_ = true;
        require(MaaTaskerPostStop(tasker_.get()) != MaaInvalidId, "STOP_POST_FAILED");
    }
}
void MaaGateway::close() noexcept {
    auto event = [&](const char *name) noexcept {
        try {
            hooks_.event(name, {});
        } catch (...) {
            try {
                hooks_.failure("CLEANUP_EVENT_FAILED");
            } catch (...) {
            }
        }
    };
    if (tasker_) {
        if (running()) {
            try {
                request_stop();
            } catch (...) {
                try {
                    hooks_.failure("STOP_POST_FAILED");
                } catch (...) {
                }
            }
            while (running())
                std::this_thread::sleep_for(5ms);
        }
        MaaTaskerRemoveSink(tasker_.get(), sink_);
        MaaTaskerRemoveContextSink(tasker_.get(), context_sink_);
        while (activity_.count.load())
            std::this_thread::sleep_for(5ms);
        tasker_.reset();
        event("objects.tasker_destroyed");
    }
    if (controller_) {
        controller_.reset();
        event("objects.controller_destroyed");
    }
    if (resource_) {
        resource_.reset();
        event("objects.resource_destroyed");
    }
    controller_callbacks_.reset();
    recognition_cache_ = {};
    bundle_.lease.reset();
    initialized_ = false;
}
nlohmann::json MaaGateway::bundle_status() const {
    if (!bundle_.lease)
        return {{"active", false}};
    return {{"active", true},
            {"root", utf8(bundle_.root)},
            {"lease", bundle_.lease->identity()},
            {"revision", bundle_.revision},
            {"file_handles", bundle_.lease->file_count()},
            {"directory_handles", bundle_.lease->directory_count()},
            {"directory_checks", bundle_.lease->directory_checks()},
            {"sealing_hash_bytes", bundle_.lease->hash_bytes()},
            {"sealing_hash_count", bundle_.lease->file_count()}};
}
MaaBool MaaGateway::action_callback(MaaContext *native, MaaTaskId task, const char *node,
                                    const char *custom, const char *parameters, MaaRecoId,
                                    const MaaRect *, void *pointer) noexcept {
    auto &self = *static_cast<MaaGateway *>(pointer);
    CallbackScope scope(self.activity_);
    try {
        Context context(self, native, task, node);
        self.hooks_.event("custom.enter",
                          {{"node", node}, {"task_id", task}, {"depth", context.depth()}});
        auto action = self.actions_.find(custom);
        require(action != self.actions_.end(), "CUSTOM_ACTION_NOT_FOUND");
        if (self.hooks_.cancelled())
            return false;
        auto ok = action->second(context, nlohmann::json::parse(parameters));
        // 子调用返回 false 的语义由 run_child 的真实 TaskDetail 判定，不能抢先写泛化错误。
        if (!ok && context.depth() == 0 && !self.hooks_.cancelled())
            self.hooks_.failure("CUSTOM_ACTION_FAILED");
        return ok;
    } catch (const std::exception &error) {
        try {
            self.hooks_.failure(error.what());
        } catch (...) {
        }
        return false;
    } catch (...) {
        try {
            self.hooks_.failure("CUSTOM_ACTION_EXCEPTION");
        } catch (...) {
        }
        return false;
    }
}
void MaaGateway::event_callback(void *, const char *message, const char *payload,
                                void *pointer) noexcept {
    auto &self = *static_cast<MaaGateway *>(pointer);
    CallbackScope scope(self.activity_);
    try {
        self.hooks_.event(message, nlohmann::json::parse(payload));
    } catch (...) {
        try {
            self.hooks_.failure("EVENT_CALLBACK_EXCEPTION");
        } catch (...) {
        }
    }
}
contracts::FrameEnvelope MaaGateway::capture() {
    require(controller_ && gate_ && !hooks_.cancelled(), "CAPTURE_NOT_AVAILABLE");
    auto id = MaaControllerPostScreencap(controller_.get());
    require(id != MaaInvalidId && MaaControllerWait(controller_.get(), id) == MaaStatus_Succeeded,
            "CAPTURE_FAILED");
    auto image = image_buffer();
    require(MaaControllerCachedImage(controller_.get(), image.get()), "CAPTURE_CACHE_FAILED");
    auto bytes = MaaImageBufferGetEncoded(image.get());
    auto size = MaaImageBufferGetEncodedSize(image.get());
    require(bytes && size, "CAPTURE_ENCODE_FAILED");
    return {gate_->frame_identity(), std::vector<std::uint8_t>(bytes, bytes + size)};
}
bool MaaGateway::controller_action(const contracts::Command &c) {
    require(bool(controller_), "CONTROLLER_NOT_AVAILABLE");
    MaaCtrlId id = MaaInvalidId;
    using contracts::ActionKind;
    switch (c.kind) {
    case ActionKind::Click:
        id = MaaControllerPostClick(controller_.get(), c.x, c.y);
        break;
    case ActionKind::Swipe:
        id = MaaControllerPostSwipe(controller_.get(), c.x, c.y, c.x2, c.y2, c.duration);
        break;
    case ActionKind::TouchDown:
        id = MaaControllerPostTouchDown(controller_.get(), c.contact, c.x, c.y, c.pressure);
        break;
    case ActionKind::TouchMove:
        id = MaaControllerPostTouchMove(controller_.get(), c.contact, c.x, c.y, c.pressure);
        break;
    case ActionKind::TouchUp:
        id = MaaControllerPostTouchUp(controller_.get(), c.contact);
        break;
    case ActionKind::ClickKey:
        id = MaaControllerPostClickKey(controller_.get(), c.key);
        break;
    case ActionKind::KeyDown:
        id = MaaControllerPostKeyDown(controller_.get(), c.key);
        break;
    case ActionKind::KeyUp:
        id = MaaControllerPostKeyUp(controller_.get(), c.key);
        break;
    case ActionKind::Text:
        id = MaaControllerPostInputText(controller_.get(), c.text.c_str());
        break;
    case ActionKind::Scroll:
        id = MaaControllerPostScroll(controller_.get(), c.x, c.y);
        break;
    case ActionKind::RelativeMove:
        id = MaaControllerPostRelativeMove(controller_.get(), c.x, c.y);
        break;
    case ActionKind::StartApp:
        id = MaaControllerPostStartApp(controller_.get(), c.text.c_str());
        break;
    case ActionKind::StopApp:
        id = MaaControllerPostStopApp(controller_.get(), c.text.c_str());
        break;
    case ActionKind::Shell:
        id = MaaControllerPostShell(controller_.get(), c.text.c_str(), c.duration);
        break;
    case ActionKind::Inactive:
        id = MaaControllerPostInactive(controller_.get());
        break;
    }
    return id != MaaInvalidId && MaaControllerWait(controller_.get(), id) == MaaStatus_Succeeded;
}
int Context::depth() const { return gateway_.depth_.load(); }
bool Context::cancelled() const { return gateway_.hooks_.cancelled(); }
contracts::FrameEnvelope Context::capture() { return gateway_.capture(); }
contracts::Observation Context::recognize(const contracts::FrameEnvelope &frame,
                                          const RecognitionRequest &request) {
    return gateway_.recognize(frame, gateway_.gate_->frame_identity(), request, context_);
}
ChildResult Context::run_child(const std::string &entry, const nlohmann::json &overrides,
                               bool clone, const std::vector<std::string> &reset_hit_counts) {
    storage::validate_bundle_references(gateway_.bundle_, overrides);
    struct Depth {
        std::atomic<int> &depth;
        Depth(std::atomic<int> &value) : depth(value) { ++depth; }
        ~Depth() { --depth; }
    } depth(gateway_.depth_);
    auto native = clone ? MaaContextClone(context_) : context_;
    require(native != nullptr, "CONTEXT_CLONE_FAILED");
    require(reset_hit_counts.size() <= 4096, "CHILD_RESET_LIMIT");
    // SDK 的上下文共享命中计数。只清理编译器封存的被调用作用域，
    // 不清外层次数、业务状态、观察缓存或输入许可；下一次动作仍需新帧。
    for (const auto &name : reset_hit_counts) {
        require(!name.empty() && name != node_, "CHILD_RESET_SCOPE_INVALID");
        require(MaaContextClearHitCount(native, name.c_str()), "CHILD_RESET_FAILED");
    }
    auto id = MaaContextRunTask(native, entry.c_str(), overrides.dump().c_str());
    auto name = string_buffer();
    MaaSize count{};
    MaaStatus status{};
    bool valid = id != MaaInvalidId && MaaTaskerGetTaskDetail(gateway_.tasker_.get(), id,
                                                              name.get(), nullptr, &count, &status);
    ChildResult result{id, status, valid};
    gateway_.hooks_.child(result);
    return result;
}
bool Context::native_action(const std::string &type, const nlohmann::json &parameters) {
    MaaRect box{};
    auto id =
        MaaContextRunActionDirect(context_, type.c_str(), parameters.dump().c_str(), &box, "{}");
    auto name = string_buffer(), action = string_buffer(), detail = string_buffer();
    MaaBool success{};
    return id != MaaInvalidId &&
           MaaTaskerGetActionDetail(gateway_.tasker_.get(), id, name.get(), action.get(), &box,
                                    &success, detail.get()) &&
           success;
}
bool Context::controller_action(const contracts::Command &command) {
    return gateway_.controller_action(command);
}
nlohmann::json Context::node_data(const std::string &name) const {
    auto value = string_buffer();
    require(MaaContextGetNodeData(context_, name.c_str(), value.get()), "NODE_DATA_UNAVAILABLE");
    return nlohmann::json::parse(MaaStringBufferGet(value.get()));
}
bool Context::with_business_state(
    const std::function<bool(contracts::BusinessRunState &)> &operation) {
    require(gateway_.business_ != nullptr, "BUSINESS_STATE_REQUIRED");
    return gateway_.business_->apply([&](auto &state) { return !cancelled() && operation(state); });
}
} // namespace wvd::maafw
