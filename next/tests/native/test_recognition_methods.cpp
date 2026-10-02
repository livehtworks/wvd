#include "authoring/semantic_assets.hpp"
#include "games/wvd/tasks/author_workflow.hpp"
#include "games/wvd/tasks/native_publisher.hpp"
#include "games/wvd/vision/native_recognizers.hpp"
#include "platform/windows/file_digest.hpp"
#include "recognition/service.hpp"
#include "workflow/serialization.hpp"
#include <opencv2/imgcodecs.hpp>
#include <fstream>
#include <iostream>

using namespace wvd;
using J = nlohmann::json;
void check(bool ok, const std::string &why) { if (!ok) throw std::runtime_error(why); }
J read(const std::filesystem::path &path) { std::ifstream in(path); return J::parse(in); }
template<class F> void rejects(F action, const std::string &code) {
    try { action(); } catch (const std::exception &e) {
        check(std::string(e.what()).find(code) != std::string::npos, e.what()); return;
    }
    throw std::runtime_error("EXPECTED_REJECTION:" + code);
}
J document(const J &recipe) {
    return {{"schema", 1}, {"flow", {{"id", "ocr-check"}, {"name", "OCR"}, {"description", ""}}},
        {"entry", "Read"}, {"execution", {{"time_limit_ms", 30000}}},
        {"nodes", J::array({{{"id", "Read"}, {"type", "recognition"}, {"name", "Read"},
            {"parameters", {{"condition", recipe}}}},
            {{"id", "Done"}, {"type", "end"}, {"name", "Done"}, {"parameters", {{"outcome", "success"}}}}})},
        {"edges", J::array({{{"id", "done"}, {"from", "Read"}, {"to", "Done"}, {"outcome", "success"}, {"order", 0}}})},
        {"layout", {{"nodes", J::array({{{"node_id", "Read"}, {"x", 0}, {"y", 0}},
            {{"node_id", "Done"}, {"x", 200}, {"y", 0}}})}, {"viewport", {{"x", 0}, {"y", 0}, {"zoom", 1}}}}}};
}
int main(int argc, char **argv) {
    try {
        check(argc == 2, "PASS_RECORDED_FRAME_DIRECTORY");
        const std::filesystem::path frames = argv[1];
        const auto catalogue = read("resources/authoring/semantic-assets.json");
        authoring::SemanticAssets assets(catalogue);
        const J ref{{"mode", "semantic"}, {"id", "outskirts.fortress.zone10"}, {"method", "ocr"}};
        const auto recipe = assets.resolve(ref, "zh-Hant", authoring::ResourceUse::Position);
        check(recipe.at("language") == "zh-Hant" && recipe.at("unique") == true, "SEMANTIC_OCR");
        check(assets.condition("outskirts.fortress.zone10", "zh-Hant").at("mode") == "template", "DEFAULT_CHANGED");
        rejects([&] { assets.resolve(ref, "en"); }, "SEMANTIC_METHOD_UNAVAILABLE");
        auto unknown = ref; unknown["method"] = "unregistered";
        rejects([&] { assets.resolve(unknown, "zh-Hant"); }, "SEMANTIC_METHOD_UNAVAILABLE");
        auto unsafe = recipe; unsafe["unique"] = false;
        rejects([&] { assets.resolve(unsafe, "zh-Hant", authoring::ResourceUse::Position); }, "OCR_POSITION_REQUIRES_EXACT_UNIQUE");
        auto alternate_default = catalogue;
        alternate_default["resources"]["outskirts.fortress.zone10"]["variants"]["zh-Hant"]["condition"] = recipe;
        check(authoring::SemanticAssets(alternate_default).condition("outskirts.fortress.zone10", "zh-Hant") == recipe,
              "OCR_DEFAULT_NOT_SUPPORTED");
        const auto compiled = games::tasks::compile_author_workflow(assets.lower(document(ref), "zh-Hant"));
        check(compiled.workflow.nodes.at("Author_Read").at("observation_args") == recipe, "AUTHOR_DROPPED_PARAMETERS");
        auto click_document = document(recipe);
        click_document["nodes"][0]["type"] = "action";
        click_document["nodes"][0]["parameters"] = {{"operation", "click"}, {"scene", recipe},
            {"target", recipe}, {"postcondition", recipe}};
        const auto click = games::tasks::compile_author_workflow(click_document);
        const auto &operation = click.workflow.nodes.at("Author_Read").at("operation_args");
        check(operation.at("target_recognition").at("parameters") == recipe && operation.at("use_target_center") == true,
              "OCR_CLICK_CENTER_NOT_CONNECTED");
        click_document["nodes"][0]["parameters"]["target"] = unsafe;
        rejects([&] { games::tasks::compile_author_workflow(click_document); }, "OCR_POSITION_REQUIRES_EXACT_UNIQUE");

        const auto root = std::filesystem::absolute(".local") /
            ("recognition-methods-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        recognition::Bundle bundle{root / "bundle", "methods-check", {}};
        const auto models = read("resources/recognition/ocr-models.json").at("models");
        for (const auto &[language, model] : models.items()) {
            (void)language;
            for (const auto &[name, member] : model.at("files").items()) {
                const auto relative = model.at("bundle_directory").get<std::string>() + "/" + name;
                const auto target = bundle.root / relative;
                std::filesystem::create_directories(target.parent_path());
                std::filesystem::copy_file(member.at("source").get<std::string>(), target);
                const auto hash = platform::file_sha256(target);
                check(hash == member.at("sha256").get<std::string>(), "MODEL_HASH");
                bundle.files.push_back({relative, hash});
            }
        }
        const auto published = games::tasks::publish_native(compiled.workflow, bundle, root / "publication", J::object(), J::object());
        const auto serialized = read(root / "publication/program/flow.json").dump();
        check(serialized.find("zh-Hant") != std::string::npos && serialized.find("exact") != std::string::npos, "PUBLISH_DROPPED_PARAMETERS");
        auto missing = bundle;
        missing.files.erase(std::remove_if(missing.files.begin(), missing.files.end(), [](const auto &file) {
            return file.relative_path == "model/ocr/zh-Hant/rec.onnx";
        }), missing.files.end());
        rejects([&] { games::tasks::publish_native(compiled.workflow, missing, root / "rejected", J::object(), J::object()); },
                "NATIVE_OCR_MODEL_MISSING_OR_UNLOCKED");
        recognition::Service service(bundle, games::vision::native_handlers(J::object(), "zh-Hant"));
        contracts::FrameEnvelope frame;
        frame.identity.device_id = "recorded"; frame.identity.game_id = "wvd";
        frame.identity.pack_revision = bundle.revision; frame.identity.viewport_id = "900x1600";
        frame.identity.generation = 1; frame.identity.raw_size = frame.identity.recognition_size = {900, 1600};
        const auto set_frame = [&](const cv::Mat &image) {
            check(!image.empty() && image.cols == 900 && image.rows == 1600, "FRAME_SIZE");
            ++frame.identity.frame_id;
            frame.identity.captured_at = frame.identity.capture_finished_at = std::chrono::steady_clock::now();
            frame.raw_bgr = std::make_shared<const std::vector<std::uint8_t>>(image.data, image.data + image.total() * image.elemSize());
        };
        J evidence = J::array();
        const auto observe = [&](const J &condition, bool custom = true) {
            auto definition = condition;
            definition.update({{"id", "recorded.methods"}, {"revision", "1"}, {"type", "ocr"}});
            auto request = recognition::parse_request(definition);
            if (custom) request = {"recorded.methods", "1", {0, 0, 900, 1600},
                recognition::CustomParameters{"WvdVision", condition}};
            auto result = service.evaluate(frame, frame.identity, request);
            check(result.outcome != contracts::RecognitionOutcome::Error, result.error_code);
            evidence.push_back({{"frame_id", frame.identity.frame_id}, {"condition", condition},
                {"hit", result.outcome == contracts::RecognitionOutcome::Hit}, {"evidence", result.evidence}});
            return result;
        };
        for (const auto file : {"frame-000055.png", "frame-000054.png"}) {
            set_frame(cv::imread((frames / file).string()));
            int index = 0;
            for (const int zone : {1, 3, 4, 5, 6, 7, 9, 10}) {
                auto target = ref; target["id"] = "outskirts.fortress.zone" + std::to_string(zone);
                const auto condition = assets.resolve(target, "zh-Hant", authoring::ResourceUse::Position);
                const auto result = observe(condition);
                check(result.outcome == contracts::RecognitionOutcome::Hit && result.center.has_value(), "ROW_NOT_HIT:" + std::to_string(zone));
                check(result.center->x >= 400 && result.center->x < 880 &&
                    result.center->y >= 225 + index * 90 && result.center->y < 280 + index * 90, "WRONG_ROW_CENTER");
                check(result.evidence.at("evidence").at("cache_hit") == (index != 0), "SAME_FRAME_CACHE");
                ++index;
            }
        }
        set_frame(cv::imread((frames / "frame-000046.png").string()));
        auto entry = ref; entry["id"] = "outskirts.fortress";
        check(observe(assets.resolve(entry, "zh-Hant")).center.has_value(), "ENTRY_MISSING");
        set_frame(cv::imread((frames / "frame-000053.png").string()));
        check(observe(recipe).outcome == contracts::RecognitionOutcome::NoHit, "FADE_FALSE_HIT");
        // 明确标注的合成重复行：验证歧义不生成点击坐标，不冒充实机页面。
        auto duplicate = cv::imread((frames / "frame-000055.png").string());
        duplicate(cv::Rect(400, 850, 480, 100)).clone().copyTo(duplicate(cv::Rect(400, 220, 480, 100)));
        set_frame(duplicate);
        const auto ambiguous = observe(recipe, false);
        check(ambiguous.matches.size() == 2 && !ambiguous.center && ambiguous.outcome == contracts::RecognitionOutcome::NoHit,
              "AMBIGUOUS_CLICK_TARGET");
        service.cancel();
        const auto cancelled = service.evaluate(frame, frame.identity,
            {"cancelled", "1", {0, 0, 900, 1600}, recognition::parse_ocr_parameters(recipe)});
        check(cancelled.outcome == contracts::RecognitionOutcome::Error && cancelled.error_code == "RECOGNITION_CANCELLED", "CANCEL_IGNORED");
        std::ofstream(root / "results.json") << evidence.dump(2);
        std::cout << "PASS: method selection, author compilation/publication, model lock, recorded 17 targets, fade/ambiguity, frame cache, cancellation.\n"
                  << "Evidence: " << root.string() << "\nNo device connected or clicked.\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
