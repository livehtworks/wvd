#include "bobber.hpp"
#include <algorithm>
#include <opencv2/imgproc.hpp>

namespace wvd::games::vision {
nlohmann::json detect_bobber(const cv::Mat &image, const cv::Mat &template_image) {
    using J = nlohmann::json;
    cv::Mat templ, field, gxt, gyt, magnitude;
    cv::cvtColor(template_image, templ, cv::COLOR_BGR2GRAY);
    templ.convertTo(field, CV_32F, 1.0 / 255);
    cv::Sobel(field, gxt, CV_32F, 1, 0, 3);
    cv::Sobel(field, gyt, CV_32F, 0, 1, 3);
    cv::magnitude(gxt, gyt, magnitude);
    magnitude += 1e-6;
    cv::divide(gxt, magnitude, gxt);
    cv::divide(gyt, magnitude, gyt);
    int side = std::min(templ.cols, templ.rows);
    cv::Mat scaled(image.size(), CV_8U);
    // 原 Python clip 后 astype(uint8) 是截断，不使用会四舍五入的 convertTo。
    for (int y = 0; y < image.rows; ++y)
        for (int x = 0; x < image.cols; ++x)
            scaled.at<std::uint8_t>(y, x) = static_cast<std::uint8_t>(std::clamp(
                (image.at<cv::Vec3b>(y, x)[2] - 14.0f) * (255.0f / 86.0f), 0.0f, 255.0f));
    cv::Mat blurred, gx, gy, rx, ry, response;
    cv::GaussianBlur(scaled, blurred, {5, 5}, 0);
    cv::Sobel(blurred, gx, CV_32F, 1, 0, 3);
    cv::Sobel(blurred, gy, CV_32F, 0, 1, 3);
    cv::filter2D(gx, rx, -1, gxt, {-1, -1}, 0, cv::BORDER_CONSTANT);
    cv::filter2D(gy, ry, -1, gyt, {-1, -1}, 0, cv::BORDER_CONSTANT);
    response = rx + ry;
    double maximum{};
    cv::minMaxLoc(response, nullptr, &maximum);
    J results = J::array();
    if (maximum > 0) {
        response /= maximum;
        cv::Mat binary = response >= 0.5;
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        struct Detection {
            cv::Rect box;
            cv::Point center;
            double score;
        };
        std::vector<Detection> candidates, kept;
        for (const auto &contour : contours)
            if (cv::contourArea(contour) >= 10) {
                auto box = cv::boundingRect(contour);
                double score;
                cv::minMaxLoc(response(box), nullptr, &score);
                candidates.push_back({box, {box.x + box.width / 2, box.y + box.height / 2}, score});
            }
        std::stable_sort(candidates.begin(), candidates.end(),
                         [](const auto &a, const auto &b) { return a.score > b.score; });
        for (auto candidate : candidates)
            if (std::none_of(kept.begin(), kept.end(), [&](const auto &p) {
                    auto d = p.center - candidate.center;
                    return d.dot(d) < 0.25 * side * side;
                }))
                kept.push_back(candidate);
        for (const auto &candidate : kept) {
            auto c = candidate.center;
            int x1 = std::max(c.x - side / 2, 0), y1 = std::max(c.y - side / 2, 0),
                x2 = std::min(c.x + side / 2, image.cols - 1),
                y2 = std::min(c.y + side / 2, image.rows - 1);
            if (x2 <= x1 || y2 <= y1)
                continue;
            auto roi = scaled(cv::Rect(x1, y1, x2 - x1, y2 - y1));
            cv::Mat resized, score;
            cv::resize(templ, resized, roi.size());
            cv::matchTemplate(roi, resized, score, cv::TM_CCOEFF_NORMED);
            double matched = (score.at<float>(0, 0) + 1.0) / 2;
            if (candidate.score >= 0.9 && matched >= 0.8)
                results.push_back({{"center", {c.x, c.y}},
                                   {"box",
                                    {candidate.box.x, candidate.box.y, candidate.box.width,
                                     candidate.box.height}},
                                   {"score", candidate.score},
                                   {"match_score", matched}});
        }
    }
    return {{"schema", 1},
            {"outcome", results.empty() ? "NoHit" : "Hit"},
            {"box", results.empty() ? J(nullptr) : results[0]["box"]},
            {"target", true},
            {"evidence", {{"detections", results}}}};
}
} // namespace wvd::games::vision
