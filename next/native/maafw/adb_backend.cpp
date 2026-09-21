#include "adb_backend.hpp"
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
            diagnostics_.push_back({{"outcome", "STOP_TIMEOUT"},
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
    diagnostics_.push_back({{"event", "connect"},
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
        diagnostics_.push_back({{"event", "device_properties"},
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
    if (connected && !binding_.contains("vpn_application_id")) {
        const auto detected = clash_package();
        if (!detected.empty())
            binding_["vpn_application_id"] = detected;
    }
    return connected;
}
void AdbBackend::disconnect() { controller_.reset(); }
std::string AdbBackend::foreground_probe() {
    // Android 15 的 windows 子段不含 mCurrentFocus；完整 window 报告才包含焦点。
    check(wait(MaaControllerPostShell(controller_.get(), "dumpsys window", 5000)),
          "FOREGROUND_QUERY_FAILED");
    auto out = string_buffer();
    check(MaaControllerGetShellOutput(controller_.get(), out.get()), "FOREGROUND_OUTPUT_FAILED");
    std::string text = MaaStringBufferGet(out.get());
    std::smatch match;
    if (std::regex_search(
            text, match,
            std::regex(R"(mCurrentFocus=Window\{[^\r\n]*\s([A-Za-z0-9_.]+)/[^\r\n]*\})")))
        return match[1].str();
    return {};
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
std::string AdbBackend::shell_probe(const std::string &command, int timeout) {
    // pidof、ip addr 等状态探测会用非零退出码表达“对象不存在”。这不是 ADB
    // 传输失败；显式归零远端命令退出码，同时仍让控制器连接/管道错误正常抛出。
    return shell("(" + command + ") 2>&1; exit 0", timeout);
}
bool AdbBackend::start_package(const std::string &package) {
    if (package.empty() || package.size() > 256 ||
        !std::all_of(package.begin(), package.end(), [](char c) {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_';
        }))
        return false;
    const auto activity = shell("cmd package resolve-activity --brief " + package);
    std::smatch match;
    if (std::regex_search(activity, match,
                          std::regex(R"(([A-Za-z0-9_.]+/[A-Za-z0-9_.$]+))"))) {
        const auto output = shell("am start -n " + match[1].str());
        return output.find("Error") == std::string::npos &&
               output.find("Exception") == std::string::npos;
    }
    const auto output = shell("monkey -p " + package +
                              " -c android.intent.category.LAUNCHER 1", 10000);
    return output.find("No activities found") == std::string::npos;
}
bool AdbBackend::vpn_connected() {
    const auto tun = shell_probe("ip addr show tun0", 5000);
    if (tun.find("tun0") != std::string::npos &&
        tun.find("does not exist") == std::string::npos)
        return true;
    return shell("dumpsys connectivity", 8000).find("Transports: VPN") != std::string::npos;
}
std::string AdbBackend::clash_package() {
    const auto packages = shell("pm list packages", 8000);
    for (const auto *candidate : {"com.github.metacubex.clash.meta", "com.github.kr328.clash",
                                  "com.github.kr328.clash.foss"})
        if (packages.find(std::string("package:") + candidate) != std::string::npos)
            return candidate;
    std::smatch match;
    if (std::regex_search(packages, match,
                          std::regex(R"(package:([^\s]*clash[^\s]*))", std::regex::icase)))
        return match[1].str();
    return {};
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
    return {binding_.at("serial"), std::to_string(binding_.at("index").get<int>()),
            binding_.value("application_id", "jp.co.drecom.wizardry.daphne"),
            binding_.value("vpn_application_id", "com.github.metacubex.clash.meta"),
            binding_.value("vpn_required", false)};
}
std::optional<devices::LifecycleObservation> AdbBackend::observe_lifecycle() {
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
        const auto process = shell_probe("pidof " + target.application_id);
        running = std::regex_search(process, std::regex(R"((^|\s)\d+($|\s))"));
        // 应用刚启动时 mCurrentFocus 可以短暂为空。生命周期轮询把它视为“尚未
        // 前台”并继续等待；常规截图仍通过 foreground() 保持严格确认。
        focused = foreground_probe() == target.application_id;
        if (target.vpn_required)
            vpn = vpn_connected();
    }
    return devices::LifecycleObservation{target, instance, connected, running, vpn,
                                         connection_generation_, std::chrono::steady_clock::now(),
                                         focused};
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
    if (operation == O::RestartInstance)
        return restart_instance(cancelled);
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
        if (vpn_connected())
            return true;
        const auto package = clash_package();
        if (package.empty() || package != target.vpn_application_id)
            return false;
        const auto action = package + ".action.START_CLASH";
        const auto first = shell("am start -n " + package +
                                 "/com.github.kr328.clash.ExternalControlActivity -a " + action);
        if (first.find("Error") != std::string::npos || first.find("Exception") != std::string::npos)
            shell("am start -a " + action + " -p " + package);
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        while (!cancelled() && std::chrono::steady_clock::now() < deadline) {
            if (vpn_connected())
                return true;
            std::this_thread::sleep_for(500ms);
        }
        return false;
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
    auto app = foreground();
    std::string viewport = std::to_string(size.width) + "x" + std::to_string(size.height);
    return {std::move(bytes),
            size,
            binding_.at("serial"),
            viewport,
            app,
            captured,
            encode_ ? "ADB_ENCODE" : "MUMU_EXTRAS",
            connection_generation_};
}
devices::RawFrame AdbBackend::capture() {
    return route_.capture([this] { return capture_current(); },
                          [this](bool encode) { return open(encode); });
}
bool AdbBackend::context_matches(const contracts::FrameIdentity &identity, const std::string &app) {
    if (!identity.frame_id)
        return false;
    auto actual = capture();
    return actual.connection_generation == identity.connection_generation &&
           actual.size == identity.raw_size && actual.device_id == identity.device_id &&
           actual.viewport_id == identity.viewport_id && actual.foreground_application == app;
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
