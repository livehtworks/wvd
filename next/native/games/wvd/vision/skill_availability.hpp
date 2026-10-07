#pragma once
#include <array>
#include <opencv2/imgproc.hpp>

namespace wvd::games::vision {
struct SkillAvailability {
    bool disabled{};
    std::array<int, 4> bright_pixels{}, text_edges{};
};
// Only menu label interiors: exclude borders, skill symbols and detail icons.
// A dark label alone is unknown. Require visible lettering and another bright
// label in the same menu, so whole-screen dimming cannot authorize a fallback.
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
    bool bright_peer = false;
    for (int i = 0; i < 4; ++i)
        if (i != slot && result.bright_pixels[i] >= 100 && result.text_edges[i] >= 150) bright_peer = true;
    result.disabled = bright_peer && result.bright_pixels[slot] <= 5 && result.text_edges[slot] >= 150;
    return result;
}
}
