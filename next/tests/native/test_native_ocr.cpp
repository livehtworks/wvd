#include "recognition/ocr.hpp"
#include "platform/windows/bundle_lease.hpp"
#include "platform/windows/file_digest.hpp"
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <iostream>

int main(int argc, char **argv) {
    try {
        const bool real_frame = argc == 4 && std::string(argv[2]) == "--real-memory-frame";
        if (argc != 2 && argc != 3 && !real_frame) throw std::runtime_error("OCR_MODEL_PATH_REQUIRED");
        const bool memory_cycles = real_frame || (argc == 3 && std::string(argv[2]) == "--memory-cycles");
        if (argc == 3 && !memory_cycles) throw std::runtime_error("OCR_OPTION_UNKNOWN");
        const auto root = std::filesystem::path(argv[1]);
        wvd::platform::BundleLease::Manifest manifest;
        for (const auto &entry : std::filesystem::directory_iterator(root))
            if (entry.is_regular_file()) manifest.emplace(entry.path().filename().string(),
                wvd::platform::file_sha256(entry.path()));
        wvd::platform::BundleLease lease(root, "published-model-lock-check", manifest);
        for (int cycle = 0; cycle < (memory_cycles ? 4 : 1); ++cycle) {
        const auto before = wvd::platform::sample_memory();
        {
        wvd::recognition::OcrEngine engine{root};
        if (real_frame) {
            const auto image = cv::imread(argv[3]);
            if (image.size() != cv::Size(900, 1600)) throw std::runtime_error("REAL_FRAME_INVALID");
            // Reuse real pixels at the production skill/modal/card ROI sizes.
            for (const cv::Rect roi : {cv::Rect{0,600,900,1000}, {0,1150,900,450}, {0,930,450,180}}) {
                const auto matches = engine.recognize(image(roi));
                std::cout << "real_roi=" << roi << " matches=" << matches.size() << '\n';
                if (matches.empty()) throw std::runtime_error("REAL_OCR_MATCHES_MISSING");
            }
        } else {
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
        }
        engine.cancel();
        }
        if (memory_cycles) {
            const auto maintenance = wvd::platform::optimize_idle_heap();
            if (!before.process_ok || !maintenance.before.process_ok || !maintenance.after.process_ok ||
                !maintenance.succeeded)
                throw std::runtime_error("OCR_MEMORY_MEASUREMENT_FAILED");
            std::cout << "memory_cycle=" << cycle + 1 << " before=" << before.private_bytes
                      << " released=" << maintenance.before.private_bytes
                      << " optimized=" << maintenance.after.private_bytes
                      << " heap_available=" << maintenance.heap_after.available
                      << " heap_complete=" << maintenance.heap_after.complete
                      << " heap_allocated=" << maintenance.heap_after.allocated
                      << " heap_committed=" << maintenance.heap_after.committed
                      << " optimize_us=" << maintenance.elapsed_us << '\n';
        }
        }
        if (lease.storage_stats().model_bytes != 0)
            throw std::runtime_error("OCR_LOADER_RETAINED_SOURCE_MODEL_BYTES");
        lease.verify_members();
        std::cout << "Frozen published model paths inferred successfully; source model buffers remain zero\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
