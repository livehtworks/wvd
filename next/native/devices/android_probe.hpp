#pragma once
// Android 命令结果解析：设备通信成功、远端命令状态、业务事实三层分离。
// 这里不发命令、不持有 SDK；调用者只允许传入固定的只读/生命周期命令。
#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <optional>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace wvd::devices::android {
struct ShellReply { int exit_code{}; std::string output; };
enum class Presence { Absent, Present };
inline std::string trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
    return std::string(text);
}
inline bool package_name(std::string_view text) {
    if (text.empty() || text.size() > 256 || text.front() == '.' || text.back() == '.') return false;
    bool component = false;
    for (const auto c : text) {
        if (c == '.') { if (!component) return false; component = false; }
        else { if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return false; component = true; }
    }
    return component;
}
inline std::string probe_command(const std::string &fixed_command, const std::string &marker) {
    if (fixed_command.empty() || fixed_command.size() > 8192 || marker.empty() ||
        marker.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_") != std::string::npos)
        throw std::runtime_error("ADB_PROBE_COMMAND_INVALID");
    // 最后的 printf 允许 SDK 正常收集业务负结果，原 rc 没有被丢弃。
    // 通道断开/超时仍由 Maa shell 调用结果报告，缺尾标也绝不视为命令成功。
    return "(" + fixed_command + ") 2>&1; __wvd_rc=$?; printf '\\n" + marker + "%d\\n' \"$__wvd_rc\"";
}
inline ShellReply parse_shell_reply(const std::string &raw, const std::string &marker) {
    if (raw.size() > 2 * 1024 * 1024 || marker.empty()) throw std::runtime_error("ADB_PROBE_OUTPUT_INVALID");
    const auto last = raw.rfind("\n" + marker);
    if (last == std::string::npos || raw.find(marker) != last + 1)
        throw std::runtime_error("ADB_PROBE_TRAILER_MISSING_OR_DUPLICATE");
    const auto status = trim(std::string_view(raw).substr(last + 1 + marker.size()));
    int code = -1;
    const auto [end, ec] = std::from_chars(status.data(), status.data() + status.size(), code);
    if (ec != std::errc{} || end != status.data() + status.size() || code < 0 || code > 255)
        throw std::runtime_error("ADB_PROBE_TRAILER_INVALID");
    return {code, raw.substr(0, last)};
}
inline Presence process(const ShellReply &reply) {
    const auto body = trim(reply.output);
    if (reply.exit_code == 1 && body.empty()) return Presence::Absent;
    if (reply.exit_code != 0 || body.empty()) throw std::runtime_error("ANDROID_PID_QUERY_FAILED");
    std::size_t offset = 0;
    while (offset < body.size()) {
        while (offset < body.size() && std::isspace(static_cast<unsigned char>(body[offset]))) ++offset;
        const auto start = offset;
        while (offset < body.size() && std::isdigit(static_cast<unsigned char>(body[offset]))) ++offset;
        std::uint64_t pid = 0;
        const auto [end, ec] = std::from_chars(body.data() + start, body.data() + offset, pid);
        if (start == offset || ec != std::errc{} || end != body.data() + offset || pid == 0 ||
            (offset < body.size() && !std::isspace(static_cast<unsigned char>(body[offset]))))
            throw std::runtime_error("ANDROID_PID_QUERY_MALFORMED");
    }
    return Presence::Present;
}
inline bool installed(const ShellReply &reply, const std::string &package) {
    if (!package_name(package) || reply.exit_code != 0) throw std::runtime_error("ANDROID_PACKAGE_QUERY_FAILED");
    const auto wanted = "package:" + package;
    std::size_t begin = 0;
    while (begin < reply.output.size()) {
        auto end = reply.output.find('\n', begin);
        if (end == std::string::npos) end = reply.output.size();
        if (trim(std::string_view(reply.output).substr(begin, end - begin)) == wanted) return true;
        begin = end + 1;
    }
    return false;
}
inline std::string focus(const std::string &text) {
    // mFocusedApp 不能证明当前焦点：锁屏/系统弹窗可能遮挡游戏。
    // 多显示器焦点不一致时返回未知，不随意选择第一个游戏窗口。
    static const std::regex expression(R"(mCurrentFocus\s*=\s*Window\{[^\r\n]*\s([A-Za-z0-9_.]+)/[^\r\n]*\})");
    std::set<std::string> packages;
    for (std::sregex_iterator i(text.begin(), text.end(), expression), end; i != end; ++i)
        packages.insert((*i)[1].str());
    return packages.size() == 1 ? *packages.begin() : std::string{};
}
inline bool tun_up(const ShellReply &reply) {
    if (reply.exit_code != 0) {
        // 仅对象不存在是负结果；权限错误、ip 不存在等仍是 Error。
        if (reply.exit_code == 1 && reply.output.find("does not exist") != std::string::npos)
            return false;
        throw std::runtime_error("ANDROID_TUN_QUERY_FAILED");
    }
    static const std::regex header(R"((^|\n)\s*\d+:\s*tun\d+(?:@[^: ]+)?:\s*<([^>]+)>)");
    for (std::sregex_iterator it(reply.output.begin(), reply.output.end(), header), end; it != end; ++it) {
        const auto flags = "," + (*it)[2].str() + ",";
        if (flags.find(",UP,") != std::string::npos) return true;
    }
    return false;
}
inline bool live_vpn(const ShellReply &reply) {
    if (reply.exit_code != 0) throw std::runtime_error("ANDROID_CONNECTIVITY_QUERY_FAILED");
    // 只读实时 NetworkAgentInfo 块；日志中的 VPN 字样不能证明当前 VPN 连接。
    // 不同 Android 版本文本不等价，不能把不认识的格式当成已连接。
    std::size_t begin = 0;
    while ((begin = reply.output.find("NetworkAgentInfo{", begin)) != std::string::npos) {
        const auto line_start = reply.output.rfind('\n', begin);
        const auto prefix = trim(std::string_view(reply.output).substr(
            line_start == std::string::npos ? 0 : line_start + 1,
            begin - (line_start == std::string::npos ? 0 : line_start + 1)));
        auto end = reply.output.find("\n\n", begin);
        auto next = reply.output.find("NetworkAgentInfo{", begin + 1);
        if (end == std::string::npos) end = reply.output.size();
        if (next != std::string::npos) end = std::min(end, next);
        const auto block = reply.output.substr(begin, end - begin);
        const bool current_line = prefix.empty();
        const bool connected = block.find("CONNECTED/CONNECTED") != std::string::npos ||
                               block.find("state: CONNECTED") != std::string::npos;
        static const std::regex transport(R"(Transports:\s*[^\r\n]*\bVPN\b)");
        const bool vpn = std::regex_search(block, transport) || block.find("type: VPN") != std::string::npos;
        if (current_line && connected && vpn && block.find("DISCONNECTED") == std::string::npos) return true;
        begin = end;
    }
    return false;
}
struct UiTarget { int x{}, y{}; std::string package, text; };
struct ActivityTarget { std::string package, activity; };

inline std::optional<ActivityTarget> top_resumed_activity(const ShellReply &reply) {
    if (reply.exit_code != 0) throw std::runtime_error("ANDROID_ACTIVITY_QUERY_FAILED");
    static const std::regex expression(
        R"(topResumedActivity\s*=\s*ActivityRecord\{[^\r\n]*\s([A-Za-z0-9_.]+)/([A-Za-z0-9_.$]+)(?:\s|\}))");
    std::set<std::pair<std::string, std::string>> activities;
    for (std::sregex_iterator i(reply.output.begin(), reply.output.end(), expression), end;
         i != end; ++i)
        activities.emplace((*i)[1].str(), (*i)[2].str());
    if (activities.size() != 1) return std::nullopt;
    const auto &[package, activity] = *activities.begin();
    return ActivityTarget{package, activity};
}

inline std::optional<UiTarget> clash_main_start_target(
    const ShellReply &activity_reply, const std::string &expected_package,
    int width, int height) {
    if (!package_name(expected_package))
        throw std::runtime_error("VPN_APPLICATION_INVALID");
    const auto resumed = top_resumed_activity(activity_reply);
    if (!resumed || resumed->package != expected_package ||
        !((width == 900 && height == 1600) || (width == 1600 && height == 900)))
        return std::nullopt;
    if (resumed->activity != "com.github.kr328.clash.MainActivityAlias" &&
        resumed->activity != "com.github.kr328.clash.MainActivity")
        return std::nullopt;
    // MuMu Extras 已观察到按窗口方向返回 900x1600 或 1600x900；两种帧的
    // Clash 顶部状态卡片都覆盖宽度中点，中心纵坐标固定为 243。
    return UiTarget{width / 2, 243, expected_package,
                    "verified-clash-main-start-card"};
}

inline std::string attribute(const std::string &node, const std::string &key) {
    const std::regex attr("(?:^|\\s)" + key + "=\"([^\"]*)\"");
    std::smatch result;
    return std::regex_search(node, result, attr) ? result[1].str() : std::string{};
}
inline std::optional<UiTarget> unique_ui_target(const std::string &xml,
    const std::string &expected_package, const std::vector<std::string> &labels,
    bool permission_button) {
    if (!package_name(expected_package) || xml.size() > 2 * 1024 * 1024)
        throw std::runtime_error("VPN_UI_XML_INVALID");
    static const std::regex bounds(R"(\[(\d+),(\d+)\]\[(\d+),(\d+)\])");
    std::optional<UiTarget> selected;
    std::size_t begin = 0;
    while ((begin = xml.find("<node ", begin)) != std::string::npos) {
        const auto end = xml.find('>', begin);
        if (end == std::string::npos) throw std::runtime_error("VPN_UI_XML_INVALID");
        const auto node = xml.substr(begin, end - begin + 1);
        begin = end + 1;
        if (attribute(node,"package") != expected_package || attribute(node,"enabled") != "true") continue;
        const auto text = attribute(node,"text");
        if (std::find(labels.begin(), labels.end(), text) == labels.end()) continue;
        if (permission_button && (attribute(node,"resource-id") != "android:id/button1" ||
                                  attribute(node,"clickable") != "true")) continue;
        std::smatch match;
        const auto value = attribute(node,"bounds");
        if (!std::regex_match(value, match, bounds)) continue;
        int coordinates[4]{};
        for (int i=0; i<4; ++i) {
            const auto number = match[i+1].str();
            auto [p, ec] = std::from_chars(number.data(), number.data()+number.size(), coordinates[i]);
            if (ec != std::errc{} || p != number.data()+number.size() || coordinates[i] > 16384)
                throw std::runtime_error("VPN_UI_BOUNDS_INVALID");
        }
        if (coordinates[2] <= coordinates[0] || coordinates[3] <= coordinates[1]) continue;
        const UiTarget found{(coordinates[0]+coordinates[2])/2,(coordinates[1]+coordinates[3])/2,expected_package,text};
        if (selected && (selected->x != found.x || selected->y != found.y))
            throw std::runtime_error("VPN_UI_TARGET_AMBIGUOUS");
        selected = found;
    }
    return selected;
}
} // namespace wvd::devices::android
