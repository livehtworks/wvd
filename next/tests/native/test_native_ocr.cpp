#include "recognition/ocr.hpp"
#include <opencv2/imgproc.hpp>
#include <iostream>

int main(int argc, char **argv) {
    try {
        if (argc != 2) throw std::runtime_error("OCR_MODEL_PATH_REQUIRED");
        wvd::recognition::OcrEngine engine{std::filesystem::path(argv[1])};
        cv::Mat image(180, 600, CV_8UC3, cv::Scalar(255, 255, 255));
        cv::putText(image, "NEXT", {45, 125}, cv::FONT_HERSHEY_SIMPLEX,
                    2.8, cv::Scalar(0, 0, 0), 5, cv::LINE_AA);
        const auto matches = engine.recognize(image);
        bool found{};
        for (const auto &match : matches) {
            std::cout << match.text << " score=" << match.score << '\n';
            if (match.text.find("NEXT") != std::string::npos) found = true;
        }
        if (!found) throw std::runtime_error("OCR_EXPECTED_TEXT_MISSING");
        engine.cancel();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
