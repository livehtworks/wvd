#include "adb_backend.hpp"
#include "devices/android_viewport.hpp"
#include "platform/windows/runtime_files.hpp"
#include "platform/windows/mumu_binding.hpp"
#include "platform/windows/metadata_query.hpp"
#include <algorithm>
#include <cctype>
#include <regex>
#include <thread>
#include <windows.h>

namespace wvd::maafw {
using namespace std::chrono_literals;
namespace {
void check(bool value, const char *code) {
    if (!value)
        throw std::runtime_error(code);
}
} // namespace
AdbBackend::AdbBackend(const std::filesystem::path &binding, bool encode_only,
                       std::stop_token cancellation)
    : binding_(platform::verify_mumu_binding(binding, cancellation)), verified_(true),
      encode_only_(encode_only), route_(encode_only) {}
AdbBackend::AdbBackend(nlohmann::json binding, bool encode_only,
                       std::stop_token cancellation)
    : binding_(platform::verify_mumu_binding(binding, cancellation)), verified_(true),
      encode_only_(encode_only), route_(encode_only) {}
AdbBackend::~AdbBackend() { disconnect(); }
void AdbBackend::record(nlohmann::json value) {
    std::lock_guard lock(diagnostics_mutex_);
    // 状态页读取值副本，不再并发读正在增长的 vector/JSON。
    if (diagnostics_.size() >= 256) diagnostics_.erase(diagnostics_.begin());
    diagnostics_.push_back(std::move(value));
}
nlohmann::json AdbBackend::diagnostics() const {
    std::lock_guard lock(diagnostics_mutex_);
    return {{"connections", diagnostics_}, {"failures", route_failures_}};
}
void AdbBackend::set_vpn_required(bool required) {
    std::lock_guard lock(diagnostics_mutex_);
    binding_["vpn_required"] = required;
}
bool AdbBackend::matches_selection(const std::filesystem::path &manager, int index,
                                   const std::string &serial) const {
    std::lock_guard lock(diagnostics_mutex_);
    if (binding_.at("serial") != serial || binding_.at("index") != index) return false;
    std::error_code error;
    const auto actual = std::filesystem::canonical(path_from_utf8(binding_.at("manager")), error);
    if (error) return false;
    const auto selected = std::filesystem::canonical(manager, error);
    return !error && selected == actual;
}
bool AdbBackend::wait(MaaCtrlId id) {
    if (id == MaaInvalidId)
        return false;
    auto started = std::chrono::steady_clock::now();
    bool reported = false;
    while (true) {
        auto status = MaaControllerStatus(controller_.get(), id);
        if (status != MaaStatus_Pending && status != MaaStatus_Running) {
            if (reported)
                throw std::runtime_error("STOP_TIMEOUT");
            return status == MaaStatus_Succeeded;
        }
        if (!reported && std::chrono::steady_clock::now() - started > 30s) {
            record({{"outcome", "STOP_TIMEOUT"},
                                    {"quiescent", false},
                                    {"connection_generation", connection_generation_}});
            reported = true;
        }
        std::this_thread::sleep_for(5ms);
    }
}
bool AdbBackend::open(bool encode) {
    disconnect();
    encode_ = encode;
    ++connection_generation_;
    nlohmann::json config = {{"extras",
                              {{"mumu",
                                {{"enable", !encode},
                                 {"path", binding_.at("install_root")},
                                 {"index", binding_.at("index")}}}}}};
    // 固定 SDK 的内部重试会调用 KillServer。通过其公开 command 配置替换成
    // 仅查询本设备的 get-state，禁止杀全局 ADB；SDK 内部重试等待仍可能很长。
    config["command"]["KillServer"] = {"{ADB}", "-s", "{ADB_SERIAL}", "get-state"};
    controller_.reset(MaaAdbControllerCreate(
        binding_.at("adb").get<std::string>().c_str(),
        binding_.at("serial").get<std::string>().c_str(),
        encode ? MaaAdbScreencapMethod_Encode : MaaAdbScreencapMethod_EmulatorExtras,
        MaaAdbInputMethod_AdbShell, config.dump().c_str(), ""));
    check(bool(controller_), "ADB_CONTROLLER_CREATE_FAILED");
    bool raw = true;
    check(MaaControllerSetOption(controller_.get(), MaaCtrlOption_ScreenshotUseRawSize, &raw,
                                 sizeof(raw)),
          "ADB_RAW_SIZE_FAILED");
    bool ok = wait(MaaControllerPostConnection(controller_.get()));
    record({{"event", "connect"},
                            {"backend", encode ? "ADB_ENCODE" : "MUMU_EXTRAS"},
                            {"success", ok},
                            {"connection_generation", connection_generation_}});
    if (ok) {
        check(wait(MaaControllerPostShell(controller_.get(), "getprop ro.build.version.release",
                                          5000)),
              "ANDROID_VERSION_QUERY_FAILED");
        auto version = string_buffer();
        check(MaaControllerGetShellOutput(controller_.get(), version.get()),
              "ANDROID_VERSION_OUTPUT_FAILED");
        check(wait(MaaControllerPostShell(controller_.get(), "pm path jp.co.drecom.wizardry.daphne",
                                          5000)),
              "GAME_PACKAGE_QUERY_FAILED");
        auto package = string_buffer();
        check(MaaControllerGetShellOutput(controller_.get(), package.get()),
              "GAME_PACKAGE_OUTPUT_FAILED");
        bool installed =
            std::string(MaaStringBufferGet(package.get())).find("package:") != std::string::npos;
        record({{"event", "device_properties"},
                                {"android_release", MaaStringBufferGet(version.get())},
                                {"game_installed", installed},
                                {"global_adb_restart", "DISABLED_READ_ONLY_GET_STATE"}});
        check(installed, "TARGET_GAME_NOT_INSTALLED");
    }
    return ok;
}
bool AdbBackend::connect() {
    if (controller_ && MaaControllerConnected(controller_.get()))
        return true;
    const bool connected = route_.connect([this](bool encode) { return open(encode); });
    { std::lock_guard lock(diagnostics_mutex_); route_failures_ = route_.failures(); }
    if (connected && !binding_.contains("vpn_application_id")) {
        const auto detected = clash_package();
        if (!detected.empty()) {
            std::lock_guard lock(diagnostics_mutex_);
            binding_["vpn_application_id"] = detected;
        }
    }
    return connected;
}
void AdbBackend::disconnect() { controller_.reset(); }
std::string AdbBackend::foreground_probe() {
    const auto reply = probe_reply("dumpsys window", 5000);
    check(reply.exit_code == 0, "FOREGROUND_QUERY_FAILED");
    return devices::android::focus(reply.output);
}
std::string AdbBackend::foreground() {
    const auto application = foreground_probe();
    check(!application.empty(), "FOREGROUND_UNCONFIRMED");
    return application;
}
std::string AdbBackend::shell(const std::string &command, int timeout) {
    check(bool(controller_), "ADB_NOT_CONNECTED");
    check(wait(MaaControllerPostShell(controller_.get(), command.c_str(), timeout)),
          "ADB_SHELL_FAILED");
    auto out = string_buffer();
    check(MaaControllerGetShellOutput(controller_.get(), out.get()), "ADB_SHELL_OUTPUT_FAILED");
    return MaaStringBufferGet(out.get());
}
devices::android::ShellReply AdbBackend::probe_reply(const std::string &command, int timeout) {
    auto marker = "WVD_RC_" + platform::unique_id();
    marker.erase(std::remove(marker.begin(), marker.end(), '-'), marker.end());
    marker.erase(std::remove(marker.begin(), marker.end(), '{'), marker.end());
    marker.erase(std::remove(marker.begin(), marker.end(), '}'), marker.end());
    marker += "_";
    return devices::android::parse_shell_reply(
        shell(devices::android::probe_command(command, marker), timeout), marker);
}
std::string AdbBackend::shell_probe(const std::string &command, int timeout) {
    auto result = probe_reply(command, timeout);
    check(result.exit_code == 0, "ADB_REMOTE_COMMAND_FAILED");
    return result.output;
}
bool AdbBackend::start_package(const std::string &package) {
    check(devices::android::package_name(package), "ANDROID_PACKAGE_NAME_INVALID");
    const auto activity = probe_reply("cmd package resolve-activity --brief " + package);
    std::smatch match;
    if (activity.exit_code == 0 && std::regex_search(activity.output, match,
        std::regex(R"(([A-Za-z0-9_.]+/[A-Za-z0-9_.$]+))")) &&
        match[1].str().starts_with(package + "/")) {
        const auto result = probe_reply("am start -n " + match[1].str());
        return result.exit_code == 0 && result.output.find("Error") == std::string::npos &&
               result.output.find("Exception") == std::string::npos;
    }
    // No launcher activity 使用原版的包级启动后备；不把任何 Activity 当成目标包。
    if (activity.exit_code != 0 && activity.output.find("No activity") == std::string::npos)
        throw std::runtime_error("ANDROID_ACTIVITY_QUERY_FAILED");
    const auto result = probe_reply("monkey -p " + package +
        " -c android.intent.category.LAUNCHER 1", 10000);
    return result.exit_code == 0 && result.output.find("No activities found") == std::string::npos &&
           result.output.find("Error") == std::string::npos && result.output.find("Exception") == std::string::npos;
}
bool AdbBackend::vpn_connected() {
    const auto tun = probe_reply("ip addr show", 5000);
    if (devices::android::tun_up(tun)) return true;
    return devices::android::live_vpn(probe_reply("dumpsys connectivity", 8000));
}
std::string AdbBackend::clash_package() {
    const auto packages = probe_reply("pm list packages", 8000);
    check(packages.exit_code == 0, "ANDROID_PACKAGE_QUERY_FAILED");
    for (const auto *candidate : {"com.github.metacubex.clash.meta", "com.github.kr328.clash",
                                  "com.github.kr328.clash.foss"})
        if (devices::android::installed(packages, candidate)) return candidate;
    std::set<std::string> candidates;
    const std::regex expression(R"((?:^|\n)package:([^\r\n]*clash[^\r\n]*)(?:\r?\n|$))", std::regex::icase);
    for (std::sregex_iterator i(packages.output.begin(), packages.output.end(), expression), end; i != end; ++i) {
        const auto name = devices::android::trim((*i)[1].str());
        if (devices::android::package_name(name)) candidates.insert(name);
    }
    check(candidates.size() <= 1, "VPN_APPLICATION_AMBIGUOUS");
    return candidates.empty() ? std::string{} : *candidates.begin();
}
bool AdbBackend::launch_instance(const std::function<bool()> &cancelled) {
    if (cancelled())
        return false;
    const auto manager = path_from_utf8(binding_.at("manager"));
    if (!manager.is_absolute() || !std::filesystem::is_regular_file(manager))
        return false;
    std::wstring command = L"\"" + manager.wstring() + L"\" control -v " +
                           std::to_wstring(binding_.at("index").get<int>()) + L" launch";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, manager.parent_path().c_str(), &startup, &process))
        return false;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    const auto deadline = std::chrono::steady_clock::now() + 90s;
    while (!cancelled() && std::chrono::steady_clock::now() < deadline) {
        platform::MetadataQuery query;
        auto live = query.run(path_from_utf8(binding_.at("manager")),
                              binding_.at("index").get<int>(), 3s);
        if (live.value("success", false) && live.at("data").value("is_android_started", false))
            return open(encode_only_);
        std::this_thread::sleep_for(500ms);
    }
    return false;
}
bool AdbBackend::restart_instance(const std::function<bool()> &cancelled) {
    if (cancelled())
        return false;
    const auto manager = path_from_utf8(binding_.at("manager"));
    if (!manager.is_absolute() || !std::filesystem::is_regular_file(manager))
        return false;
    disconnect();
    std::wstring command = L"\"" + manager.wstring() + L"\" control -v " +
                           std::to_wstring(binding_.at("index").get<int>()) + L" restart";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, manager.parent_path().c_str(), &startup, &process))
        return false;
    CloseHandle(process.hThread);
    // 只等待本次 manager 子命令；取消时不强杀 MuMu，也不影响其他实例。
    while (!cancelled() && WaitForSingleObject(process.hProcess, 200) == WAIT_TIMEOUT) {
    }
    DWORD exit_code = 1;
    const bool exited = GetExitCodeProcess(process.hProcess, &exit_code) &&
                        exit_code != STILL_ACTIVE;
    CloseHandle(process.hProcess);
    if (cancelled() || !exited || exit_code != 0)
        return false;
    const auto deadline = std::chrono::steady_clock::now() + 120s;
    while (!cancelled() && std::chrono::steady_clock::now() < deadline) {
        platform::MetadataQuery query;
        auto live = query.run(manager, binding_.at("index").get<int>(), 3s);
        if (live.value("success", false) && live.at("data").value("is_android_started", false)) {
            const auto &data = live.at("data");
            if (!data.contains("adb_port") ||
                "127.0.0.1:" + std::to_string(data.at("adb_port").get<int>()) !=
                    binding_.at("serial").get<std::string>())
                return false;
            binding_["created_timestamp"] = data.at("created_timestamp");
            binding_["live_manager"] = data;
            return open(encode_only_);
        }
        std::this_thread::sleep_for(500ms);
    }
    return false;
}
devices::LifecycleTarget AdbBackend::lifecycle_target() const {
    std::lock_guard lock(diagnostics_mutex_);
    return {binding_.at("serial"), std::to_string(binding_.at("index").get<int>()),
            binding_.value("application_id", "jp.co.drecom.wizardry.daphne"),
            binding_.value("vpn_application_id", "com.github.metacubex.clash.meta"),
            binding_.value("vpn_required", false)};
}
std::optional<devices::LifecycleObservation> AdbBackend::observe_lifecycle() {
    const auto sampled_at = std::chrono::steady_clock::now();
    const auto target = lifecycle_target();
    platform::MetadataQuery query;
    auto metadata = query.run(path_from_utf8(binding_.at("manager")),
                              binding_.at("index").get<int>(), 3s);
    if (!metadata.value("success", false))
        return {};
    const auto &live = metadata.at("data");
    const bool instance = live.value("is_process_started", false) &&
                          live.value("is_android_started", false);
    const bool connected = instance && controller_ && MaaControllerConnected(controller_.get());
    bool running = false, focused = false, vpn = !target.vpn_required;
    if (connected) {
        running = devices::android::process(probe_reply("pidof " + target.application_id)) ==
                  devices::android::Presence::Present;
        // 应用刚启动时 mCurrentFocus 可以短暂为空。生命周期轮询把它视为“尚未
        // 前台”并继续等待；常规截图仍通过 foreground() 保持严格确认。
        focused = foreground_probe() == target.application_id;
        if (focused && !running) {
            // 进程在两次查询之间启动：复查一次，不将正常前台切换升级为恢复。
            running = devices::android::process(probe_reply("pidof " + target.application_id)) ==
                      devices::android::Presence::Present;
            if (!running) focused = false;
        }
        if (target.vpn_required)
            vpn = vpn_connected();
    }
    return devices::LifecycleObservation{target, instance, connected, running, vpn,
                                         connection_generation_, sampled_at,
                                         focused};
}
bool AdbBackend::vpn_ui_step(const std::string &package, bool &start_clicked,
                             const std::function<bool()> &cancelled) {
    if (cancelled() || vpn_connected()) return false;
    const auto focus = foreground_probe();
    const bool permission = focus == "com.android.vpndialogs";
    if (!permission && (focus != package || start_clicked)) return false;
    auto id = platform::unique_id();
    id.erase(std::remove_if(id.begin(), id.end(), [](unsigned char c) { return !std::isalnum(c); }), id.end());
    const auto path = "/sdcard/.wvd-vpn-" + id + ".xml";
    const auto response = probe_reply("uiautomator dump " + path +
        " >/dev/null && cat " + path + "; __wvd_ui=$?; rm -f " + path + "; exit $__wvd_ui", 5000);
    const auto labels = permission
        ? std::vector<std::string>{"OK", "Allow", "允许", "确定", "允許", "確定"}
        : std::vector<std::string>{"Tap to start", "点此启动", "點此啟動", "点击启动", "點擊啟動"};
    std::optional<devices::android::UiTarget> target;
    const bool hierarchy = response.exit_code == 0 &&
                           response.output.find("<hierarchy") != std::string::npos;
    if (hierarchy) {
        target = devices::android::unique_ui_target(response.output, focus, labels, permission);
    } else {
        // Android 15 的部分 MuMu 镜像会让 uiautomator dump 直接崩溃。系统授权页绝不
        // 使用坐标后备；只有 Clash 自身主界面、已验证横屏尺寸和精确 Activity 同时
        // 成立时，才允许单次点击启动卡片。
        check(!permission && !start_clicked, "VPN_UI_DUMP_FAILED");
        const auto frame = capture_current();
        check(frame.foreground_application == package, "VPN_UI_CONTEXT_CHANGED");
        const auto activity = probe_reply("dumpsys activity activities", 5000);
        target = devices::android::clash_main_start_target(
            activity, package, frame.size.width, frame.size.height);
        check(target.has_value(), "VPN_UI_FALLBACK_UNCONFIRMED");
        record({{"event", "vpn.ui_fallback"}, {"reason", "UIAUTOMATOR_UNAVAILABLE"},
                {"package", package}, {"viewport", frame.viewport_id},
                {"x", target->x}, {"y", target->y}});
    }
    if (!target) return false;
    if (cancelled() || vpn_connected() || foreground_probe() != focus) return false;
    // 只执行已确认系统授权/当前停止按钮的中心；这条输入归属于 EnsureVpn 生命周期。
    // 游戏像素坐标门禁仍不放行系统包，不能借此暴露任意坐标接口。
    record({{"event", "vpn.ui_action"}, {"package", focus},
            {"kind", permission ? "permission" : "start"}, {"x", target->x}, {"y", target->y}});
    check(wait(MaaControllerPostClick(controller_.get(), target->x, target->y)), "VPN_UI_CLICK_FAILED");
    if (!permission) start_clicked = true;
    if (!permission) {
        const auto verify_deadline = std::chrono::steady_clock::now() + 2s;
        while (!cancelled() && std::chrono::steady_clock::now() < verify_deadline) {
            if (vpn_connected()) {
                record({{"event", "vpn.ui_verified"}, {"connected", true}});
                return true;
            }
            std::this_thread::sleep_for(100ms);
        }
        record({{"event", "vpn.ui_verified"}, {"connected", false}});
    }
    return true;
}
bool AdbBackend::execute_lifecycle(devices::LifecycleOperation operation,
                                   const devices::LifecycleTarget &target,
                                   const std::function<bool()> &cancelled) {
    if (target.device_id != binding_.at("serial").get<std::string>() ||
        target.instance_id != std::to_string(binding_.at("index").get<int>()) ||
        target.application_id != binding_.value("application_id", "jp.co.drecom.wizardry.daphne") ||
        target.vpn_application_id != binding_.value("vpn_application_id", "com.github.metacubex.clash.meta"))
        return false;
    if (cancelled())
        return false;
    using O = devices::LifecycleOperation;
    if (operation == O::RestartInstance) {
        record({{"event", "lifecycle.rejected"}, {"operation", "RestartInstance"},
                {"reason", "INSTANCE_RESTART_NOT_AUTHORIZED"}});
        return false;
    }
    if (operation == O::Reconnect)
        return open(encode_only_);
    if (!controller_ || !MaaControllerConnected(controller_.get()))
        return false;
    if (operation == O::StopApplication) {
        shell("am force-stop " + target.application_id);
        return true;
    }
    if (operation == O::StartApplication)
        return start_package(target.application_id);
    if (operation == O::EnsureVpn) {
        if (vpn_connected()) return true;
        const auto package = clash_package();
        check(!package.empty() && package == target.vpn_application_id, "VPN_APPLICATION_MISMATCH");
        const auto action = package + ".action.START_CLASH";
        const auto first = probe_reply("am start -n " + package +
            "/com.github.kr328.clash.ExternalControlActivity -a " + action);
        if (first.exit_code != 0 || first.output.find("Error") != std::string::npos ||
            first.output.find("Exception") != std::string::npos) {
            if (cancelled()) return false;
            const auto fallback = probe_reply("am start -a " + action + " -p " + package);
            check(fallback.exit_code == 0 && fallback.output.find("Error") == std::string::npos &&
                  fallback.output.find("Exception") == std::string::npos, "VPN_START_INTENT_FAILED");
        }
        bool opened_main = false, start_clicked = false;
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        while (!cancelled() && std::chrono::steady_clock::now() < deadline) {
            if (vpn_connected()) return true;
            // ExternalControlActivity 在需要首次授权时可能只显示 Toast 并退出。
            // 主界面确实显示“停止/点此启动”后才点击；不盲发 TOGGLE、不使用估算坐标。
            if (!opened_main && foreground_probe() != "com.android.vpndialogs") {
                check(start_package(package), "VPN_MAIN_ACTIVITY_FAILED");
                opened_main = true;
            }
            vpn_ui_step(package, start_clicked, cancelled);
            if (cancelled()) return false;
            std::this_thread::sleep_for(200ms);
        }
        if (cancelled()) return false;
        throw std::runtime_error("VPN_PERMISSION_OR_PROFILE_REQUIRED");
    }
    return false;
}
devices::RawFrame AdbBackend::capture_current() {
    check(bool(controller_), "ADB_NOT_CONNECTED");
    auto captured = std::chrono::steady_clock::now();
    check(wait(MaaControllerPostScreencap(controller_.get())), "ADB_CAPTURE_FAILED");
    auto image = image_buffer();
    check(MaaControllerCachedImage(controller_.get(), image.get()) &&
              MaaImageBufferGetRawData(image.get()) && MaaImageBufferType(image.get()) == 16,
          "ADB_CAPTURE_DECODE_FAILED");
    contracts::Size size{MaaImageBufferWidth(image.get()), MaaImageBufferHeight(image.get())};
    check(size.width > 0 && size.height > 0, "ADB_CAPTURE_SIZE_INVALID");
    auto data = MaaImageBufferGetEncoded(image.get());
    auto length = MaaImageBufferGetEncodedSize(image.get());
    check(data && length, "ADB_CAPTURE_ENCODE_FAILED");
    std::vector<std::uint8_t> bytes(data, data + length);
    // 保留采集起点，另记像素就绪时间；查询/识别结束不能伪装成刚拍到的画面。
    const auto capture_finished = std::chrono::steady_clock::now();
    auto app = foreground_probe(); // 暂态仍可预览；InputGate 不给未知前台输入许可。
    int rotation = -1;
    try {
        const auto input = probe_reply("dumpsys input", 5000);
        const auto viewport_state = input.exit_code == 0
            ? devices::android::input_viewport(input.output) : std::nullopt;
        if (viewport_state && viewport_state->size == size) rotation = viewport_state->rotation;
        else record({{"event", "viewport_unconfirmed"}, {"input_allowed", false}});
    } catch (const std::exception &error) {
        // 元数据缺失不转到另一条截图后端；保留预览，但不猜测旋转来批准输入。
        record({{"event", "viewport_unconfirmed"}, {"reason", error.what()}, {"input_allowed", false}});
    }
    std::string viewport = std::to_string(size.width) + "x" + std::to_string(size.height);
    return {std::move(bytes),
            size,
            binding_.at("serial"),
            viewport,
            app,
            captured,
            encode_ ? "ADB_ENCODE" : "MUMU_EXTRAS",
            connection_generation_, capture_finished, rotation};
}
devices::RawFrame AdbBackend::capture() {
    try {
        auto result = route_.capture([this] { return capture_current(); },
                                      [this](bool encode) { return open(encode); });
        { std::lock_guard lock(diagnostics_mutex_); route_failures_ = route_.failures(); }
        return result;
    } catch (...) {
        { std::lock_guard lock(diagnostics_mutex_); route_failures_ = route_.failures(); }
        throw;
    }
}
bool AdbBackend::context_matches(const contracts::FrameIdentity &identity, const std::string &app) {
    if (!identity.frame_id || identity.display_rotation < 0 || !controller_ ||
        !MaaControllerConnected(controller_.get()) ||
        identity.connection_generation != connection_generation_ ||
        identity.device_id != binding_.at("serial").get<std::string>() || app.empty() ||
        identity.foreground_application != app)
        return false;
    // 只复核设备/显示器/前台，不再第二次取整张图，不触发后备连接或改变 frame_id。
    // 两次前台查询不能提供内核级原子点击保证，但会拒绝检测到的切换和歧义。
    if (foreground_probe() != app) return false;
    const auto input = probe_reply("dumpsys input", 5000);
    const auto viewport = input.exit_code == 0
        ? devices::android::input_viewport(input.output) : std::nullopt;
    if (!viewport || viewport->size != identity.raw_size ||
        viewport->rotation != identity.display_rotation ||
        identity.viewport_id != std::to_string(viewport->size.width) + "x" +
                                std::to_string(viewport->size.height))
        return false;
    return foreground_probe() == app && MaaControllerConnected(controller_.get()) &&
           connection_generation_ == identity.connection_generation;
}
bool AdbBackend::execute(const contracts::Command &command) {
    using contracts::ActionKind;
    check(bool(controller_), "ADB_NOT_CONNECTED");
    MaaCtrlId id = MaaInvalidId;
    switch (command.kind) {
    case ActionKind::Click:
        id = MaaControllerPostClick(controller_.get(), command.x, command.y);
        break;
    case ActionKind::Swipe:
        id = MaaControllerPostSwipe(controller_.get(), command.x, command.y, command.x2, command.y2,
                                    command.duration);
        break;
    case ActionKind::ClickKey:
        id = MaaControllerPostClickKey(controller_.get(), command.key);
        break;
    case ActionKind::TouchDown:
        id = MaaControllerPostTouchDown(controller_.get(), command.contact, command.x, command.y,
                                        command.pressure);
        break;
    case ActionKind::TouchMove:
        id = MaaControllerPostTouchMove(controller_.get(), command.contact, command.x, command.y,
                                        command.pressure);
        break;
    case ActionKind::TouchUp:
        id = MaaControllerPostTouchUp(controller_.get(), command.contact);
        break;
    default:
        return false;
    }
    return wait(id);
}
} // namespace wvd::maafw
