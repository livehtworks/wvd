#include "recognition/service.hpp"
#include "platform/windows/file_digest.hpp"
#include <opencv2/imgcodecs.hpp>
#include <iostream>
#include <cstring>
#include <windows.h>

int main(int argc, char **argv) {
    namespace fs = std::filesystem;
    fs::path directory;
    try {
        if (argc != 2) throw std::runtime_error("SAMPLE_PATH_REQUIRED");
        const fs::path sample(std::u8string(
            reinterpret_cast<const char8_t *>(argv[1]),
            reinterpret_cast<const char8_t *>(argv[1] + std::strlen(argv[1]))));
        const auto image = cv::imread(sample.string(), cv::IMREAD_COLOR);
        if (image.empty() || !image.isContinuous() || image.cols < 4 || image.rows < 4)
            throw std::runtime_error("SAMPLE_IMAGE_INVALID");
        directory = fs::temp_directory_path() /
            ("wvd-native-recognition-" + std::to_string(GetCurrentProcessId()));
        if (fs::exists(directory)) throw std::runtime_error("SAMPLE_DIRECTORY_EXISTS");
        fs::create_directory(directory);
        fs::create_directory(directory / "image");
        fs::copy_file(sample, directory / "image" / "target.png");
        const auto hash = wvd::platform::file_sha256(directory / "image" / "target.png");
        {
            wvd::recognition::Bundle bundle;
            bundle.root = directory;
            bundle.revision = "offline-sample";
            bundle.files.push_back({"image/target.png", hash});
            wvd::recognition::Service service(std::move(bundle), {});
            wvd::contracts::FrameEnvelope frame;
            frame.identity.device_id = "offline";
            frame.identity.game_id = "wvd";
            frame.identity.pack_revision = "offline-sample";
            frame.identity.viewport_id = "sample";
            frame.identity.generation = 1;
            frame.identity.frame_id = 1;
            frame.identity.raw_size = {image.cols, image.rows};
            frame.identity.recognition_size = frame.identity.raw_size;
            frame.identity.captured_at = std::chrono::steady_clock::now();
            frame.identity.capture_finished_at = frame.identity.captured_at;
            frame.raw_bgr = std::make_shared<const std::vector<std::uint8_t>>(
                image.data, image.data + image.total() * image.elemSize());
            wvd::recognition::Request request{"template", "1",
                {0, 0, image.cols, image.rows},
                wvd::recognition::TemplateParameters{"target.png", 0.99}};
            const auto hit = service.evaluate(frame, frame.identity, request);
            if (hit.outcome != wvd::contracts::RecognitionOutcome::Hit ||
                !hit.center || hit.center->x != image.cols / 2 ||
                hit.center->y != image.rows / 2)
                throw std::runtime_error("TEMPLATE_POSITIVE_FAILED:" + hit.error_code);
            request.roi.width = 1;
            const auto invalid = service.evaluate(frame, frame.identity, request);
            if (invalid.outcome != wvd::contracts::RecognitionOutcome::Error ||
                invalid.error_code != "TEMPLATE_EXCEEDS_ROI")
                throw std::runtime_error("TEMPLATE_ERROR_NOT_DISTINCT");
        }
        fs::remove(directory / "image" / "target.png");
        fs::remove(directory / "image");
        fs::remove(directory);
        std::cout << "native OpenCV hit and error distinct\n";
    } catch (const std::exception &error) {
        if (!directory.empty()) {
            std::error_code ignored;
            fs::remove(directory / "image" / "target.png", ignored);
            fs::remove(directory / "image", ignored);
            fs::remove(directory, ignored);
        }
        std::cerr << error.what() << '\n';
        return 1;
    }
}
