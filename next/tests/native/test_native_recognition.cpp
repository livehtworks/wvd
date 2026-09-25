#include "recognition/service.hpp"
#include "platform/windows/file_digest.hpp"
#include <opencv2/imgcodecs.hpp>
#include <iostream>
#include <cstring>
#include <future>
#include <thread>
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
        {
            const auto bytes = image.total() * image.elemSize();
            auto cache = std::make_shared<wvd::recognition::DecodedAssetCache>(bytes + 1);
            std::atomic<int> decodes{0};
            std::vector<std::future<const std::uint8_t *>> readers;
            for (int i = 0; i < 4; ++i)
                readers.push_back(std::async(std::launch::async, [&] {
                    auto lease = cache->load("same-frozen-member", [&] {
                        ++decodes;
                        std::this_thread::sleep_for(std::chrono::milliseconds(20));
                        return image.clone();
                    });
                    return static_cast<const std::uint8_t *>(lease.mat().data);
                }));
            const auto first = readers.front().get();
            for (std::size_t i = 1; i < readers.size(); ++i)
                if (readers[i].get() != first)
                    throw std::runtime_error("CACHE_PIXEL_IDENTITY_DIVERGED");
            if (decodes != 1 || cache->stats().decode_count != 1 ||
                cache->stats().retained_bytes != bytes || cache->stats().in_use_bytes != 0)
                throw std::runtime_error("CACHE_COLD_LOAD_NOT_SHARED");
            { auto second = cache->load("other-member", [&] { return image.clone(); }); }
            if (cache->stats().retained_bytes > bytes + 1)
                throw std::runtime_error("CACHE_RETENTION_TARGET_IGNORED");
            { auto reloaded = cache->load("same-frozen-member", [&] { ++decodes; return image.clone(); }); }
            if (decodes != 2) throw std::runtime_error("CACHE_EVICT_RELOAD_MISSING");
            auto pinned_cache = std::make_shared<wvd::recognition::DecodedAssetCache>(bytes + 1);
            {
                auto held = pinned_cache->load("held", [&] { return image.clone(); });
                {
                    auto second = pinned_cache->load("second", [&] { return image.clone(); });
                    if (pinned_cache->stats().retained_bytes != bytes * 2 ||
                        pinned_cache->stats().in_use_bytes != bytes * 2)
                        throw std::runtime_error("CACHE_IN_USE_BYTES_UNDERCOUNTED");
                }
                if (pinned_cache->stats().retained_bytes > bytes + 1)
                    throw std::runtime_error("CACHE_RELEASE_DID_NOT_TRIM");
            }
        }
        {
            wvd::recognition::MatchBudget budget(1024);
            std::atomic<bool> cancelled{false};
            std::promise<void> started;
            auto owner = budget.acquire(800, cancelled);
            auto waiter = std::async(std::launch::async, [&] {
                started.set_value();
                try { auto unexpected = budget.acquire(800, cancelled); return false; }
                catch (const std::runtime_error &error) {
                    return std::string(error.what()) == "RECOGNITION_CANCELLED";
                }
            });
            started.get_future().wait();
            cancelled = true;
            budget.wake();
            if (!waiter.get()) throw std::runtime_error("BUDGET_CANCEL_NOT_HONORED");
            if (budget.stats().active_matches != 1)
                throw std::runtime_error("BUDGET_ACTIVE_COUNT_INVALID");
        }
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
