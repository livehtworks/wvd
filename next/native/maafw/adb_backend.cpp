#include "adb_backend.hpp"
#include "platform/windows/mumu_binding.hpp"
#include <regex>
#include <thread>

namespace wvd::maafw {
using namespace std::chrono_literals;
namespace {
void check(bool value, const char *code) {
    if (!value)
        throw std::runtime_error(code);
}
} // namespace
AdbBackend::AdbBackend(const std::filesystem::path &binding, bool encode_only)
    : binding_(platform::verify_mumu_binding(binding)), verified_(true), encode_only_(encode_only),
      route_(encode_only) {}
AdbBackend::~AdbBackend() {
    disconnect();
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
    return route_.connect([this](bool encode) { return open(encode); });
}
void AdbBackend::disconnect() {
    controller_.reset();
}
std::string AdbBackend::foreground() {
    // Android 15 的 windows 子段不含 mCurrentFocus；完整 window 报告才包含焦点。
    check(wait(MaaControllerPostShell(controller_.get(), "dumpsys window", 5000)),
          "FOREGROUND_QUERY_FAILED");
    auto out = string_buffer();
    check(MaaControllerGetShellOutput(controller_.get(), out.get()), "FOREGROUND_OUTPUT_FAILED");
    std::string text = MaaStringBufferGet(out.get());
    std::smatch match;
    check(std::regex_search(
              text, match,
              std::regex(R"(mCurrentFocus=Window\{[^\r\n]*\s([A-Za-z0-9_.]+)/[^\r\n]*\})")),
          "FOREGROUND_UNCONFIRMED");
    return match[1].str();
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
