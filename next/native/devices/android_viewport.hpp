#pragma once
#include "contracts/recognition.hpp"
#include <optional>
#include <regex>
#include <sstream>
#include <string>

namespace wvd::devices::android {
struct InputViewport {
    contracts::Size size;
    int rotation{};
    bool operator==(const InputViewport &) const = default;
};
// 解析 Android InputReader 的默认显示器 viewport，不假定手机天然方向。
// 多条相同记录可合并；不同 displayId、冲突、非零原点或未识别格式不能任选一条。
inline std::optional<InputViewport> input_viewport(const std::string &dump) {
    if (dump.size() > 2 * 1024 * 1024) return {};
    static const std::regex display(R"(\bdisplayId\s*=\s*0(?:\s|,|\}))");
    static const std::regex orientation(R"(\borientation\s*=\s*([0-3])(?:\s|,|\}))");
    static const std::regex frame(R"(\blogicalFrame\s*=\s*\[\s*([0-9]{1,5})\s*,\s*([0-9]{1,5})\s*,\s*([0-9]{1,5})\s*,\s*([0-9]{1,5})\s*\])");
    std::istringstream lines(dump);
    std::string line;
    std::optional<InputViewport> found;
    while (std::getline(lines, line)) {
        if (line.find("Viewport") == std::string::npos || !std::regex_search(line, display)) continue;
        std::smatch o, f;
        if (!std::regex_search(line, o, orientation) || !std::regex_search(line, f, frame)) return {};
        const int x=std::stoi(f[1]), y=std::stoi(f[2]), right=std::stoi(f[3]), bottom=std::stoi(f[4]);
        if (x != 0 || y != 0 || right < 1 || bottom < 1 || right > 16384 || bottom > 16384) return {};
        const InputViewport value{{right,bottom},std::stoi(o[1])};
        if (found && *found != value) return {};
        found=value;
    }
    return found;
}
} // namespace wvd::devices::android
