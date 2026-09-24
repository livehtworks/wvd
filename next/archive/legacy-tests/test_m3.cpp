#include "games/wvd/vision/asset_resolver.hpp"
#include "games/wvd/vision/image_ops.hpp"
#include "games/wvd/vision/recognizers.hpp"
#include "runtime_fixture.hpp"
#include "storage/runtime_bundle.hpp"
#include <iostream>
#include <opencv2/imgcodecs.hpp>

using namespace fixture;
namespace fixture {
J m3_device_cases(const J &config);
}
int main(int argc, char **argv) {
    try {
        require(argc == 2, "M3_TEST_CONFIG_REQUIRED");
        J config;
        std::ifstream(maafw::path_from_utf8(argv[1])) >> config;
        if (config.contains("pixel_operations")) {
            const auto frame =
                cv::imdecode(bytes(maafw::path_from_utf8(config.at("frame"))), cv::IMREAD_COLOR);
            J result = J::array();
            for (const auto &operation : config["pixel_operations"]) {
                const auto image = games::vision::transform_rgb(
                    frame, operation.at("rgb").get<std::array<double, 3>>(),
                    operation.at("subtract"));
                J pixels = J::array();
                for (int y = 0; y < image.rows; ++y)
                    for (int x = 0; x < image.cols; ++x) {
                        const auto pixel = image.at<cv::Vec3b>(y, x);
                        pixels.push_back({pixel[0], pixel[1], pixel[2]});
                    }
                result.push_back(pixels);
            }
            std::ofstream(maafw::path_from_utf8(config.at("output"))) << result.dump();
            return 0;
        }
        if (config.value("device_checks", false)) {
            auto result = m3_device_cases(config);
            std::ofstream(maafw::path_from_utf8(config.at("output"))) << result.dump(2);
            return 0;
        }
        auto root = maafw::path_from_utf8(config.at("bundle"));
        maafw::Bundle bundle{root, config.at("revision"), {}};
        for (const auto &value : config.at("files"))
            bundle.files.push_back({value.at("path"), value.at("sha256")});
        if (config.contains("mod_fixture")) {
            const auto &mod_config = config["mod_fixture"];
            maafw::Bundle mod{maafw::path_from_utf8(mod_config.at("root")), "isolated-mod", {}};
            for (const auto &file : mod_config.at("files"))
                mod.files.push_back({file.at("path"), file.at("sha256")});
            maafw::RecognitionCache cache;
            J aliases{{"Alias.png", "fixture.png"}};
            auto active_base = storage::materialize_bundle(bundle);
            auto active_mod = storage::materialize_bundle(mod);
            games::vision::AssetResolver resolver(active_base, aliases, cache, &active_mod);
            auto base = resolver.load("fixture"), alias = resolver.load("Alias"),
                 fallback = resolver.load("modOnly");
            require(cv::norm(base, alias, cv::NORM_INF) == 0, "explicit alias mismatch");
            require(base.at<cv::Vec3b>(0, 0) != cv::Vec3b(7, 7, 7), "mod overrode baseline");
            require(fallback.at<cv::Vec3b>(0, 0) == cv::Vec3b(7, 7, 7), "mod fallback missing");
            games::vision::AssetResolver other(active_mod, J::object(), cache);
            require(other.load("fixture").at<cv::Vec3b>(0, 0) == cv::Vec3b(7, 7, 7),
                    "same-name bundle cache collision");
        }
        auto registry = std::make_shared<runtime::BehaviorRegistry>("m3-vision-1");
        games::vision::register_wvd(*registry);
        registry->seal();
        auto binding = games::vision::binding(config.value("aliases", J::object()));
        auto device = std::make_shared<OfflineDevice>();
        device->before = bytes(maafw::path_from_utf8(config.at("frame")));
        device->after = device->before;
        device->change_frame = false;
        contracts::InputPolicy policy{
            "m2-offline", "wvd", "fixture.app", bundle.revision, "portrait", {900, 1600}, {},
            {},           {},    2000ms};
        storage::EventJournal journal("m3-offline", 1);
        devices::InputGate gate(*device, policy, 1, 1, journal);
        J custom_events = J::array();
        maafw::GatewayHooks hooks;
        hooks.event = [&](const auto &name, const J &data) {
            if (name == "recognition.custom")
                custom_events.push_back(data);
        };
        maafw::MaaGateway gateway(bundle, &gate, hooks, {}, registry->bind_recognitions({binding}));
        auto init_begin = std::chrono::steady_clock::now();
        gateway.initialize();
        J output{{"sdk", MaaVersion()},
                 {"bundle", gateway.bundle_status()},
                 {"registry", registry->manifest()},
                 {"initialization_ms", std::chrono::duration<double, std::milli>(
                                           std::chrono::steady_clock::now() - init_begin)
                                           .count()},
                 {"cases", J::array()}};
        for (const auto &item : config.at("cases")) {
            auto capture_begin = std::chrono::steady_clock::now();
            auto frame = gateway.capture();
            auto capture_ms = std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() - capture_begin)
                                  .count();
            auto original = frame.encoded_image;
            maafw::RecognitionRequest request{
                item.at("id"),
                "1",
                {0, 0, 900, 1600},
                maafw::RecognitionRequest::CustomParameters{"WvdVision", item.at("parameters")}};
            auto begin = std::chrono::steady_clock::now();
            auto integrity_before = gateway.bundle_status();
            auto result = gateway.recognize(frame, gate.frame_identity(), request);
            auto integrity_after = gateway.bundle_status();
            auto outcome = result.outcome == contracts::RecognitionOutcome::Hit     ? "Hit"
                           : result.outcome == contracts::RecognitionOutcome::NoHit ? "NoHit"
                                                                                    : "Error";
            require(frame.encoded_image == original, "custom recognition modified source");
            output["cases"].push_back({{"id", item.at("id")},
                                       {"integrity_before", integrity_before},
                                       {"integrity_after", integrity_after},
                                       {"outcome", outcome},
                                       {"expected", item.at("expected")},
                                       {"pass", outcome == item.at("expected")},
                                       {"error", result.error_code},
                                       {"evidence", result.evidence},
                                       {"stages_ms", result.timing_ms},
                                       {"capture_decode_resize_ms", capture_ms},
                                       {"elapsed_ms", std::chrono::duration<double, std::milli>(
                                                          std::chrono::steady_clock::now() - begin)
                                                          .count()},
                                       {"task_id", result.engine_task_id},
                                       {"reco_id", result.engine_reco_id}});
        }
        if (config.value("pipeline", false)) {
            custom_events = J::array();
            auto id = gateway.post("VisionPipeline");
            while (gateway.running() || gateway.status(id) == MaaStatus_Pending ||
                   gateway.status(id) == MaaStatus_Running)
                std::this_thread::sleep_for(5ms);
            output["pipeline_status"] = gateway.status(id);
            output["pipeline_events"] = custom_events;
        }
        output["backend_inputs"] = device->calls.load();
        gate.close();
        gateway.close();
        output["bundle_after_close"] = gateway.bundle_status();
        gate.disconnect_backend();
        output["quiescent"] = true;
        std::ofstream(maafw::path_from_utf8(config.at("output"))) << output.dump(2);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
