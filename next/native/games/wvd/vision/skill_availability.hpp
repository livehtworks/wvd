#pragma once
#include <array>
#include <opencv2/imgproc.hpp>

namespace wvd::games::vision {
struct SkillAvailability {
    bool disabled{};
    std::array<int, 4> bright_pixels{}, text_edges{};
    std::array<int, 4> command_bright_pixels{};
};
// Only menu label interiors: exclude borders, skill symbols and detail icons.
// Compare skill lettering with the command row, not another skill: resource
// shortage or a status effect can disable all four skills at once.
inline SkillAvailability measure_skill_availability(const cv::Mat &bgr, int slot) {
    SkillAvailability result;
    if (bgr.size() != cv::Size(900, 1600) || bgr.type() != CV_8UC3 || slot < 0 || slot > 3)
        return result;
    for (int i = 0; i < 4; ++i) {
        cv::Mat gray, edges;
        cv::cvtColor(bgr(cv::Rect(i % 2 ? 520 : 145, i / 2 ? 1040 : 950, 230, 40)), gray, cv::COLOR_BGR2GRAY);
        result.bright_pixels[i] = cv::countNonZero(gray > 170);
        cv::Canny(gray, edges, 40, 80);
        result.text_edges[i] = cv::countNonZero(edges);
    }
    int visible_commands = 0;
    constexpr std::array<int, 4> command_x{195, 330, 465, 740};
    for (int i = 0; i < 4; ++i) {
        cv::Mat gray;
        cv::cvtColor(bgr(cv::Rect(command_x[i], 1170, 90, 40)), gray, cv::COLOR_BGR2GRAY);
        result.command_bright_pixels[i] = cv::countNonZero(gray > 170);
        visible_commands += result.command_bright_pixels[i] >= 60;
    }
    // Blank labels and whole-screen dimming are unknown, not disabled proof.
    result.disabled = visible_commands >= 2 && result.bright_pixels[slot] <= 5 && result.text_edges[slot] >= 150;
    return result;
}
}
