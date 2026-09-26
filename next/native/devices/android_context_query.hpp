#pragma once
#include <array>
#include <optional>
#include <string_view>

namespace wvd::devices::android {
// 只合并传输，不删除焦点前/后夹持检查。外层 shell_fixed 继续验证传输和总返回码。
// 每段自己的状态也必须成功，不能让最后一个 printf 吞掉前面 dumpsys 的失败。
inline constexpr std::string_view context_probe_command =
    "printf '__WVD_CTX_V1_BEGIN__\\n'; "
    "dumpsys window; _wvd_ctx_a=$?; "
    "printf '\\n__WVD_CTX_V1_INPUT__\\n'; "
    "dumpsys input; _wvd_ctx_b=$?; "
    "printf '\\n__WVD_CTX_V1_END_FOCUS__\\n'; "
    "dumpsys window; _wvd_ctx_c=$?; "
    "printf '\\n__WVD_CTX_V1_STATUS__:%d:%d:%d\\n' "
    "\"$_wvd_ctx_a\" \"$_wvd_ctx_b\" \"$_wvd_ctx_c\"";

struct ContextDumpViews {
    // 只借用 ShellReply.output；调用方不得在 output 生命周期结束后保留这些视图。
    std::string_view before_focus;
    std::string_view input;
    std::string_view after_focus;
};
inline std::optional<ContextDumpViews> parse_context_dump(std::string_view output) {
    constexpr std::array<std::string_view, 4> tags{
        "__WVD_CTX_V1_BEGIN__", "__WVD_CTX_V1_INPUT__",
        "__WVD_CTX_V1_END_FOCUS__", "__WVD_CTX_V1_STATUS__"};
    if (output.size() > 8ULL * 1024 * 1024) return std::nullopt;
    std::array<std::size_t, 4> positions{};
    for (std::size_t i = 0; i < tags.size(); ++i) {
        positions[i] = output.find(tags[i]);
        if (positions[i] == std::string_view::npos ||
            output.find(tags[i], positions[i] + tags[i].size()) != std::string_view::npos ||
            (i && positions[i] <= positions[i - 1])) return std::nullopt;
    }
    const auto whitespace = [](std::string_view value) {
        return value.find_first_not_of(" \r\n\t") == std::string_view::npos;
    };
    if (!whitespace(output.substr(0, positions[0]))) return std::nullopt;
    const auto tail = output.substr(positions[3] + tags[3].size());
    constexpr std::string_view ok = ":0:0:0";
    if (!tail.starts_with(ok) || !whitespace(tail.substr(ok.size()))) return std::nullopt;
    const auto segment = [&](std::size_t i) -> std::optional<std::string_view> {
        auto begin = positions[i] + tags[i].size();
        if (begin < output.size() && output[begin] == '\r') ++begin;
        if (begin >= output.size() || output[begin] != '\n') return std::nullopt;
        ++begin;
        auto end = positions[i + 1];
        if (!end || output[end - 1] != '\n') return std::nullopt;
        --end;
        if (end > begin && output[end - 1] == '\r') --end;
        if (end < begin) return std::nullopt;
        auto value = output.substr(begin, end - begin);
        if (value.empty() || value.size() > 2ULL * 1024 * 1024) return std::nullopt;
        return value;
    };
    const auto before = segment(0), viewport = segment(1), after = segment(2);
    if (!before || !viewport || !after) return std::nullopt;
    return ContextDumpViews{*before, *viewport, *after};
}
} // namespace wvd::devices::android
