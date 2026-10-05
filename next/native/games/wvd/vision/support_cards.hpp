#pragma once
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <array>
#include <vector>

namespace wvd::games::vision {
struct SupportCards {
    std::vector<cv::Rect> candidates;
    std::vector<cv::Rect> cards; // 唯一的两行三列，先上排再下排。
};

// 只检测卡片外框，不读取姓名、HP/MP、等级或颜色。输入是已确认详情窗口内的区域。
inline SupportCards detect_support_cards(const cv::Mat &bgr, cv::Rect area) {
    SupportCards result;
    cv::Mat gray, edges;
    cv::cvtColor(bgr(area), gray, cv::COLOR_BGR2GRAY);
    cv::Canny(gray, edges, 20, 60);
    // 补合少量锯齿/断边；不通过大核把文字或相邻卡片连成矩形。
    cv::morphologyEx(edges, edges, cv::MORPH_CLOSE,
                    cv::getStructuringElement(cv::MORPH_RECT, {3, 3}));
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(edges, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);
    for (const auto &contour : contours) {
        auto r = cv::boundingRect(contour);
        if (r.width < bgr.cols * .22 || r.width > bgr.cols * .36 ||
            r.height < bgr.cols * .11 || r.height > bgr.cols * .24 ||
            r.width < r.height * 1.25 || r.width > r.height * 2.5 ||
            cv::contourArea(contour) < r.area() * .82) continue;
        r.x += area.x; r.y += area.y;
        // Canny内外双边属于同一张卡，不能计作两张。
        if (std::any_of(result.candidates.begin(), result.candidates.end(), [&](cv::Rect other) {
            return double((r & other).area()) / (r | other).area() > .8;
        })) continue;
        result.candidates.push_back(r);
    }
    // 噪声过多不穷举，也不放宽判定；调用者记录为待确认并等待新帧。
    if (result.candidates.size() < 5 || result.candidates.size() > 24) return result;
    const auto border_supported = [&](cv::Rect r) {
        r.x -= area.x; r.y -= area.y;
        // Buff icons can join a card contour. Require visible edges at the missing grid cell,
        // not merely five neighbours from which an imaginary sixth card could be inferred.
        const auto coverage = [&](cv::Rect strip, bool horizontal) {
            if ((strip & cv::Rect(0, 0, edges.cols, edges.rows)) != strip) return 0.0;
            cv::Mat projection;
            cv::reduce(edges(strip), projection, horizontal ? 0 : 1, cv::REDUCE_MAX);
            return double(cv::countNonZero(projection)) / (horizontal ? strip.width : strip.height);
        };
        return coverage({r.x + 8, r.y - 5, r.width - 16, 11}, true) > .65 &&
               coverage({r.x + 8, r.br().y - 6, r.width - 16, 11}, true) > .65 &&
               coverage({r.x - 5, r.y + 8, 11, r.height - 16}, false) > .65 &&
               coverage({r.br().x - 6, r.y + 8, 11, r.height - 16}, false) > .65;
    };
    const auto similar = [](cv::Rect a, cv::Rect b) {
        return std::abs(a.width - b.width) <= a.width * .12 &&
               std::abs(a.height - b.height) <= a.height * .15;
    };
    for (const auto a : result.candidates) for (const auto b : result.candidates) for (const int span : {1, 2}) {
        const int pitch = (b.x - a.x) / span;
        if (!similar(a, b) || std::abs(a.y - b.y) > a.height * .1 ||
            pitch < a.width * 1.01 || pitch > a.width * 1.35) continue;
        for (const auto lower : result.candidates) {
            const int row_pitch = lower.y - a.y;
            if (!similar(a, lower) || std::abs(lower.x - a.x) > a.width * .06 ||
                row_pitch < a.height * 1.02 || row_pitch > a.height * 1.5) continue;
            // The upper-left contour can include buff icons. Any visible column
            // may anchor the grid; every inferred cell still needs four real borders.
            for (int anchor_col = 0; anchor_col < 3; ++anchor_col) {
                const int origin_x = a.x - anchor_col * pitch;
                std::vector<cv::Rect> grid;
                for (int row = 0; row < 2; ++row) for (int col = 0; col < 3; ++col) {
                    const auto match = std::find_if(result.candidates.begin(), result.candidates.end(), [&](cv::Rect r) {
                        return similar(a, r) && std::abs(r.x - origin_x - col * pitch) <= a.width * .06 &&
                               std::abs(r.y - a.y - row * row_pitch) <= a.height * .1;
                    });
                    if (match != result.candidates.end()) grid.push_back(*match);
                    else {
                        const cv::Rect inferred(origin_x + col * pitch, a.y + row * row_pitch, a.width, a.height);
                        if (border_supported(inferred)) grid.push_back(inferred);
                    }
                }
                if (grid.size() != 6) continue;
                const bool same_grid = result.cards.size() == grid.size() &&
                    std::equal(result.cards.begin(), result.cards.end(), grid.begin(), [](cv::Rect old, cv::Rect fresh) {
                        return double((old & fresh).area()) / (old | fresh).area() > .85;
                    });
                if (!result.cards.empty() && !same_grid) {
                    result.cards.clear();
                    return result; // 多套布局不能产生可点击点位。
                }
                if (result.cards.empty()) result.cards = std::move(grid);
            }
        }
    }
    return result;
}
} // namespace wvd::games::vision
