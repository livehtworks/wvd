#include "adb_command.hpp"
#include "platform/execution_timing.hpp"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <stdexcept>
#include <windows.h>

namespace wvd::devices {
namespace {
std::atomic<std::uint64_t> marker_id{};

std::wstring wide(const std::string &utf8) {
    if (utf8.empty() || utf8.find('\0') != std::string::npos)
        throw std::runtime_error("ADB_ARGUMENT_INVALID");
    const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (count <= 0) throw std::runtime_error("ADB_ARGUMENT_ENCODING_INVALID");
    std::wstring result(count, L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        utf8.data(), static_cast<int>(utf8.size()), result.data(), count) != count)
        throw std::runtime_error("ADB_ARGUMENT_ENCODING_INVALID");
    return result;
}

std::string as_text(const std::vector<std::uint8_t> &bytes) {
    return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}

void require_transport(const platform::ProcessResult &result) {
    if (result.state == platform::ProcessState::Cancelled)
        throw std::runtime_error("ADB_CANCELLED");
    if (result.state == platform::ProcessState::TimedOut)
        throw std::runtime_error("ADB_TIMEOUT");
    if (result.exit_code != 0)
        throw std::runtime_error("ADB_TRANSPORT_FAILED:" + as_text(result.stderr_bytes));
}
} // namespace

AdbCommandClient::AdbCommandClient(std::filesystem::path executable, std::string serial)
    : executable_(std::move(executable)), serial_(std::move(serial)) {
    if (!executable_.is_absolute() || !std::filesystem::is_regular_file(executable_) ||
        serial_.empty() || serial_.size() > 128 ||
        !std::all_of(serial_.begin(), serial_.end(), [](unsigned char c) {
            return std::isalnum(c) || c == ':' || c == '.' || c == '_' || c == '-';
        }))
        throw std::runtime_error("ADB_DEVICE_BINDING_INVALID");
}

platform::ProcessResult AdbCommandClient::run(const std::vector<std::wstring> &arguments,
                                               std::chrono::milliseconds timeout,
                                               std::stop_token stop,
                                               std::size_t output_limit) const {
    std::vector<std::wstring> scoped{L"-s", wide(serial_)};
    platform::timing::count(platform::timing::Counter::AdbClients);
    scoped.insert(scoped.end(), arguments.begin(), arguments.end());
    // ADB may launch its shared server; cancelling our client must not kill that daemon.
    return platform::run_process(executable_, scoped, timeout, stop, output_limit, false);
}

android::ShellReply AdbCommandClient::shell_fixed(const std::string &command,
                                                   std::chrono::milliseconds timeout,
                                                   std::stop_token stop,
                                                   std::size_t output_limit) const {
    if (command.empty() || command.size() > 8192 || output_limit == 0 ||
        output_limit > 8ULL * 1024 * 1024)
        throw std::runtime_error("ADB_COMMAND_INVALID");
    const auto marker = "__WVD_RC_" + std::to_string(GetCurrentProcessId()) + "_" +
        std::to_string(++marker_id) + "__";
    const auto script = android::probe_command(command, marker);
    // ADB shell 自己会将完整命令交给设备端 shell；再嵌一层 sh -c 会让
    // 部分 MuMu ADB 把括号脚本拆成多个实参，连接后的首个查询就语法失败。
    const auto result = run({L"shell", wide(script)}, timeout, stop, output_limit);
    require_transport(result);
    return android::parse_shell_reply(as_text(result.stdout_bytes), marker, output_limit);
}

std::vector<std::uint8_t> AdbCommandClient::screenshot_png(
    std::chrono::milliseconds timeout, std::stop_token stop) const {
    auto result = run({L"exec-out", L"screencap", L"-p"}, timeout, stop,
                      64 * 1024 * 1024);
    require_transport(result);
    const auto &bytes = result.stdout_bytes;
    constexpr std::uint8_t png[] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (bytes.size() < sizeof(png) || !std::equal(std::begin(png), std::end(png), bytes.begin()))
        throw std::runtime_error("ADB_SCREENSHOT_NOT_PNG");
    return bytes;
}

bool AdbCommandClient::connected(std::stop_token stop) const {
    auto result = run({L"get-state"}, std::chrono::seconds{5}, stop);
    if (result.state == platform::ProcessState::Cancelled)
        throw std::runtime_error("ADB_CANCELLED");
    if (result.state != platform::ProcessState::Exited || result.exit_code != 0)
        return false;
    return android::trim(as_text(result.stdout_bytes)) == "device";
}

bool AdbCommandClient::connect(std::stop_token stop) const {
    // `adb connect` is explicitly scoped by its address, not a global server reset.
    const auto result = platform::run_process(executable_, {L"connect", wide(serial_)},
        std::chrono::seconds{20}, stop, 4096, false);
    require_transport(result);
    return connected(stop);
}
} // namespace wvd::devices
