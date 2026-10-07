#include "recognition/ocr.hpp"
#include "platform/windows/bundle_lease.hpp"
#include "platform/windows/file_digest.hpp"
#include <opencv2/imgproc.hpp>
#include <iostream>

int main(int argc, char **argv) {
    try {
        if (argc != 2 && argc != 3) throw std::runtime_error("OCR_MODEL_PATH_REQUIRED");
        const bool memory_cycles = argc == 3 && std::string(argv[2]) == "--memory-cycles";
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
