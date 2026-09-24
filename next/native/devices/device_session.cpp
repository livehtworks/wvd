#include "device_session.hpp"
#include "android_viewport.hpp"
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

void DeviceSession::record(nlohmann::json item) {
    std::lock_guard lock(mutex_);
    if (diagnostics_.size() >= 256) diagnostics_.erase(diagnostics_.begin());
    diagnostics_.push_back(std::move(item));
}
nlohmann::json DeviceSession::diagnostics() const {
    std::lock_guard lock(mutex_);
    return { {"connections", diagnostics_}, {"fast_capture_disabled", fast_failed_},
             {"connection_generation", generation_} };
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
    if (connected_ && adb_.connected()) return true;
    if (!adb_.connect()) return false;
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
            {"generation", generation_}, {"fast_capture_available", !fast_failed_}});
    return true;
}

void DeviceSession::disconnect() {
    if (control_) control_->close();
    if (capture_host_) capture_host_->close();
    control_.reset(); capture_host_.reset();
    if (connected_) ++generation_;
    connected_ = false;
    metadata_at_ = {};
}

bool DeviceSession::release_owned_inputs() {
    if (!control_) return true;
    control_->close();
    return !control_->unresolved();
}

android::ShellReply DeviceSession::query(const std::string &command, int timeout,
                                          std::stop_token stop) {
    return adb_.shell_fixed(command, std::chrono::milliseconds{timeout}, stop);
}
std::string DeviceSession::foreground() {
    const auto answer = query("dumpsys window");
    require(answer.exit_code == 0, "FOREGROUND_QUERY_FAILED");
    return android::focus(answer.output);
}

RawFrame DeviceSession::capture_impl(bool preview, std::stop_token stop) {
    if (stop.stop_requested()) throw std::runtime_error("CAPTURE_CANCELLED");
    require(connected_, "DEVICE_NOT_CONNECTED");
    const auto started = std::chrono::steady_clock::now();
    cv::Mat bgr;
    std::string backend;
    if (!fast_failed_ && capture_host_) {
        try {
            const auto raw = capture_host_->capture(3000ms, stop);
            const cv::Mat rgba(raw.height, raw.width, CV_8UC4,
                               const_cast<std::uint8_t *>(raw.rgba_bottom_up.data()));
            cv::cvtColor(rgba, bgr, cv::COLOR_RGBA2BGR);
            cv::flip(bgr, bgr, 0);
            backend = "MUMU_IPC";
        } catch (const std::exception &error) {
            if (stop.stop_requested()) throw;
            fast_failed_ = true;
            record({{"event", "capture.fallback"}, {"reason", error.what()}});
        }
    }
    if (bgr.empty()) {
        auto png = adb_.screenshot_png(8000ms, stop);
        bgr = cv::imdecode(png, cv::IMREAD_COLOR);
        require(!bgr.empty(), "ADB_CAPTURE_DECODE_FAILED");
        backend = "ADB_PNG";
    }
    require(bgr.cols > 0 && bgr.rows > 0 && bgr.isContinuous(),
            "CAPTURE_PIXELS_INVALID");
    const auto finished = std::chrono::steady_clock::now();
    const contracts::Size size{bgr.cols, bgr.rows};
    if (metadata_at_ == std::chrono::steady_clock::time_point{} ||
        finished - metadata_at_ > 1000ms || latest_size_ != size) {
        latest_foreground_ = foreground();
        latest_rotation_ = -1;
        try {
            const auto input = query("dumpsys input");
            const auto viewport = input.exit_code == 0
                ? android::input_viewport(input.output) : std::nullopt;
            if (viewport && viewport->size == size) latest_rotation_ = viewport->rotation;
        } catch (...) { latest_rotation_ = -1; }
        latest_size_ = size;
        metadata_at_ = finished;
    }
    auto payload = std::make_shared<std::vector<std::uint8_t>>(
        bgr.data, bgr.data + bgr.total() * bgr.elemSize());
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
    return frame;
}
RawFrame DeviceSession::capture() { return capture_impl(false); }
RawFrame DeviceSession::capture(std::stop_token stop) { return capture_impl(false, stop); }
RawFrame DeviceSession::capture_preview() { return capture_impl(true); }

bool DeviceSession::context_matches(const contracts::FrameIdentity &identity,
                                     const std::string &application) {
    if (!connected_ || !identity.frame_id || identity.connection_generation != generation_ ||
        identity.device_id != adb_.serial() || identity.display_rotation < 0 ||
        identity.foreground_application != application || application.empty() ||
        !adb_.connected() || foreground() != application)
        return false;
    const auto input = query("dumpsys input");
    const auto viewport = input.exit_code == 0
        ? android::input_viewport(input.output) : std::nullopt;
    return viewport && viewport->size == identity.raw_size &&
        viewport->rotation == identity.display_rotation &&
        identity.viewport_id == std::to_string(viewport->size.width) + "x" +
                                std::to_string(viewport->size.height) &&
        foreground() == application && generation_ == identity.connection_generation;
}

bool DeviceSession::execute(const contracts::Command &command) {
    return execute(command, {});
}
bool DeviceSession::execute(const contracts::Command &command, std::stop_token stop) {
    if (stop.stop_requested()) return false;
    require(connected_ && latest_size_.width > 0 && latest_size_.height > 0,
            "DEVICE_INPUT_NOT_READY");
    require(control_ != nullptr, "SCRCPY_CONTROL_MISSING");
    if (!control_->connected()) control_->connect(15000ms, stop);
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

std::optional<LifecycleObservation> DeviceSession::observe_lifecycle() {
    const auto target = lifecycle_target();
    platform::MetadataQuery query_manager;
    const auto metadata = query_manager.run(platform::path_from_utf8(binding_.at("manager")),
                                            binding_.at("index").get<int>(), 3s);
    if (!metadata.value("success", false)) return {};
    const auto &live = metadata.at("data");
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
                                std::chrono::steady_clock::now(), focused};
}

bool DeviceSession::vpn_ui_step(const std::string &package, bool &start_clicked,
                                 const std::function<bool()> &cancelled) {
    if (cancelled() || vpn_connected()) return false;
    const auto focus = foreground();
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
    if (!control_->connected()) control_->connect(15000ms);
    control_->submit(contracts::Command{.kind = contracts::ActionKind::Click,
        .x = target->x, .y = target->y}, latest_size_.width, latest_size_.height);
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
        disconnect(); return connect();
    }
    if (operation == LifecycleOperation::RestartInstance) {
        // Instance restart remains a separately authorized recovery operation.
        disconnect();
        const auto manager = platform::path_from_utf8(binding_.at("manager"));
        const auto result = platform::run_process(manager,
            {L"control", L"-v", std::to_wstring(binding_.at("index").get<int>()), L"restart"},
            30000ms, {}, 1024 * 1024, false);
        if (result.state != platform::ProcessState::Exited || result.exit_code || cancelled())
            return false;
        const auto deadline = std::chrono::steady_clock::now() + 120s;
        while (!cancelled() && std::chrono::steady_clock::now() < deadline) {
            platform::MetadataQuery metadata;
            const auto live = metadata.run(manager, binding_.at("index").get<int>(), 3s);
            if (live.value("success", false) &&
                live.at("data").value("is_android_started", false)) return connect();
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
        const auto deadline = std::chrono::steady_clock::now() + 10s;
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
        throw std::runtime_error("VPN_PERMISSION_OR_PROFILE_REQUIRED");
    }
    return false;
}
} // namespace wvd::devices
