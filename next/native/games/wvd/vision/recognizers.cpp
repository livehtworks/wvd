#include "recognizers.hpp"
#include "asset_resolver.hpp"
#include "bobber.hpp"
#include "image_ops.hpp"
#include "games/wvd/business_condition.hpp"
#include <cmath>
#include <chrono>
#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect.hpp>

namespace wvd::games::vision {
using J = nlohmann::json;
namespace {
struct MovementSample {
    cv::Mat gray;
    std::chrono::steady_clock::time_point at;
};
void check(bool ok, const char *error) {
    if (!ok)
        throw std::runtime_error(error);
}
cv::Rect rect(const J &value, cv::Size size) {
    auto v = value.get<std::vector<int>>();
    check(v.size() == 4, "WVD_ROI_INVALID");
    check(v[0] >= 0 && v[1] >= 0 && v[2] > 0 && v[3] > 0 && v[2] <= size.width &&
              v[3] <= size.height && v[0] <= size.width - v[2] && v[1] <= size.height - v[3],
          "WVD_ROI_INVALID");
    return {v[0], v[1], v[2], v[3]};
}
J box(cv::Rect value) { return {value.x, value.y, value.width, value.height}; }
J decision(bool hit, cv::Rect area, J evidence, bool target = false) {
    return {{"schema", 1},
            {"outcome", hit ? "Hit" : "NoHit"},
            {"box", hit ? box(area) : J(nullptr)},
            {"target", target},
            {"evidence", std::move(evidence)}};
}
J match(const cv::Mat &source, cv::Mat templ, J p, maafw::RecognitionCache &cache,
        const std::string &key) {
    cv::Rect main(0, 0, source.cols, source.rows);
    if (p.contains("roi"))
        main = rect(p["roi"], source.size());
    auto search = source(main);
    if (p.contains("exclude")) {
        search = search.clone();
        for (const auto &value : p["exclude"]) {
            auto excluded = rect(value, source.size()) & main;
            if (!excluded.empty()) {
                excluded.x -= main.x;
                excluded.y -= main.y;
                search(excluded).setTo(0);
            }
        }
    }
    double scale = p.value("scale", 1.0), threshold = p.value("threshold", 0.8);
    check(std::isfinite(scale) && scale >= 0.3 && scale <= 2.0, "WVD_SCALE_INVALID");
    check(std::isfinite(threshold) && threshold >= 0 && threshold <= 1, "THRESHOLD_INVALID");
    if (scale != 1.0)
        cv::resize(templ, templ, {}, scale, scale, cv::INTER_LINEAR);
    if (p.contains("crop"))
        templ = templ(rect(p["crop"], templ.size()));
    check(templ.cols <= search.cols && templ.rows <= search.rows, "TEMPLATE_EXCEEDS_ROI");
    cv::Mat scores;
    const bool bright = p.value("bright_mask", false);
    if (bright) {
        int minimum = p.value("min_brightness", 145);
        check(minimum >= 0 && minimum <= 255, "WVD_MASK_INVALID");
        auto mask_key = "mask:" + key + ":" + p.dump();
        cv::Mat mask;
        if (auto found = cache.assets.find(mask_key); found != cache.assets.end())
            mask = std::any_cast<cv::Mat>(found->second);
        else {
            cv::Mat gray;
            cv::cvtColor(templ, gray, cv::COLOR_BGR2GRAY);
            cv::inRange(gray, minimum, 255, mask);
            cv::dilate(mask, mask, cv::Mat::ones(2, 2, CV_8U));
            check(cv::countNonZero(mask) > 0, "WVD_MASK_EMPTY");
            check(cache.assets.size() < 2048, "WVD_SESSION_ASSET_CAPACITY");
            cache.assets.emplace(mask_key, mask);
        }
        cv::matchTemplate(search, templ, scores, cv::TM_CCORR_NORMED, mask);
    } else
        cv::matchTemplate(search, templ, scores, cv::TM_CCOEFF_NORMED);
    for (int y = 0; y < scores.rows; ++y)
        for (int x = 0; x < scores.cols; ++x)
            if (!std::isfinite(scores.at<float>(y, x)))
                scores.at<float>(y, x) = -1;
    double maximum{};
    cv::Point location;
    cv::minMaxLoc(scores, nullptr, &maximum, nullptr, &location);
    cv::Rect found(main.x + location.x, main.y + location.y, templ.cols, templ.rows);
    J evidence{{"best_score", maximum},
               {"best_box", box(found)},
               {"threshold", threshold},
               {"scale", scale},
               {"method", bright ? "CCORR_NORMED_BRIGHT_MASK" : "CCOEFF_NORMED"}};
    if (p.value("multiple", false)) {
        std::vector<cv::Rect> rectangles;
        for (int y = 0; y < scores.rows; ++y)
            for (int x = 0; x < scores.cols; ++x)
                if (scores.at<float>(y, x) >= threshold) {
                    check(rectangles.size() < 200000, "WVD_MATCH_CANDIDATE_CAPACITY");
                    rectangles.emplace_back(x + main.x, y + main.y, templ.cols, templ.rows);
                    rectangles.push_back(rectangles.back());
                }
        cv::groupRectangles(rectangles, 1, 0.5);
        evidence["boxes"] = J::array();
        for (auto value : rectangles)
            evidence["boxes"].push_back(box(value));
    }
    return decision(maximum >= threshold, found, std::move(evidence), true);
}
J layout(const cv::Mat &source) {
    cv::Mat gray, mask, labels, stats, centers;
    cv::cvtColor(source, gray, cv::COLOR_BGR2GRAY);
    cv::inRange(gray, 120, 255, mask);
    cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, cv::Mat::ones(2, 2, CV_8U));
    int count = cv::connectedComponentsWithStats(mask, labels, stats, centers, 8);
    std::vector<cv::Rect> components;
    for (int i = 1; i < count; ++i) {
        auto area = stats.at<int>(i, cv::CC_STAT_AREA);
        cv::Rect r(stats.at<int>(i, 0), stats.at<int>(i, 1), stats.at<int>(i, 2),
                   stats.at<int>(i, 3));
        if (area >= 6 && area <= 700 && r.width >= 2 && r.width <= 80 && r.height >= 8 &&
            r.height <= 70)
            components.push_back(r);
    }
    cv::Rect bounds;
    for (auto r : components)
        bounds = bounds.empty() ? r : bounds | r;
    bool hit = components.size() >= 3 && components.size() <= 8 && bounds.width >= 55 &&
               bounds.width <= 190 && bounds.height >= 18 && bounds.height <= 70 && bounds.y >= 5 &&
               bounds.y <= 80;
    return decision(hit, bounds,
                    {{"components", components.size()},
                     {"text_width", bounds.width},
                     {"text_height", bounds.height}},
                    false);
}
J evaluate_impl(const maafw::Bundle &bundle, maafw::RecognitionPixels pixels, const J &p,
                const J &bound, const maafw::CustomRecognitionScope &scope,
                maafw::RecognitionCache &cache, unsigned depth) {
    check(depth <= 8, "WVD_CONDITION_DEPTH");
    check(pixels.size.width > 0 && pixels.size.height > 0 &&
              pixels.bgr.size() == std::size_t(pixels.size.width) * pixels.size.height * 3,
          "WVD_PIXELS_INVALID");
    cv::Mat image(pixels.size.height, pixels.size.width, CV_8UC3,
                  const_cast<std::uint8_t *>(pixels.bgr.data()));
    if (p.contains("preprocess")) {
        const auto &transform = p["preprocess"];
        const auto operation = transform.at("operation").get<std::string>();
        check(operation == "multiply" || operation == "subtract", "WVD_COLOR_OPERATION_INVALID");
        image = transform_rgb(image, transform.at("rgb").get<std::array<double, 3>>(),
                              operation == "subtract");
    }
    const J aliases = bound.value("aliases", J::object());
    AssetResolver assets(bundle, aliases, cache);
    const auto allowed = scope.allowed_roi();
    const auto allowed_rect =
        rect(J{allowed.x, allowed.y, allowed.width, allowed.height}, image.size());
    if (p.contains("roi")) {
        const auto explicit_roi = rect(p.at("roi"), image.size());
        check((explicit_roi & allowed_rect) == explicit_roi, "WVD_ROI_OUTSIDE_SCOPE");
    }
    auto mode = p.at("mode").get<std::string>();
    if (mode == "business") {
        check(!p.contains("roi") && !p.contains("preprocess"), "WVD_COMPOSITE_SCOPE_INVALID");
        const auto hit = games::business_condition(scope.business_summary(), p);
        auto result = decision(hit, allowed_rect, {{"field", p.at("field")},
                               {"comparison", p.value("comparison", "eq")}, {"expected", p.at("value")}}, false);
        result["action_eligible"] = false;
        return result;
    }
    if (mode == "all" || mode == "any" || mode == "not") {
        check(!p.contains("roi") && !p.contains("preprocess"), "WVD_COMPOSITE_SCOPE_INVALID");
        const auto &children = p.at("conditions");
        check(children.is_array() && !children.empty() && children.size() <= 16 &&
                  (mode != "not" || children.size() == 1),
              "WVD_CONDITIONS_INVALID");
        bool all = true, any = false, action_eligible = true;
        J evidence = J::array();
        // 组合只产生布尔条件，不赋予坐标许可；所有子项都检查，不能短路掩盖缺图/Error。
        for (const auto &child : children) {
            auto result = evaluate_impl(bundle, pixels, child, bound, scope, cache, depth + 1);
            check(result.at("outcome") != "Error", "WVD_CONDITION_ERROR");
            bool hit = result.at("outcome") == "Hit";
            all = all && hit;
            any = any || hit;
            // 布尔包装不能提升子识别的授权级别，尤其不能洗掉低置信 NEXT 的限制。
            action_eligible = action_eligible && result.value("action_eligible", true);
            evidence.push_back(std::move(result));
        }
        auto result = decision(mode == "all"   ? all
                               : mode == "any" ? any
                                               : !any,
                               allowed_rect, {{"conditions", evidence}}, false);
        result["action_eligible"] = action_eligible;
        return result;
    }
    if (mode == "bobber") {
        check(allowed_rect == cv::Rect(0, 0, image.cols, image.rows), "WVD_ROI_OUTSIDE_SCOPE");
        return detect_bobber(image, assets.load("fishing/bobber"));
    }
    auto one = [&](const std::string &name, J parameters) {
        const bool has_roi = parameters.contains("roi");
        auto effective = has_roi ? rect(parameters.at("roi"), image.size()) : allowed_rect;
        check((effective & allowed_rect) == effective, "WVD_ROI_OUTSIDE_SCOPE");
        parameters["roi"] = box(effective);
        auto result =
            match(image, assets.load(name), parameters, cache, bundle.revision + ":" + name);
        result["effective_roi"] = box(effective);
        result["roi_source"] = parameters.value("roi_source", has_roi ? "explicit" : "scope");
        return result;
    };
    if (mode == "movement_stopped") {
        // 沿用旧移动检查的 3 秒间隔和小地图 ROI，只保存一个 Session 内的灰度副本。
        // Hit 仅表示需要重新打开地图检查，不表示目标完成或整局游戏卡死。
        check(image.cols == 900 && image.rows == 1600, "WVD_VIEWPORT_INVALID");
        const cv::Rect area(650, 25, 225, 225);
        check((area & allowed_rect) == area, "WVD_ROI_OUTSIDE_SCOPE");
        const auto dungeon = one("dungFlag", J::object());
        const auto map = one("mapFlag", J::object());
        const std::string key = "movement.sample";
        if (dungeon.at("outcome") != "Hit" || map.at("outcome") == "Hit") {
            cache.assets.erase(key);
            return decision(false, {}, {{"reason", "not_moving_scene"}});
        }
        cv::Mat gray;
        cv::cvtColor(image(area), gray, cv::COLOR_BGR2GRAY);
        const auto now = std::chrono::steady_clock::now();
        auto found = cache.assets.find(key);
        if (found == cache.assets.end()) {
            check(cache.assets.size() < 2048, "WVD_SESSION_ASSET_CAPACITY");
            cache.assets.emplace(key, MovementSample{gray, now});
            return decision(false, {}, {{"reason", "first_sample"}});
        }
        auto &previous = std::any_cast<MovementSample &>(found->second);
        if (now - previous.at < std::chrono::seconds(3))
            return decision(false, {}, {{"reason", "sample_interval"}});
        cv::Mat difference;
        cv::absdiff(gray, previous.gray, difference);
        const double mean = cv::mean(difference)[0] / 255;
        previous = {gray, now};
        return decision(mean < 0.1, area, {{"mean_difference", mean}, {"threshold", 0.1}}, false);
    }
    if (mode == "template" || mode == "bright_mask" || mode == "multiple") {
        auto parameters = p;
        parameters["roi_source"] = p.contains("roi") ? "explicit" : "scope";
        if (!p.contains("roi") && p.value("default_roi", false)) {
            const auto name = p.at("image").get<std::string>();
            if (name == "next" || name == "combatTarget")
                parameters["roi"] = {80, 220, 819, 680};
            else if (name == "flee")
                parameters["roi"] = {720, 1120, 180, 130};
            else if (name == "combatActive" || name == "combatActive_2" ||
                     name == "combatActive_3" || name == "combatActive_4")
                parameters["roi"] = {0, 0, 150, 80};
            if (parameters.contains("roi"))
                parameters["roi_source"] = "default";
        }
        parameters["bright_mask"] = mode == "bright_mask" || p.value("bright_mask", false);
        parameters["multiple"] = mode == "multiple";
        return one(p.at("image"), parameters);
    }
    if (mode == "fast_forward_off") {
        auto result = one("fastforward_off", {{"roi", {190, 1440, 100, 100}}});
        const auto &candidate = result["evidence"]["best_box"];
        result["evidence"]["legacy_position"] = {candidate[0], candidate[1]};
        if (result["evidence"]["best_score"].get<double>() <= 0.8)
            return decision(false, {}, result["evidence"]);
        return result;
    }
    if (mode == "harken_stair") {
        if (p.contains("stair") && p["stair"].is_string() &&
            p["stair"].get<std::string>().starts_with("stair_")) {
            auto result = one(p.at("stair"), J::object());
            if (result["evidence"]["best_score"].get<double>() <= 0.8)
                return decision(
                    false, {},
                    {{"wrong_stair", true}, {"match", result}, {"navigation", "M4_NOT_EXECUTED"}});
        }
        auto result = one(p.at("image"), J::object());
        if (result["evidence"]["best_score"].get<double>() <= 0.8)
            return decision(false, {}, result["evidence"]);
        return result;
    }
    // 低置信结果只供已限定阶段的调用者判断；本纯识别器从不触发点击/自动战斗。
    if (mode == "next_low_confidence" || mode == "target_marker") {
        auto result = one(
            mode == "target_marker" ? "combatTarget" : "next",
            {{"roi", {80, 220, 819, 680}}, {"threshold", mode == "target_marker" ? 0.86 : 0.60}});
        result["action_eligible"] = mode != "next_low_confidence";
        return result;
    }
    if (mode == "next") {
        J attempts = J::array();
        for (const auto &name : {"next", "combatTarget"}) {
            for (double scale : p.value("scales", std::vector<double>{1.0})) {
                auto result = one(name, {{"roi", {80, 220, 819, 680}},
                                         {"threshold", p.value("threshold", 0.86)},
                                         {"scale", scale}});
                attempts.push_back({{"image", name}, {"result", result}});
                if (result["outcome"] == "Hit") {
                    result["attempts"] = attempts;
                    return result;
                }
            }
        }
        return decision(false, {}, {{"attempts", attempts}});
    }
    if (mode == "pause_layout") {
        auto area = rect(p.at("roi"), image.size());
        auto result = layout(image(area));
        if (result["outcome"] == "Hit")
            result["box"] = box(area);
        return result;
    }
    if (mode == "pause_ocr")
        throw std::runtime_error("PAUSE_TESSERACT_NOT_MIGRATED");
    if (mode == "pause" || mode == "pause_negative") {
        check(image.cols == 900 && image.rows == 1600, "WVD_VIEWPORT_INVALID");
        cv::Rect area(330, 740, 240, 110);
        check((area & allowed_rect) == area, "WVD_ROI_OUTSIDE_SCOPE");
        cv::Mat gray;
        cv::cvtColor(image(area), gray, cv::COLOR_BGR2GRAY);
        double dark = double(cv::countNonZero(gray < 70)) / gray.total(),
               white = double(cv::countNonZero(gray > 120)) / gray.total(), maximum{};
        cv::minMaxLoc(gray, nullptr, &maximum);
        J detail{{"dark_ratio", dark},
                 {"white_ratio", white},
                 {"max_brightness", maximum},
                 {"ocr_status", "UNVERIFIED_TESSERACT_NOT_MIGRATED"}};
        if (mode == "pause" && !(dark > 0.65 && white > 0.015 && white < 0.09 && maximum > 135))
            return decision(false, {}, detail);
        for (const auto &name : {"trait", "recover", "spellskill/skillDetail", "close"}) {
            J args = J::object();
            if (std::string_view(name) == "close")
                args["roi"] = {250, 1420, 420, 150};
            auto evidence = one(name, args);
            if (evidence["outcome"] == "Hit") {
                detail["negative"] = name;
                detail["match"] = evidence;
                return decision(mode == "pause_negative", area, detail);
            }
        }
        if (mode == "pause_negative")
            return decision(false, {}, detail);
        auto result = layout(image(area));
        detail["layout"] = result;
        return decision(result["outcome"] == "Hit", area, detail);
    }
    if (mode == "combat_active") {
        J attempts = J::array();
        for (const auto &name :
             {"combatActive", "combatActive_2", "combatActive_3", "combatActive_4"}) {
            auto result = one(name, {{"roi", {0, 0, 150, 80}}});
            attempts.push_back(result);
            if (result["outcome"] == "Hit") {
                result["attempts"] = attempts;
                return result;
            }
        }
        return decision(false, {}, {{"attempts", attempts}});
    }
    if (mode == "prepared_actor" || mode == "skill_target") {
        const auto summary = scope.business_summary();
        if (!summary.at("has_prepared_skill").get<bool>())
            return decision(false, {}, {{"reason", "no_prepared_skill"}});
        const auto name = "spellskill/char/" + summary.at("prepared_portrait").get<std::string>();
        const auto &portraits = p.at("portraits");
        check(portraits.is_array() && portraits.size() <= 384, "COMBAT_PORTRAITS_INVALID");
        bool declared = false;
        for (const auto &entry : portraits)
            declared = declared || entry.at("image") == name;
        check(declared, "COMBAT_PORTRAIT_NOT_COMPILED");
        auto actor = evaluate_impl(bundle, pixels, {{"mode", "portrait"}, {"image", name}}, bound, scope, cache, depth + 1);
        if (mode == "prepared_actor" || actor.at("outcome") != "Hit")
            return actor;
        // 0.60 只在已选技能、同一角色、详情仍开且没有友方/OK 确认的单体阶段生效。
        // 普通 next_low_confidence 的低信任契约不变，不能用组合条件直接抬升它。
        auto detail = one("spellskill/skillDetail", J::object());
        auto ok = one("OK", J::object());
        auto support = one("supportSkillCheck", {{"roi", {677, 1475, 189, 80}}});
        if (detail.at("outcome") != "Hit" || ok.at("outcome") == "Hit" || support.at("outcome") == "Hit")
            return decision(false, {}, {{"reason", "not_enemy_selection"}});
        J attempts = J::array();
        for (const auto &candidate : {std::pair{"next", .86}, std::pair{"combatTarget", .86}, std::pair{"next", .60}}) {
            auto result = one(candidate.first, {{"roi", {80, 220, 819, 680}}, {"threshold", candidate.second}});
            attempts.push_back(result);
            if (result.at("outcome") == "Hit") {
                result["evidence"]["attempts"] = attempts;
                result["evidence"]["selection_index"] = summary.at("prepared_skill_index");
                result["evidence"]["actor"] = actor;
                result["evidence"]["phase"] = "confirmed_single_target_detail";
                return result;
            }
        }
        return decision(false, {}, {{"attempts", attempts}});
    }
    if (mode == "portrait") {
        auto name = p.at("image").get<std::string>();
        auto templ = assets.load(name);
        int w = templ.cols, h = templ.rows;
        std::vector<cv::Point> bases{{87, 55}, {24, 55}, {24, 63}, {32, 55}};
        if (p.contains("active"))
            bases.push_back(
                {int(p["active"][0].get<int>() - w * 0.35), p["active"][1].get<int>() + 35});
        std::vector<cv::Rect> crops{{0, 0, w, h},
                                    {w * 40 / 100, 0, w - w * 40 / 100, h},
                                    {w * 33 / 100, 0, w - w * 33 / 100, h * 80 / 100},
                                    {0, 0, w, h * 70 / 100}};
        J best;
        double score = -2;
        for (auto base : bases)
            for (auto crop : crops) {
                auto result =
                    match(image, templ,
                          {{"roi", {base.x + crop.x, base.y + crop.y, crop.width, crop.height}},
                           {"crop", box(crop)},
                           {"threshold", p.value("threshold", 0.8)}},
                          cache, bundle.revision + name);
                if (result["evidence"]["best_score"].get<double>() > score) {
                    score = result["evidence"]["best_score"];
                    best = result;
                    best["evidence"]["base"] = {base.x, base.y};
                    best["evidence"]["crop"] = box(crop);
                }
            }
        return best;
    }
    if (mode == "skill_level") {
        int level = p.at("level");
        check(level >= 1 && level <= 9, "WVD_SKILL_LEVEL_INVALID");
        J attempts = J::array();
        for (auto prefix : {"lv", "s_lv"}) {
            auto args = p;
            args["roi"] = {0, 0, image.cols, image.rows};
            auto result =
                one(std::string("spellskill/skillLvl/") + prefix + std::to_string(level), args);
            attempts.push_back(result);
            if (result["outcome"] == "Hit") {
                result["attempts"] = attempts;
                return result;
            }
        }
        return decision(false, {}, {{"attempts", attempts}});
    }
    if (mode == "focus_cursor") {
        auto name = p.at("image").get<std::string>();
        auto templ = assets.load(name);
        auto result = one(name, p);
        if (result["outcome"] != "Hit")
            return result;
        check(templ.cols >= 15 && templ.rows >= 15, "WVD_CURSOR_TEMPLATE_SMALL");
        auto found = rect(result["box"], image.size());
        cv::Rect center((templ.cols - 15) / 2, (templ.rows - 15) / 2, 15, 15);
        cv::Mat a, b, diff;
        cv::cvtColor(image(found)(center), a, cv::COLOR_BGR2GRAY);
        cv::cvtColor(templ(center), b, cv::COLOR_BGR2GRAY);
        cv::absdiff(a, b, diff);
        double difference = cv::mean(diff)[0] / 255;
        return decision(difference < 0.2, found,
                        {{"match", result}, {"center_difference", difference}}, false);
    }
    if (mode == "reached" || mode == "through_stair") {
        int x = p.at("position")[0], y = p.at("position")[1];
        if (mode == "reached") {
            x = std::clamp(x, 33, 866);
            y = std::clamp(y, 33, 1566);
        }
        cv::Rect area(x - 33, y - 33, 66, 66);
        rect(box(area), image.size());
        J attempts = J::array();
        if (mode == "reached") {
            for (int i = 0; i < 4; ++i) {
                auto result = one("cursor_" + std::to_string(i), {{"roi", box(area)}});
                attempts.push_back(result);
                if (result["evidence"]["best_score"].get<double>() > 0.8)
                    return decision(true, area, {{"attempts", attempts}});
            }
            return decision(false, {}, {{"attempts", attempts}});
        }
        auto name = p.at("image").get<std::string>();
        bool stair = name == "stair_up" || name == "stair_down" || name == "stair_teleport";
        auto result = one(name, stair ? J{{"roi", box(area)}} : J::object());
        bool present = result["evidence"]["best_score"].get<double>() > 0.8;
        return decision(stair ? !present : present, area, {{"template", result}, {"stair", stair}});
    }
    throw std::runtime_error("WVD_RECOGNIZER_UNKNOWN");
}
J evaluate(const maafw::Bundle &bundle, maafw::RecognitionPixels pixels, const J &p, const J &bound,
           const maafw::CustomRecognitionScope &scope, maafw::RecognitionCache &cache) {
    return evaluate_impl(bundle, pixels, p, bound, scope, cache, 0);
}
} // namespace
void register_wvd(runtime::BehaviorRegistry &registry) {
    registry.add_recognition({"wvd.vision", "1"}, evaluate);
}
contracts::BehaviorBinding binding(const J &aliases) {
    return {"WvdVision", {"wvd.vision", "1"}, {{"aliases", aliases}}};
}
} // namespace wvd::games::vision
