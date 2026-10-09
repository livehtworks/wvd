#include "games/wvd/combat/turn.hpp"
#include "games/wvd/tasks/public_flow_library.hpp"
#include "games/wvd/tasks/public_step_scope.hpp"
#include "games/wvd/tasks/locale_assets.hpp"
#include "games/wvd/tasks/native_program.hpp"
#include "games/wvd/vision/native_recognizers.hpp"
#include "games/wvd/vision/native_asset_resolver.hpp"
#include "games/wvd/vision/support_cards.hpp"
#include "games/wvd/vision/skill_availability.hpp"
#include "recognition/service.hpp"
#include "storage/legacy_import.hpp"
#include "platform/windows/file_digest.hpp"
#include "platform/windows/runtime_files.hpp"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <fstream>
#include <iostream>
using namespace wvd;
using J = nlohmann::json;
using O = contracts::RecognitionOutcome;
void check(bool ok, const std::string &reason) { if (!ok) throw std::runtime_error(reason); }
J read(const char *file) { std::ifstream in(file); return J::parse(in); }
int main(int argc, char **argv) {
    try {
        if (argc == 3 && std::string(argv[1]) == "--harken-menu") {
            const auto manifest = read("packs/wvd/manifest.json");
            const auto root = std::filesystem::temp_directory_path() / ("wvd-harken-frame-" + platform::unique_id());
            recognition::Bundle bundle{root, manifest.at("revision"), {}};
            const auto copy = [&](const std::filesystem::path &source, const std::string &relative, const std::string &hash) {
                const auto target = root / relative;
                std::filesystem::create_directories(target.parent_path());
                std::filesystem::copy_file(source, target);
                check(platform::file_sha256(target) == hash, "HARKEN_RESOURCE_HASH");
                bundle.files.push_back({relative, hash});
            };
            for (const auto &row : manifest.at("files")) {
                const auto relative = row.at("path").get<std::string>();
                copy(std::filesystem::path("packs/wvd") / relative, relative, row.at("sha256"));
            }
            const auto models = read("resources/recognition/ocr-models.json");
            for (const auto &[locale, model] : models.at("models").items())
                for (const auto &[name, file] : model.at("files").items())
                    copy(file.at("source").get<std::string>(), model.at("bundle_directory").get<std::string>() + "/" + name,
                        file.at("sha256"));
            const auto pixels = cv::imread(argv[2]);
            check(pixels.cols == 900 && pixels.rows == 1600, "HARKEN_FRAME_INVALID");
            recognition::Service service(bundle, games::vision::native_handlers(manifest.value("aliases", J::object()), "zh-Hant"));
            contracts::FrameEnvelope frame;
            frame.identity.device_id = "recorded"; frame.identity.game_id = "wvd";
            frame.identity.pack_revision = bundle.revision; frame.identity.viewport_id = "900x1600";
            frame.identity.generation = frame.identity.frame_id = frame.identity.connection_generation = 1;
            frame.identity.raw_size = frame.identity.recognition_size = {900,1600};
            frame.identity.captured_at = frame.identity.capture_finished_at = std::chrono::steady_clock::now();
            frame.raw_bgr = std::make_shared<const std::vector<std::uint8_t>>(pixels.data, pixels.data + pixels.total() * pixels.elemSize());
            for (const char *mode : {"auto_route_moving", "auto_route_post"}) {
                const auto result = service.evaluate(frame, frame.identity, {mode, "1", {0,0,900,1600},
                    recognition::CustomParameters{"WvdVision", {{"mode",mode}, {"resource_locale","zh-Hant"}}}});
                check(result.outcome == (std::string(mode) == "auto_route_post" ? O::Hit : O::NoHit),
                    "REAL_HARKEN_HANDOFF:" + std::string(mode) + ":" + result.error_code);
            }
            std::cout << "real Harken floor menu: navigation post Hit, moving NoHit\n";
            return 0;
        }
        if (argc == 5 && std::string(argv[1]) == "--disabled-skills") {
            const auto root = std::filesystem::temp_directory_path() / ("wvd-skill-" + platform::unique_id());
            std::filesystem::create_directories(root);
            std::filesystem::copy_file(argv[2], root / "frame.png");
            recognition::Service service({root, "recorded-menu", {{"frame.png", platform::file_sha256(root / "frame.png")}}},
                games::vision::native_handlers(J::object(), "zh-Hant"));
            contracts::FrameEnvelope frame;
            frame.identity.device_id = "recorded"; frame.identity.game_id = "wvd";
            frame.identity.pack_revision = "recorded-menu"; frame.identity.viewport_id = "900x1600";
            frame.identity.generation = 1; frame.identity.raw_size = frame.identity.recognition_size = {900, 1600};
            for (int i = 2; i < 5; ++i) {
                const auto pixels = cv::imread(argv[i]);
                check(pixels.size() == cv::Size(900, 1600), "REAL_MENU_FRAME_INVALID");
                const auto disabled = games::vision::measure_skill_availability(pixels, 0);
                check(disabled.disabled, "REAL_DISABLED_SKILL_MISSED");
                check(!games::vision::measure_skill_availability(pixels, 1).disabled, "BRIGHT_PEER_SKILL_REJECTED");
                cv::Mat dim; pixels.convertTo(dim, -1, .35);
                check(!games::vision::measure_skill_availability(dim, 0).disabled, "DIM_PAGE_IS_NOT_DISABLED_PROOF");
                auto blank = pixels.clone(); blank(cv::Rect(145, 950, 230, 40)).setTo(cv::Scalar(65, 65, 65));
                check(!games::vision::measure_skill_availability(blank, 0).disabled, "BLANK_LABEL_IS_NOT_DISABLED_PROOF");
                ++frame.identity.frame_id;
                frame.identity.captured_at = frame.identity.capture_finished_at = std::chrono::steady_clock::now();
                frame.raw_bgr = std::make_shared<const std::vector<std::uint8_t>>(pixels.data, pixels.data + pixels.total() * pixels.elemSize());
                for (int slot = 0; slot < 2; ++slot) {
                    const auto result = service.evaluate(frame, frame.identity, {"disabled-skill", "1", {0, 0, 900, 1600},
                        recognition::CustomParameters{"WvdVision", {{"mode", "combat_skill_disabled"}, {"slot", slot}}}});
                    check(result.outcome == (slot == 0 ? O::Hit : O::NoHit), "REAL_SERVICE_AVAILABILITY:" + result.error_code);
                    check(!result.center.has_value(), "GRAY_OBSERVATION_GRANTED_CLICK_TARGET");
                }
                std::cout << J{{"file", argv[i]}, {"bright_pixels", disabled.bright_pixels},
                    {"text_edges", disabled.text_edges}, {"disabled", true}}.dump() << '\n';
            }
            return 0;
        }
        if (argc == 3 && std::string(argv[1]) == "--buff-frame") {
            auto pixels = cv::imread(argv[2]);
            check(pixels.cols == 900 && pixels.rows == 1600, "FRAME_INVALID");
            const cv::Rect area(0, 800, 900, 699);
            const auto cards = games::vision::detect_support_cards(pixels, area);
            for (const auto &r : cards.candidates) std::cout << "candidate " << r << '\n';
            check(cards.cards.size() == 6, "BUFF_JOINED_CARD_MISSING");
            cv::rectangle(pixels, cards.cards.front() + cv::Size(4, 4), cv::Scalar(20, 20, 20), cv::FILLED);
            check(games::vision::detect_support_cards(pixels, area).cards.empty(), "HIDDEN_CARD_INFERRED");
            std::cout << "PASS real buff-obscured contour and hidden-card exclusion\n";
            return 0;
        }
        const bool popup_frames = argc == 5 && std::string(argv[1]) == "--popup-frames";
        check(argc == 4 || popup_frames, "PASS_TWO_FRIEND_FRAMES_AND_ONE_ENEMY_FRAME");
        auto profile = storage::LegacyConfigImporter(read("packs/wvd/parameters/legacy-config-fields.json"))
            .parse({{"GENERAL", J::object()}}).values;
        J rows = J::array();
        for (const auto name : {"左上角色", "中上角色", "右上角色", "左下角色", "中下角色", "右下角色"})
            rows.push_back({{"role_var", "actor"}, {"skill_var", "左上技能"}, {"skill_lvl", 1},
                {"target_var", name}, {"freq_var", "重复"}});
        profile["STRATEGY"] = J::array({{{"group_name", "test"}, {"skill_settings", rows}}});
        profile["DEFAULT_OVERALL_STRATEGY"] = "test";
        const auto assets = read("resources/authoring/semantic-assets.json");
        J docs = J::object();
        for (const auto &doc : read("resources/authoring/public-flows.json")) docs[doc.at("flow").at("id").get<std::string>()] = doc;
        const games::tasks::PublicFlowLibrary library(docs, assets);
        const games::tasks::PublicStepScope steps([&](const std::string &id, const J &args) { return library.compile_step(id, args, "zh-Hant"); });
        auto flow = games::combat::take_turn(profile, {});
        games::tasks::localize_task_assets(flow, assets, "zh-Hant");
        games::tasks::compile_native_program(flow, J::object(), "support-check").validate();
        check(std::find(flow.images.begin(), flow.images.end(), "supportSkillCheck.png") == flow.images.end() && flow.nodes.dump().find("supportSkillCheck") == std::string::npos,
              "OLD_TEMPLATE_STILL_ACTIVE");
        for (int slot = 0; slot < 6; ++slot) {
            const auto &op = flow.nodes.at("Skill" + std::to_string(slot) + "Try0Support").at("operation_args");
            check(op.at("target_recognition").at("parameters").at("slot") == slot && op.at("use_target_center") == true,
                  "SUPPORT_CLICK_NOT_WIRED");
        }
        const auto manifest = read("packs/wvd/manifest.json");
        const auto aliases = manifest.value("aliases", J::object());
        recognition::Bundle source{std::filesystem::absolute("packs/wvd"), manifest.at("revision"), {}};
        for (const auto &row : manifest.at("files")) source.files.push_back({row.at("path"), row.at("sha256")});
        const auto root = std::filesystem::absolute(".local") / ("support-check-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        recognition::Bundle bundle{root / "bundle", source.revision, {}};
        std::set<std::string> copied;
        for (const auto &name : flow.images) {
            const auto asset = games::vision::resolve_image_source(source, aliases, name);
            if (!copied.insert(asset.relative_path).second) continue;
            const auto target = bundle.root / asset.relative_path;
            std::filesystem::create_directories(target.parent_path());
            std::filesystem::copy_file(source.root / asset.relative_path, target);
            bundle.files.push_back({asset.relative_path, platform::file_sha256(target)});
        }
        recognition::Service service(bundle, games::vision::native_handlers(aliases, "zh-Hant"));
        contracts::FrameEnvelope frame;
        frame.identity.device_id = "recorded"; frame.identity.game_id = "wvd";
        frame.identity.pack_revision = bundle.revision; frame.identity.viewport_id = "900x1600";
        frame.identity.generation = 1; frame.identity.raw_size = frame.identity.recognition_size = {900, 1600};
        J evidence = J::array();
        const auto set_frame = [&](const cv::Mat &image) {
            check(image.cols == 900 && image.rows == 1600 && image.isContinuous(), "FRAME_INVALID");
            ++frame.identity.frame_id;
            frame.identity.captured_at = frame.identity.capture_finished_at = std::chrono::steady_clock::now();
            frame.raw_bgr = std::make_shared<const std::vector<std::uint8_t>>(image.data, image.data + image.total() * image.elemSize());
        };
        const auto observe = [&](J condition) {
            const auto r = service.evaluate(frame, frame.identity,
                {"support-check", "1", {0, 0, 900, 1600}, recognition::CustomParameters{"WvdVision", condition}});
            evidence.push_back({{"frame", frame.identity.frame_id}, {"condition", condition}, {"evidence", r.evidence},
                {"hit", r.outcome == O::Hit}, {"error", r.error_code}});
            std::ofstream(root / "evidence.json") << evidence.dump(2);
            check(r.outcome != O::Error, r.error_code);
            return r;
        };
        if (popup_frames) {
            const auto &unexpected = flow.nodes.at("UnexpectedPopup").at("observation_args");
            const auto &recovery = flow.nodes.at("UnownedDetail");
            check(recovery.at("observation_args") == unexpected && recovery.at("next") == J::array({"Entry"}),
                  "RECOVERY_RECHECK_NOT_WIRED");
            set_frame(cv::imread(argv[2]));
            const auto confirm = observe(library.resource_condition("combat.skill.confirm", "zh-Hant", authoring::ResourceUse::Observation));
            check(confirm.outcome == O::Hit, "NETWORK_BUTTON_FALSE_MATCH_NOT_REPRODUCED");
            check(observe(unexpected).outcome == O::NoHit, "NETWORK_RETRY_CLASSIFIED_AS_SKILL");
            set_frame(cv::imread(argv[3]));
            check(observe(unexpected).outcome == O::NoHit, "CONNECTING_CLASSIFIED_AS_SKILL");
            set_frame(cv::imread(argv[4]));
            check(observe(unexpected).outcome == O::Hit, "REAL_SKILL_DETAIL_NOT_PROTECTED");
            std::cout << "PASS network Retry false match excluded, connecting excluded, real skill protected; evidence " << root << '\n';
            return 0;
        }
        const J support{{"mode", "support_selection"}}, absent{{"mode", "support_selection"}, {"expect", "absent"}};
        for (int i = 1; i <= 2; ++i) {
            set_frame(cv::imread(argv[i]));
            check(observe(support).outcome == O::Hit, "REAL_SUPPORT_MISSING");
            check(observe(absent).outcome == O::NoHit, "REAL_SUPPORT_ALLOWS_ENEMY");
            for (int slot = 0; slot < 6; ++slot) {
                const auto r = observe({{"mode", "support_selection"}, {"slot", slot}});
                check(r.outcome == O::Hit && r.center && r.center->x > 40 + slot % 3 * 270 &&
                    r.center->x < 320 + slot % 3 * 270 && r.center->y > 1110 + slot / 3 * 175 &&
                    r.center->y < 1290 + slot / 3 * 175, "WRONG_CARD_CENTER");
            }
        }
        set_frame(cv::imread(argv[3]));
        check(observe(support).outcome == O::NoHit && observe(absent).outcome == O::Hit, "ENEMY_FALSE_SUPPORT");
        // 明确标注的合成退化：遮住一张卡片，不得因结构不完整授权敌方选敌。
        auto partial = cv::imread(argv[1]);
        cv::rectangle(partial, {40, 1110, 285, 180}, cv::Scalar(20, 20, 20), cv::FILLED);
        set_frame(partial);
        check(observe(support).outcome == O::NoHit && observe(absent).outcome == O::NoHit, "PARTIAL_GRID_ALLOWS_INPUT");
        // 只移卡片、保留标题与关闭位置，检查检测不是六个固定像素框。
        auto shifted = cv::imread(argv[1]);
        const auto cards = shifted(cv::Rect(40, 1110, 830, 360)).clone();
        cv::rectangle(shifted, {40, 1110, 830, 360}, cv::Scalar(20, 20, 20), cv::FILLED);
        cards.copyTo(shifted(cv::Rect(55, 1080, 830, 360)));
        set_frame(shifted);
        const auto moved = observe({{"mode", "support_selection"}, {"slot", 0}});
        check(moved.outcome == O::Hit && moved.center && moved.center->y < 1180, "SHIFTED_GRID_MISSING");
        set_frame(cv::Mat(1600, 900, CV_8UC3, cv::Scalar(0, 0, 0)));
        check(observe(support).outcome == O::NoHit && observe(absent).outcome == O::NoHit, "NO_DETAIL_ALLOWS_INPUT");
        std::cout << "PASS real friend/enemy frames, six mapped centers, ambiguous exclusion, shifted layout, full production flow compilation. No device input.\n";
        std::cout << root.string() << '\n';
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
