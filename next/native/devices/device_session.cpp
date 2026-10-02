#include "device_session.hpp"
#include "adb_failure.hpp"
#include "metadata_read_fault.hpp"
#include "platform/execution_timing.hpp"
#include "android_viewport.hpp"
#include "android_context_query.hpp"
#include "platform/windows/metadata_query.hpp"
#include "platform/windows/mumu_binding.hpp"
#include "platform/windows/path_utf8.hpp"
#include "platform/windows/process.hpp"
#include <algorithm>
#include <regex>
#include <set>
#include <thread>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

namespace wvd::devices {
namespace {
using namespace std::chrono_literals;
void require(bool condition, const char *code) {
    if (!condition) throw std::runtime_error(code);
}
std::string bytes(const std::vector<std::uint8_t> &value) {
    return {value.begin(), value.end()};
}
} // namespace

DeviceSession::DeviceSession(nlohmann::json binding, std::filesystem::path capture_host,
                             std::filesystem::path scrcpy_server,
                             std::stop_token cancellation)
    : binding_(platform::verify_mumu_binding(binding, cancellation)),
      helper_path_(std::move(capture_host)), server_path_(std::move(scrcpy_server)),
      adb_(platform::path_from_utf8(binding_.at("adb")), binding_.at("serial")),
      verified_(true) {
    require(std::filesystem::is_regular_file(helper_path_) &&
            std::filesystem::is_regular_file(server_path_), "DEVICE_RUNTIME_FILES_MISSING");
}

DeviceSession::~DeviceSession() noexcept {
    try { disconnect(); }
    catch (...) { OutputDebugStringW(L"WVD DeviceSession destroyed with cleanup unconfirmed\n"); }
}
bool DeviceSession::input_channel_ready() const {
    // 仅由会话工作线程在门禁的 dispatch_mutex 下读取；控制面不借此操作设备。
    return connected_ && control_ && control_->connected() && !control_->unresolved();
}
void DeviceSession::observation_window(std::chrono::steady_clock::time_point deadline, std::stop_token stop) {
    read_deadline_ = deadline;
    read_stop_ = stop;
}
std::chrono::milliseconds DeviceSession::read_budget(std::chrono::milliseconds ceiling) const {
    if (read_stop_.stop_requested()) throw std::runtime_error("CAPTURE_CANCELLED");
    if (read_deadline_ == std::chrono::steady_clock::time_point{}) return ceiling;
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(read_deadline_ - std::chrono::steady_clock::now());
    if (remaining <= 0ms) throw contracts::ObservationUnavailable({contracts::ReadFaultKind::Timeout,
        contracts::ReadFaultStage::Capture, "OBSERVATION_WINDOW_EXHAUSTED", "read.window", {}, {}});
    return std::min(remaining, ceiling);
}
void DeviceSession::record_adb_failure(const AdbFailureInfo &info,
                                      const char *operation) noexcept {
    try {
        record({{"event", "adb.command_failed"}, {"operation", operation},
            {"command", info.command}, {"serial", info.serial}, {"code", info.code},
            {"timeout_ms", info.timeout.count()}, {"elapsed_ms", info.elapsed.count()},
            {"exit_code", info.exit_code}, {"retryable_transport", info.retryable_transport},
            {"stdout_excerpt", info.stdout_excerpt}, {"stderr_excerpt", info.stderr_excerpt}});
    } catch (...) { /* 诊断分配失败不能改变原始读取故障的类型。 */ }
}
void DeviceSession::prepare_input_channel(std::stop_token stop) {
    require(!stop.stop_requested(), "INPUT_PREPARATION_CANCELLED");
    require(connected_ && control_, "DEVICE_INPUT_NOT_READY");
    require(!control_->unresolved(), "DEVICE_INPUT_RESULT_UNCONFIRMED");
    if (!control_->connected()) control_->connect(15000ms, stop);
    require(!stop.stop_requested(), "INPUT_PREPARATION_CANCELLED");
}
void DeviceSession::record(nlohmann::json item) {
    std::lock_guard lock(mutex_);
    if (diagnostics_.size() >= 256) diagnostics_.erase(diagnostics_.begin());
    diagnostics_.push_back(std::move(item));
}
nlohmann::json DeviceSession::diagnostics() const {
    std::lock_guard lock(mutex_);
    return { {"connections", diagnostics_}, {"fast_capture_disabled", fast_failed_.load()},
             {"connection_generation", generation_.load()} };
}
void DeviceSession::set_vpn_required(bool value) {
    std::lock_guard lock(mutex_);
    binding_["vpn_required"] = value;
}
bool DeviceSession::matches_selection(const std::filesystem::path &manager, int index,
                                      const std::string &serial) const {
    if (binding_.at("serial") != serial || binding_.at("index") != index) return false;
    std::error_code error;
    const auto actual = std::filesystem::canonical(
        platform::path_from_utf8(binding_.at("manager")), error);
    const auto selected = std::filesystem::canonical(manager, error);
    return !error && actual == selected;
}
LifecycleTarget DeviceSession::lifecycle_target() const {
    std::lock_guard lock(mutex_);
    return {binding_.at("serial"), std::to_string(binding_.at("index").get<int>()),
            binding_.value("application_id", "jp.co.drecom.wizardry.daphne"),
            binding_.value("vpn_application_id", "com.github.metacubex.clash.meta"),
            binding_.value("vpn_required", false)};
}

bool DeviceSession::connect() {
    require(!capture_host_ || !capture_host_->cleanup_pending(), "DEVICE_CLEANUP_PENDING");
    require(!control_ || !control_->unresolved(), "DEVICE_INPUT_RESULT_UNCONFIRMED");
    if (connected_ && adb_.connected(read_stop_, read_budget(5000ms))) return true;
    if (!adb_.connect(read_stop_, read_budget(20000ms))) return false;
    connected_ = true;
    ++generation_;
    fast_failed_ = false;
    metadata_at_ = {};
    const auto launcher = platform::path_from_utf8(binding_.at("launcher"));
    const auto library = launcher.parent_path() / "sdk" / "external_renderer_ipc.dll";
    if (std::filesystem::is_regular_file(library)) {
        capture_host_ = std::make_unique<MumuCaptureClient>(helper_path_,
            platform::path_from_utf8(binding_.at("install_root")), library,
            binding_.at("index").get<int>(), L"jp.co.drecom.wizardry.daphne");
    } else {
        fast_failed_ = true;
        record({{"event", "capture.fallback"}, {"reason", "MUMU_IPC_DLL_MISSING"}});
    }
    control_ = std::make_unique<ScrcpyControlClient>(adb_, server_path_);
    record({{"event", "connect"}, {"serial", adb_.serial()},
            {"generation", generation_.load()}, {"fast_capture_available", !fast_failed_}});
    return true;
}

void DeviceSession::disconnect() {
    // 清理失败时保留对象；Application 也必须保留 backend 和租约，不能先 move 掉。
    require(release_owned_inputs(), "DEVICE_CLEANUP_PENDING");
    control_.reset(); capture_host_.reset();
    if (connected_) ++generation_;
    connected_ = false;
    metadata_at_ = {};
}

bool DeviceSession::release_owned_inputs() {
    bool control_released = !control_ || control_->close();
    const bool capture_released = !capture_host_ || capture_host_->close();
    if (!control_released) {
        try {
            const auto live = instance_metadata();
            if (!live.at("is_process_started").get<bool>()) {
                control_released = control_->retire_exited_instance();
                record({{"event", "device.instance_exited"}, {"index", binding_.at("index")},
                    {"input_channel_retired", control_released}});
            }
        } catch (...) { /* 无有效实例证据时继续保留清理未确认，绝不把查询失败当退出。 */ }
    }
    record({{"event", "device.cleanup"}, {"control_released", control_released},
            {"capture_released", capture_released},
            {"control_status", control_ ? control_->cleanup_status() : "absent"},
            {"capture_pending", capture_host_ && capture_host_->cleanup_pending()}});
    return control_released && capture_released;
}

android::ShellReply DeviceSession::query(const std::string &command, int timeout,
                                          std::stop_token stop) {
    try { return adb_.shell_fixed(command, read_budget(std::chrono::milliseconds{timeout}),
        stop.stop_possible() ? stop : read_stop_); }
    catch (const AdbCommandFailure &error) {
        record_adb_failure(error.info(), "device.query");
        throw;
    }
}
std::string DeviceSession::foreground(std::stop_token stop) {
    const auto answer = query("dumpsys window", 5000, stop);
    require(answer.exit_code == 0, "FOREGROUND_QUERY_FAILED");
    return android::focus(answer.output);
}

RawFrame DeviceSession::capture_impl(bool preview, std::stop_token stop) {
    namespace timing = platform::timing;
    timing::Scope capture_time(timing::Part::Capture);
    if (stop.stop_requested()) throw std::runtime_error("CAPTURE_CANCELLED");
    require(connected_, "DEVICE_NOT_CONNECTED");
    const auto started = std::chrono::steady_clock::now();
    failed_pixels_.reset();
    cv::Mat bgr;
    std::string backend;
    if (!fast_failed_ && capture_host_) {
        try {
            const auto raw = capture_host_->capture(read_budget(3000ms), stop);
            timing::Scope convert_time(timing::Part::Convert);
            const cv::Mat rgba(raw.height, raw.width, CV_8UC4,
                               const_cast<std::uint8_t *>(raw.rgba_bottom_up.data()));
            cv::cvtColor(rgba, bgr, cv::COLOR_RGBA2BGR);
            cv::flip(bgr, bgr, 0);
            backend = "MUMU_IPC";
        } catch (const contracts::ObservationUnavailable &) {
            throw; // 读取总窗口耗尽不是IPC故障，不修改后端选择。
        } catch (const MumuCaptureNotReady &error) {
            // 游戏显示尚未创建时本次用 ADB 取帧；保持 IPC helper 等待下一帧。
            record({{"event", "capture.not_ready"}, {"reason", error.what()}});
        } catch (const std::exception &error) {
            if (stop.stop_requested() || capture_host_->cleanup_pending()) throw;
            fast_failed_ = true;
            record({{"event", "capture.fallback"}, {"reason", error.what()}});
        }
    }
    if (bgr.empty()) {
        auto png = adb_.screenshot_png(read_budget(8000ms), stop);
        timing::Scope convert_time(timing::Part::Convert);
        bgr = cv::imdecode(png, cv::IMREAD_COLOR);
        require(!bgr.empty(), "ADB_CAPTURE_DECODE_FAILED");
        backend = "ADB_PNG";
    }
    require(bgr.cols > 0 && bgr.rows > 0 && bgr.isContinuous(),
            "CAPTURE_PIXELS_INVALID");
    const auto finished = std::chrono::steady_clock::now();
    capture_time.finish();
    const contracts::Size size{bgr.cols, bgr.rows};
    auto payload = std::make_shared<std::vector<std::uint8_t>>(
        bgr.data, bgr.data + bgr.total() * bgr.elemSize());
    // 元数据失败仍保留本次像素，正常返回立即清除。没有伪造FrameIdentity。
    failed_pixels_ = contracts::DiagnosticPixels{size, payload, finished, adb_.serial(), backend};
    if (metadata_at_ == std::chrono::steady_clock::time_point{} ||
        finished - metadata_at_ > 1000ms || latest_size_ != size) {
        timing::Scope metadata_time(timing::Part::Metadata);
        metadata_at_ = {}; // 查询失败不得留下“刚刷新过”的半套元数据。
        const auto focused = foreground(stop);
        const auto input = query("dumpsys input", 5000, stop);
        require(input.exit_code == 0, "VIEWPORT_QUERY_FAILED");
        const auto viewport = android::input_viewport(input.output);
        if (stop.stop_requested()) throw std::runtime_error("CAPTURE_CANCELLED");
        if (focused.empty() || !viewport || viewport->size != size)
            throw contracts::ObservationUnavailable({contracts::ReadFaultKind::MetadataUnavailable,
                contracts::ReadFaultStage::Capture, "CAPTURE_METADATA_NOT_READY",
                "capture.metadata", 5000ms, {}});
        // 全套读取成功才发布；缺元数据的像素不能变成可点击观察。
        latest_foreground_ = focused;
        latest_rotation_ = viewport->rotation;
        latest_size_ = size;
        metadata_at_ = std::chrono::steady_clock::now();
    }
    timing::Scope payload_time(timing::Part::Convert);
    RawFrame frame;
    frame.raw_bgr = std::move(payload);
    if (preview) {
        if (!cv::imencode(".png", bgr, frame.encoded))
            throw std::runtime_error("CAPTURE_PREVIEW_ENCODE_FAILED");
    }
    frame.size = size;
    frame.device_id = adb_.serial();
    frame.viewport_id = std::to_string(size.width) + "x" + std::to_string(size.height);
    frame.foreground_application = latest_foreground_;
    frame.captured_at = started;
    frame.capture_finished_at = finished;
    frame.backend = backend;
    frame.connection_generation = generation_;
    frame.display_rotation = latest_rotation_;
    failed_pixels_.reset();
    return frame;
}
RawFrame DeviceSession::capture() { return capture({}); }
RawFrame DeviceSession::capture(std::stop_token stop) {
    try { return capture_impl(false, stop); }
    catch (const AdbCommandFailure &error) {
        metadata_at_ = {};
        record_adb_failure(error.info(), "capture");
        if (!stop.stop_requested() && error.info().retryable_transport)
            throw error.as_read_fault(contracts::ReadFaultStage::Capture, "capture:" + error.info().command);
        throw;
    }
}
RawFrame DeviceSession::capture_preview() { return capture_impl(true); }

bool DeviceSession::context_matches(const contracts::FrameIdentity &identity,
                                     const std::string &application) {
    return context_matches(identity, application, {});
}
bool DeviceSession::context_matches(const contracts::FrameIdentity &identity,
                                     const std::string &application, std::stop_token stop) {
    platform::timing::Scope measure(platform::timing::Part::InputValidation);
    if (stop.stop_requested() || !connected_ || !identity.frame_id || identity.connection_generation != generation_ ||
        identity.device_id != adb_.serial() || identity.display_rotation < 0 ||
        identity.foreground_application != application || application.empty()) {
        metadata_at_ = {};
        record({{"event", "input.context_changed"}, {"reason", "FRAME_IDENTITY_CHANGED"},
            {"frame_application", identity.foreground_application}, {"expected_application", application},
            {"frame_generation", identity.connection_generation}, {"current_generation", generation_.load()}});
        return false;
    }
    // 成功的带返回码事务本身证明 transport 可达；不再单独启动 adb get-state。
    // 保留前台 -> viewport -> 前台的原安全顺序，不把它冒充设备端原子快照。
    platform::timing::count(platform::timing::Counter::ContextTransactions);
    android::ShellReply answer;
    try {
        answer = adb_.shell_fixed(std::string(android::context_probe_command),
            read_budget(20000ms), stop, 8ULL * 1024 * 1024);
    } catch (const AdbCommandFailure &error) {
        metadata_at_ = {};
        record_adb_failure(error.info(), "input.context");
        if (!stop.stop_requested() && error.info().retryable_transport)
            throw error.as_read_fault(contracts::ReadFaultStage::InputContext, "input.context");
        throw;
    }
    if (stop.stop_requested() || answer.exit_code != 0) {
        metadata_at_ = {};
        record({{"event", "input.context_changed"}, {"reason", "CONTEXT_QUERY_FAILED"}, {"exit_code", answer.exit_code}});
        return false;
    }
    const auto sections = android::parse_context_dump(answer.output);
    if (!sections) {
        metadata_at_ = {};
        record({{"event", "input.context_changed"}, {"reason", "CONTEXT_DUMP_INVALID"}});
        return false;
    }
    const auto before = android::focus(std::string(sections->before_focus));
    const auto viewport = android::input_viewport(std::string(sections->input));
    const auto after = android::focus(std::string(sections->after_focus));
    const bool valid = before == application && after == application && viewport &&
        viewport->size == identity.raw_size && viewport->rotation == identity.display_rotation &&
        identity.viewport_id == std::to_string(viewport->size.width) + "x" +
                                std::to_string(viewport->size.height) &&
        !stop.stop_requested() && connected_ && generation_ == identity.connection_generation;
    if (valid) {
        latest_foreground_ = after;
        latest_size_ = viewport->size;
        latest_rotation_ = viewport->rotation;
        metadata_at_ = std::chrono::steady_clock::now();
    } else {
        metadata_at_ = {};
        record({{"event", "input.context_changed"}, {"before", before}, {"after", after},
            {"expected_application", application}, {"expected_rotation", identity.display_rotation},
            {"expected_size", {identity.raw_size.width, identity.raw_size.height}},
            {"actual_rotation", viewport ? viewport->rotation : -1},
            {"actual_size", viewport ? nlohmann::json{viewport->size.width, viewport->size.height} : nlohmann::json(nullptr)}});
    }
    return valid;
}

bool DeviceSession::execute(const contracts::Command &command) {
    return execute(command, {});
}
bool DeviceSession::execute(const contracts::Command &command, std::stop_token stop) {
    platform::timing::Scope measure(platform::timing::Part::InputDelivery);
    if (stop.stop_requested()) return false;
    require(connected_ && latest_size_.width > 0 && latest_size_.height > 0,
            "DEVICE_INPUT_NOT_READY");
    require(control_ != nullptr, "SCRCPY_CONTROL_MISSING");
    // 输入前已准备控制通道并重新取帧；此处不可握手后提交旧坐标。
    require(control_->connected(), "INPUT_CHANNEL_CHANGED_REOBSERVE_REQUIRED");
    if (stop.stop_requested()) return false;
    control_->submit(command, latest_size_.width, latest_size_.height, stop);
    return true; // TransportSubmitted, never a game-level confirmation.
}

bool DeviceSession::vpn_connected() {
    if (android::tun_up(query("ip addr show"))) return true;
    return android::live_vpn(query("dumpsys connectivity", 8000));
}
std::string DeviceSession::clash_package() {
    const auto packages = query("pm list packages", 8000);
    require(packages.exit_code == 0, "VPN_PACKAGE_QUERY_FAILED");
    for (const auto *candidate : {"com.github.metacubex.clash.meta", "com.github.kr328.clash",
                                  "com.github.kr328.clash.foss"})
        if (android::installed(packages, candidate)) return candidate;
    std::set<std::string> found;
    const std::regex expression(R"((?:^|\n)package:([^\r\n]*clash[^\r\n]*)(?:\r?\n|$))",
                                std::regex::icase);
    for (std::sregex_iterator it(packages.output.begin(), packages.output.end(), expression), end;
         it != end; ++it) {
        const auto name = android::trim((*it)[1].str());
        if (android::package_name(name)) found.insert(name);
    }
    require(found.size() <= 1, "VPN_APPLICATION_AMBIGUOUS");
    return found.empty() ? std::string{} : *found.begin();
}
bool DeviceSession::start_package(const std::string &package) {
    require(android::package_name(package), "ANDROID_PACKAGE_INVALID");
    const auto activity = query("cmd package resolve-activity --brief " + package);
    std::smatch match;
    if (activity.exit_code == 0 &&
        std::regex_search(activity.output, match,
                          std::regex(R"(([A-Za-z0-9_.]+/[A-Za-z0-9_.$]+))")) &&
        match[1].str().starts_with(package + "/")) {
        const auto result = query("am start -n " + match[1].str());
        return result.exit_code == 0 && result.output.find("Error") == std::string::npos &&
            result.output.find("Exception") == std::string::npos;
    }
    if (activity.exit_code != 0 &&
        activity.output.find("No activity") == std::string::npos)
        throw std::runtime_error("ANDROID_ACTIVITY_QUERY_FAILED");
    const auto result = query("monkey -p " + package +
        " -c android.intent.category.LAUNCHER 1", 10000);
    return result.exit_code == 0 &&
        result.output.find("No activities found") == std::string::npos &&
        result.output.find("Error") == std::string::npos &&
        result.output.find("Exception") == std::string::npos;
}

nlohmann::json DeviceSession::instance_metadata() {
    platform::MetadataQuery query_manager;
    std::stop_callback cancel(read_stop_, [&] { query_manager.cancel(); });
    const auto budget = read_budget(3s);
    const auto metadata = query_manager.run(platform::path_from_utf8(binding_.at("manager")),
                                            binding_.at("index").get<int>(), budget);
    require_metadata_read(metadata, budget);
    const auto &live = metadata.at("data");
    require(platform::mumu_metadata_usable(live) &&
        live.at("index").get<std::string>() == std::to_string(binding_.at("index").get<int>()) &&
        live.at("created_timestamp") == binding_.at("created_timestamp") &&
        live.at("is_process_started").is_boolean(), "MUMU_INSTANCE_MISMATCH");
    if (live.value("is_android_started", false))
        require("127.0.0.1:" + std::to_string(live.at("adb_port").get<int>()) ==
            binding_.at("serial").get<std::string>(), "MUMU_ADB_BINDING_MISMATCH");
    return live;
}
contracts::ObservationRecovery DeviceSession::recover_observation(bool restart_application) try {
    // 只由当前Session工作线程调用。每次只推进一次连接/启动检查，退避归执行器。
    const auto live = instance_metadata(); // 包括创建标识、端口、实例核对，失败不得换设备。
    const auto pending = [&]() -> void {
        throw contracts::ObservationUnavailable({contracts::ReadFaultKind::TransportUnavailable,
            contracts::ReadFaultStage::Capture,
            recovery_launched_ ? "DEVICE_INSTANCE_STARTING" :
                recovery_instance_detected_ ? "DEVICE_INSTANCE_RESTART_REQUIRED" : "DEVICE_RECONNECT_WAIT",
            "bound_device.reconnect", {}, {}});
    };
    const auto cancelled = [&] { return read_stop_.stop_requested() ||
        std::chrono::steady_clock::now() >= read_deadline_; };
    if (cancelled()) throw std::runtime_error("CAPTURE_CANCELLED");
    const auto target = lifecycle_target();
    if (!live.at("is_process_started").get<bool>()) {
        if (recovery_launched_) pending();
        if (!recovery_origin_) recovery_origin_ = generation_.load();
        if (!recovery_instance_detected_) {
            recovery_instance_detected_ = true;
            pending(); // 先让执行器扩展已证实实例退出的读取窗口，再启动模拟器。
        }
        LifecyclePlan plan{target, {LifecycleOperation::RestartInstance}, 1};
        if (!execute_lifecycle(plan.operations.front(), target, cancelled)) pending();
        recovery_launched_ = true;
        pending();
    }
    if (!live.value("is_android_started", false)) pending();
    const bool online = adb_.connected(read_stop_, read_budget(5000ms));
    if (!online || !connected_) {
        if (!recovery_origin_) recovery_origin_ = generation_.load();
        // 先恢复ADB可达性再释放旧控制资源。未释放不建立新输入通道。
        if (!adb_.connect(read_stop_, read_budget(20000ms))) pending();
        LifecyclePlan plan{target, {LifecycleOperation::Reconnect}, 1};
        if (!execute_lifecycle(plan.operations.front(), target, cancelled)) pending();
    }
    // 仅执行器确认连续异常到期才关闭存活游戏；后续重试不再次停止已拉起的应用。
    if (restart_application && !recovery_application_started_) {
        record({{"event", "observation.application_restart_requested"},
            {"reason", "CONTINUOUS_EXCEPTION_TIMEOUT"}, {"device", target.device_id},
            {"instance", target.instance_id}, {"application", target.application_id}});
        if (!execute_lifecycle(LifecycleOperation::StopApplication, target, cancelled)) pending();
        recovery_application_started_ = true;
        metadata_at_ = {};
    }
    // 读取恢复也检查应用，而非仅检查ADB。前台丢失时按pid区分切回和冷启动；
    // 只恢复绑定实例，不force-stop存活游戏，不关其它模拟器，不重放游戏点击。
    const bool running = android::process(query("pidof " + target.application_id)) ==
                         android::Presence::Present;
    const bool focused = foreground(read_stop_) == target.application_id;
    bool restored = false;
    if (recovery_launched_ || recovery_application_started_ || !focused) {
        if (target.vpn_required &&
            !execute_lifecycle(LifecycleOperation::EnsureVpn, target, cancelled)) pending();
        if (!running) recovery_application_started_ = true;
        if (!execute_lifecycle(LifecycleOperation::StartApplication, target, cancelled)) pending();
        metadata_at_ = {}; // 丢弃切回前的桌面视口/焦点缓存。
        if (foreground(read_stop_) != target.application_id) pending();
        restored = true;
    }
    (void)instance_metadata();
    contracts::ObservationRecovery result;
    result.application_restarted = recovery_launched_ || recovery_application_started_;
    result.foreground_restored = restored;
    if (recovery_origin_) {
        result.reconnect = contracts::ObservationReconnect{target.device_id, target.instance_id,
            binding_.at("created_timestamp").dump(), *recovery_origin_, generation_};
        require(result.reconnect->after > result.reconnect->before, "RECONNECT_GENERATION_NOT_ADVANCED");
    }
    record({{"event", "observation.context_restored"}, {"device", target.device_id},
        {"instance", target.instance_id}, {"application_was_running", running},
        {"application_restarted", result.application_restarted}, {"foreground_restored", restored},
        {"reconnected", result.reconnect.has_value()},
        {"before", recovery_origin_.value_or(generation_)}, {"after", generation_.load()}});
    recovery_origin_.reset(); recovery_instance_detected_ = false;
    recovery_launched_ = recovery_application_started_ = false;
    return result;
} catch (const AdbCommandFailure &error) {
    record_adb_failure(error.info(), "observation.reconnect");
    if (!read_stop_.stop_requested() && error.info().retryable_transport)
        throw error.as_read_fault(contracts::ReadFaultStage::Capture, "bound_device.reconnect");
    throw;
}
bool DeviceSession::settle_observed_input() {
    if (!control_ || !control_->unresolved()) return true;
    // 业务后置条件已由新帧证实，但必须先确认旧通道静止，才允许下一次输入。
    if (!control_->close()) return false;
    control_ = std::make_unique<ScrcpyControlClient>(adb_, server_path_);
    return true;
}
std::optional<LifecycleObservation> DeviceSession::observe_lifecycle() {
    const auto target = lifecycle_target();
    const auto live = instance_metadata();
    const bool instance = live.value("is_process_started", false) &&
                          live.value("is_android_started", false);
    const bool online = instance && connected_ && adb_.connected();
    bool running{}, focused{}, vpn = !target.vpn_required;
    if (online) {
        running = android::process(query("pidof " + target.application_id)) ==
                  android::Presence::Present;
        focused = foreground() == target.application_id;
        if (target.vpn_required) vpn = vpn_connected();
    }
    return LifecycleObservation{target, instance, online, running, vpn, generation_,
                                std::chrono::steady_clock::now(), focused,
                                !live.at("is_process_started").get<bool>()};
}

bool DeviceSession::vpn_ui_step(const std::string &package, bool &start_clicked,
                                 const std::function<bool()> &cancelled) {
    if (cancelled() || vpn_connected()) return false;
    // 先准备控制通道，再采当前授权界面，不能拿握手前的坐标迟到点击。
    if (!control_) throw std::runtime_error("SCRCPY_CONTROL_MISSING");
    if (!control_->connected()) control_->connect(15000ms, {}, cancelled);
    if (cancelled()) return false;
    const auto live_frame = capture();
    if (cancelled()) return false;
    const auto focus = foreground();
    if (live_frame.foreground_application != focus) return false;
    const bool permission = focus == "com.android.vpndialogs";
    if (!permission && (focus != package || start_clicked)) return false;
    const auto xml = query("uiautomator dump /sdcard/wvd-vpn.xml >/dev/null && "
                           "cat /sdcard/wvd-vpn.xml && rm /sdcard/wvd-vpn.xml", 5000);
    const auto labels = permission
        ? std::vector<std::string>{"OK", "Allow", "允许", "确定", "允許", "確定"}
        : std::vector<std::string>{"Tap to start", "点此启动", "點此啟動", "点击启动", "點擊啟動"};
    std::optional<android::UiTarget> target;
    if (xml.exit_code == 0 && xml.output.find("<hierarchy") != std::string::npos)
        target = android::unique_ui_target(xml.output, focus, labels, permission);
    else {
        require(!permission && !start_clicked, "VPN_UI_DUMP_FAILED");
        const auto frame = capture();
        require(frame.foreground_application == package, "VPN_UI_CONTEXT_CHANGED");
        target = android::clash_main_start_target(query("dumpsys activity activities"),
            package, frame.size.width, frame.size.height);
        require(target.has_value(), "VPN_UI_FALLBACK_UNCONFIRMED");
    }
    if (!target || cancelled() || vpn_connected() || foreground() != focus) return false;
    if (cancelled()) return false;
    control_->submit(contracts::Command{.kind = contracts::ActionKind::Click,
        .x = target->x, .y = target->y}, live_frame.size.width, live_frame.size.height,
        {}, cancelled);
    if (!permission) start_clicked = true;
    return true;
}

bool DeviceSession::execute_lifecycle(LifecycleOperation operation, const LifecycleTarget &target,
                                       const std::function<bool()> &cancelled) {
    const auto selected = lifecycle_target();
    if (target.device_id != selected.device_id || target.instance_id != selected.instance_id ||
        target.application_id != selected.application_id ||
        target.vpn_application_id != selected.vpn_application_id || cancelled()) return false;
    if (operation == LifecycleOperation::Reconnect) {
        disconnect();
        if (read_deadline_ != std::chrono::steady_clock::time_point{})
            return !cancelled() && connect();
        const auto deadline = std::chrono::steady_clock::now() + 120s;
        while (!cancelled() && std::chrono::steady_clock::now() < deadline) {
            try { if (connect()) return true; }
            catch (const std::exception &error) {
                record({{"event", "recovery.adb_wait"}, {"reason", error.what()}});
            }
            std::this_thread::sleep_for(500ms);
        }
        return false;
    }
    if (operation == LifecycleOperation::RestartInstance) {
        // 只恢复经管理器再次确认已退出的绑定实例，不把离线/黑帧升级为杀模拟器。
        require(!instance_metadata().at("is_process_started").get<bool>(),
            "MUMU_INSTANCE_EXIT_NOT_CONFIRMED");
        disconnect();
        if (cancelled()) return false;
        platform::launch_selected_instance(platform::path_from_utf8(binding_.at("launcher")),
            binding_.at("index").get<int>());
        if (read_deadline_ != std::chrono::steady_clock::time_point{}) return true;
        const auto deadline = std::chrono::steady_clock::now() + 120s;
        while (!cancelled() && std::chrono::steady_clock::now() < deadline) {
            const auto live = instance_metadata();
            if (live.value("is_android_started", false)) {
                try { if (connect()) return true; }
                catch (const std::exception &error) {
                    record({{"event", "recovery.adb_wait"}, {"reason", error.what()}});
                }
            }
            std::this_thread::sleep_for(500ms);
        }
        return false;
    }
    if (!connected_) return false;
    if (operation == LifecycleOperation::StopApplication) {
        return query("am force-stop " + target.application_id).exit_code == 0;
    }
    if (operation == LifecycleOperation::StartApplication)
        return start_package(target.application_id);
    if (operation == LifecycleOperation::EnsureVpn) {
        if (vpn_connected()) return true;
        const auto package = clash_package();
        require(!package.empty() && package == target.vpn_application_id,
                "VPN_APPLICATION_MISMATCH");
        const auto action = package + ".action.START_CLASH";
        auto started = query("am start -n " + package +
            "/com.github.kr328.clash.ExternalControlActivity -a " + action);
        if (started.exit_code != 0 || started.output.find("Error") != std::string::npos ||
            started.output.find("Exception") != std::string::npos) {
            started = query("am start -a " + action + " -p " + package);
            require(started.exit_code == 0 && started.output.find("Error") == std::string::npos &&
                    started.output.find("Exception") == std::string::npos,
                    "VPN_START_INTENT_FAILED");
        }
        bool main_open{}, clicked{};
        // 冷启动的系统授权/Clash界面与tun建立是异步的。沿用生命周期步骤期限，
        // 不把一次慢UI读取跨过10秒当成配置缺失；停止/读图恢复仍受外层期限约束。
        const auto deadline = std::chrono::steady_clock::now() + 120s;
        while (!cancelled() && std::chrono::steady_clock::now() < deadline) {
            if (vpn_connected()) return true;
            if (!main_open && foreground() != "com.android.vpndialogs") {
                require(start_package(package), "VPN_MAIN_ACTIVITY_FAILED");
                main_open = true;
            }
            vpn_ui_step(package, clicked, cancelled);
            std::this_thread::sleep_for(200ms);
        }
        if (cancelled()) return false;
        if (vpn_connected()) return true;
        throw std::runtime_error("VPN_PERMISSION_OR_PROFILE_REQUIRED");
    }
    return false;
}
} // namespace wvd::devices
