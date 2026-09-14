#include "maafw/buffers.hpp"
#include "maafw/recognition.hpp"
#include <MaaFramework/MaaAPI.h>
#include <fstream>
#include <iostream>
#include <json.hpp>

using J = nlohmann::json;
using namespace wvd;

// 驱动只负责把可丢弃样本交给真实库；没有假的识别结果、Controller 或游戏入口。
int main(int argc, char **argv) {
    try {
        if (argc != 3)
            throw std::runtime_error("expected case and result files");
        std::ifstream input(maafw::path_from_utf8(argv[1]));
        J cfg;
        input >> cfg;
        MaaLoggingLevel level = MaaLoggingLevel_Off;
        MaaGlobalSetOption(MaaGlobalOption_StdoutLevel, &level, sizeof(level));
        maafw::Bundle bundle{
            maafw::path_from_utf8(cfg.at("bundle").get<std::string>()), "test-revision", {}};
        for (const auto &item : cfg.at("files"))
            bundle.files.push_back({item.at("path"), item.at("sha256")});
        maafw::OfflineRecognizer recognizer(std::move(bundle));
        std::ifstream image(maafw::path_from_utf8(cfg.at("frame").get<std::string>()),
                            std::ios::binary);
        contracts::FrameEnvelope frame;
        frame.encoded_image.assign(std::istreambuf_iterator<char>(image), {});
        frame.identity = {"offline-fixture",
                          "offline",
                          "test-revision",
                          "portrait",
                          1,
                          1,
                          0,
                          {900, 1600},
                          {900, 1600},
                          std::chrono::steady_clock::now(),
                          "BGR8"};
        auto current = frame.identity;
        const auto mismatch = cfg.value("mismatch", std::string{});
        if (mismatch == "generation")
            ++current.generation;
        if (mismatch == "frame")
            ++current.frame_id;
        if (mismatch == "epoch")
            ++current.action_epoch;
        if (mismatch == "device")
            current.device_id = "another-fixture";
        if (mismatch == "pack")
            current.pack_revision = "another-revision";
        if (mismatch == "viewport")
            current.viewport_id = "landscape";
        if (mismatch == "color")
            current.color_format = "RGB8";
        if (mismatch == "size")
            frame.identity.recognition_size = current.recognition_size = {100, 100};
        if (mismatch == "decoded_size")
            frame.identity.recognition_size = current.recognition_size = {450, 800};
        if (mismatch == "future")
            frame.identity.captured_at = current.captured_at =
                std::chrono::steady_clock::now() + std::chrono::hours(1);
        auto roi = cfg.value("roi", std::vector<int>{0, 0, 900, 1600});
        if (roi.size() != 4)
            throw std::runtime_error("test ROI shape");
        maafw::RecognitionRequest request{
            "fixture", "parameters-1", {roi[0], roi[1], roi[2], roi[3]}, {}};
        if (cfg.value("ocr", false))
            request.parameters =
                maafw::OcrParameters{cfg.at("expected").get<std::vector<std::string>>()};
        else
            request.parameters = maafw::TemplateParameters{cfg.value("template", "target.png"),
                                                           cfg.value("threshold", 0.8)};
        if (cfg.contains("mutate_after_load")) {
            // 新契约先阻止活动文件写入；作者目录可编辑，但不再是运行时加载源。
            auto active = maafw::path_from_utf8(recognizer.bundle_status().at("root"));
            std::ofstream change(active / "image/target.png", std::ios::binary);
            change << "mutated";
            if (change)
                throw std::runtime_error("ACTIVE_BUNDLE_WRITE_WAS_ALLOWED");
        }
        J results = J::array();
        for (int i = 0; i < cfg.value("repeat", 1); ++i) {
            auto result = recognizer.evaluate(frame, current, request);
            const char *outcome = result.outcome == contracts::RecognitionOutcome::Hit ? "Hit"
                                  : result.outcome == contracts::RecognitionOutcome::NoHit
                                      ? "NoHit"
                                      : "Error";
            J row{{"outcome", outcome},
                  {"error", result.error_code},
                  {"stage", result.error_stage},
                  {"task_id", result.engine_task_id},
                  {"reco_id", result.engine_reco_id},
                  {"engine_status", result.engine_status},
                  {"matches", result.matches.size()},
                  {"frame_id", result.basis.frame_id},
                  {"generation", result.basis.generation}};
            row["box"] =
                result.box ? J{result.box->x, result.box->y, result.box->width, result.box->height}
                           : J(nullptr);
            row["center"] = result.center ? J{result.center->x, result.center->y} : J(nullptr);
            if (!result.matches.empty()) {
                row["score"] = result.matches.front().score;
                row["text"] = result.matches.front().text;
            }
            results.push_back(row);
        }
        std::ofstream output(maafw::path_from_utf8(argv[2]), std::ios::binary);
        output << J{{"sdk_version", MaaVersion()}, {"results", results}}.dump() << '\n';
        output.close();
        if (!output)
            throw std::runtime_error("result write failed");
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
