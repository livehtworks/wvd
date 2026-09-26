#include "service.hpp"
#include "platform/execution_timing.hpp"
#include "platform/windows/bundle_lease.hpp"
#include <algorithm>
#include <cmath>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <stdexcept>

namespace wvd::recognition {
namespace {
void require(bool condition, const char *code) {
    if (!condition) throw std::runtime_error(code);
}
bool within(contracts::Box box, contracts::Size size) {
    return box.x >= 0 && box.y >= 0 && box.width > 0 && box.height > 0 &&
        box.x <= size.width - box.width && box.y <= size.height - box.height;
}
bool inside(contracts::Box inner, contracts::Box outer) {
    return inner.x >= outer.x && inner.y >= outer.y &&
        inner.x + inner.width <= outer.x + outer.width &&
        inner.y + inner.height <= outer.y + outer.height;
}
cv::Rect rect(contracts::Box box) { return {box.x, box.y, box.width, box.height}; }
contracts::Box box(const cv::Rect &value) {
    return {value.x, value.y, value.width, value.height};
}
std::string frame_key(const contracts::FrameIdentity &basis) {
    return nlohmann::json::array({basis.device_id, basis.game_id, basis.pack_revision,
        basis.viewport_id, basis.generation, basis.frame_id, basis.action_epoch,
        basis.connection_generation, basis.captured_at.time_since_epoch().count()}).dump();
}
void hit(contracts::Observation &observation, contracts::Box location, bool target) {
    observation.outcome = contracts::RecognitionOutcome::Hit;
    observation.box = location;
    if (target) observation.center = contracts::Point{location.x + location.width / 2,
                                                        location.y + location.height / 2};
}
bool bounded_json(const nlohmann::json &value, std::size_t &remaining) {
    if (!remaining--) return false;
    if (value.is_array() || value.is_object())
        for (const auto &child : value)
            if (!bounded_json(child, remaining)) return false;
    return true;
}
} // namespace

Service::Service(Bundle bundle, Handlers handlers, std::shared_ptr<MatchBudget> budget,
                 std::filesystem::path diagnostics_path,
                 std::uint64_t run_id, std::uint64_t generation)
    : bundle_(std::move(bundle)), handlers_(std::move(handlers)) {
    cache_.decoded = std::make_shared<DecodedAssetCache>();
    cache_.match_budget = budget ? std::move(budget) : std::make_shared<MatchBudget>();
    cache_.cancelled = &cancelled_;
    cache_.diagnostics = std::make_shared<platform::MemoryDiagnostics>(
        diagnostics_path, run_id, generation);
    require(bundle_.root.is_absolute() && !bundle_.revision.empty(), "BUNDLE_IDENTITY_INVALID");
    platform::BundleLease::Manifest manifest;
    for (const auto &file : bundle_.files)
        require(manifest.emplace(file.relative_path, file.sha256).second,
                "BUNDLE_DUPLICATE_FILE");
    if (!bundle_.lease)
        bundle_.lease = std::make_shared<platform::BundleLease>(bundle_.root,
                                                                  bundle_.revision, manifest);
    require(bundle_.lease->root() == bundle_.root &&
                bundle_.lease->revision() == bundle_.revision,
            "BUNDLE_LEASE_MISMATCH");
    bundle_.lease->verify_members();
}

contracts::Observation Service::evaluate(const contracts::FrameEnvelope &frame,
                                          const contracts::FrameIdentity &current,
                                          const Request &request,
                                          const contracts::BusinessRunState *business) {
    std::lock_guard lock(mutex_);
    contracts::Observation result;
    result.basis = frame.identity;
    result.recognizer_id = request.recognizer_id;
    result.parameter_revision = request.parameter_revision;
    result.error_stage = "frame_preflight";
    try {
        require(!cancelled_.load(), "RECOGNITION_CANCELLED");
        require(!request.recognizer_id.empty() && !request.parameter_revision.empty(),
                "RECO_IDENTITY_INVALID");
        validate_frame_identity(frame, current, bundle_.revision);
        const auto key = frame_key(frame.identity);
        if (key != frame_pixels_key_) {
            auto diagnostic = cache_.diagnostics->begin({
                std::hash<std::string>{}(request.recognizer_id), 0,
                frame.encoded_image.size(), frame.identity.raw_size.width,
                frame.identity.raw_size.height, 0, 0, 0, 0, 3, -3,
                false, false, false, 0, 0});
            try { frame_pixels_ = std::make_unique<FramePixels>(frame, current, bundle_.revision); }
            catch (const std::bad_alloc &) { diagnostic.failure(-1); throw; }
            catch (const cv::Exception &error) {
                if (error.code == cv::Error::StsNoMem) diagnostic.failure(error.code);
                throw;
            }
            frame_pixels_key_ = key;
        }
        require(within(request.roi, frame_pixels_->size()), "ROI_INVALID");
        result.error_stage = "recognition";
        if (key != cache_.frame_key) {
            cache_.frame_key = key;
            cache_.results.clear();
            cache_.result_bytes = 0;
            cache_.template_results.clear();
            cache_.template_result_bytes = 0;
        }
        return evaluate_locked(*frame_pixels_, request, business, frame.identity);
    } catch (const ResourcePressure &) {
        result.error_code = "RECOGNITION_RESOURCE_PRESSURE";
        result.outcome = contracts::RecognitionOutcome::Error;
        return result;
    } catch (const std::bad_alloc &) {
        result.error_code = "OOM";
        result.outcome = contracts::RecognitionOutcome::Error;
        return result;
    } catch (const cv::Exception &error) {
        result.error_code = error.code == cv::Error::StsNoMem ?
            "CV_OOM:-4" :
            "RECOGNITION_OPENCV_ERROR:" + std::to_string(error.code);
        result.outcome = contracts::RecognitionOutcome::Error;
        return result;
    } catch (const std::exception &error) {
        result.error_code = error.what();
        result.outcome = contracts::RecognitionOutcome::Error;
        return result;
    }
}

contracts::Observation Service::evaluate_locked(const FramePixels &pixels, const Request &request,
                                                 const contracts::BusinessRunState *business,
                                                 const contracts::FrameIdentity &basis) {
    require(!cancelled_.load(), "RECOGNITION_CANCELLED");
    contracts::Observation result;
    result.basis = basis;
    result.recognizer_id = request.recognizer_id;
    result.parameter_revision = request.parameter_revision;
    result.outcome = contracts::RecognitionOutcome::NoHit;
    result.error_stage = "recognition";
    cache_.source_id = std::hash<std::string>{}(request.recognizer_id);
    if (const auto *templ = std::get_if<TemplateParameters>(&request.parameters)) {
        require(std::isfinite(templ->threshold) && templ->threshold >= 0 &&
                    templ->threshold <= 1, "THRESHOLD_INVALID");
        const auto relative = std::string("image/") + templ->image;
        bundle_.lease->require_member(relative);
        const auto asset_key = bundle_.revision + ":" + bundle_.lease->identity() + ":" + relative;
        auto lease = cache_.decoded->load(asset_key, [&] {
            const auto &bytes = bundle_.lease->bytes(relative);
            require(!bytes.empty() && bytes.size() <= 64 * 1024 * 1024,
                    "TEMPLATE_BYTES_INVALID");
            const auto stats = cache_.decoded->stats();
            auto diagnostic = cache_.diagnostics->begin({
                cache_.source_id, std::hash<std::string>{}(asset_key), bytes.size(),
                0, 0, 0, 0, 0, 0, 3, -2, false, false, false,
                stats.retained_bytes, stats.in_use_bytes});
            cv::Mat image;
            try { image = cv::imdecode(bytes, cv::IMREAD_COLOR); }
            catch (const std::bad_alloc &) { diagnostic.failure(-1); throw; }
            catch (const cv::Exception &error) {
                if (error.code == cv::Error::StsNoMem) diagnostic.failure(error.code);
                throw;
            }
            require(!image.empty() && image.type() == CV_8UC3,
                    "TEMPLATE_DECODE_INVALID");
            return image;
        }, &cancelled_);
        const auto &image = lease.mat();
        require(image.cols <= request.roi.width && image.rows <= request.roi.height,
                "TEMPLATE_EXCEEDS_ROI");
        const auto estimated = estimate_match_workspace(
            {request.roi.width, request.roi.height}, image.size(), 3, false, false, false);
        const auto assets = cache_.decoded->stats();
        auto diagnostic = cache_.diagnostics->begin({
            cache_.source_id, std::hash<std::string>{}(asset_key), estimated, pixels.size().width,
            pixels.size().height, request.roi.width, request.roi.height,
            image.cols, image.rows, image.channels(), cv::TM_CCOEFF_NORMED,
            false, false, false, assets.retained_bytes, assets.in_use_bytes,
            cache_.match_budget->stats().active_matches});
        MatchBudget::Ticket ticket;
        cv::Mat scores;
        try {
            ticket = cache_.match_budget->acquire(estimated, cancelled_);
            platform::timing::Scope measure(platform::timing::Part::Match);
            platform::timing::count(platform::timing::Counter::Matches);
            cv::matchTemplate(pixels.mat()(rect(request.roi)), image, scores,
                              cv::TM_CCOEFF_NORMED);
        } catch (const std::bad_alloc &) { diagnostic.failure(-1); throw; }
        catch (const cv::Exception &error) {
            if (error.code == cv::Error::StsNoMem) diagnostic.failure(error.code);
            throw;
        } catch (const ResourcePressure &) { diagnostic.failure(-2); throw;
        }
        require(!scores.empty(), "TEMPLATE_MATCH_INVALID");
        for (int y = 0; y < scores.rows; ++y)
            for (int x = 0; x < scores.cols; ++x)
                if (!std::isfinite(scores.at<float>(y, x))) scores.at<float>(y, x) = -1;
        double maximum{};
        cv::Point location;
        cv::minMaxLoc(scores, nullptr, &maximum, nullptr, &location);
        auto found_box = contracts::Box{request.roi.x + location.x,
                                        request.roi.y + location.y, image.cols, image.rows};
        result.evidence = {{"best_score", maximum}, {"threshold", templ->threshold},
                           {"method", "CCOEFF_NORMED"}};
        if (maximum >= templ->threshold) {
            result.matches.push_back({found_box, maximum, {}});
            hit(result, found_box, true);
        }
        return result;
    }
    if (std::holds_alternative<OcrParameters>(request.parameters))
        return recognize_ocr(pixels, request, basis);
    const auto &custom = std::get<CustomParameters>(request.parameters);
    auto handler = handlers_.find(custom.binding);
    require(handler != handlers_.end(), "CUSTOM_RECO_NOT_REGISTERED");
    const auto key = custom.binding + ":" + custom.parameters.dump() + ":" +
        nlohmann::json::array({request.roi.x, request.roi.y, request.roi.width,
                               request.roi.height}).dump() + ":" +
        std::to_string(business ? business->version() : 0);
    nlohmann::json detail;
    if (auto previous = cache_.results.find(key); previous != cache_.results.end())
        detail = previous->second;
    else {
        const auto ocr = [&](const nlohmann::json &condition) {
            require(condition.value("mode", "") == "ocr", "CUSTOM_OCR_CONTEXT_INVALID");
            auto roi = condition.value("roi", nlohmann::json::array({request.roi.x,
                request.roi.y, request.roi.width, request.roi.height}));
            auto nested = parse_request({{"id", "custom.ocr"}, {"revision", bundle_.revision},
                {"type", "ocr"}, {"roi", roi}, {"expected", condition.at("expected")}});
            require(inside(nested.roi, request.roi), "CUSTOM_OCR_OUTSIDE_SCOPE");
            auto observed = recognize_ocr(pixels, nested, basis);
            require(observed.outcome != contracts::RecognitionOutcome::Error,
                    observed.error_code.c_str());
            return nlohmann::json{{"schema", 1},
                {"outcome", observed.outcome == contracts::RecognitionOutcome::Hit ? "Hit" : "NoHit"},
                {"box", observed.box ? nlohmann::json::array({observed.box->x,
                    observed.box->y, observed.box->width, observed.box->height}) : nlohmann::json(nullptr)},
                {"target", bool(observed.center)}, {"evidence", observed.evidence}};
        };
        Scope scope(request.roi, ++invocation_, business, ocr, &cancelled_);
        detail = handler->second(bundle_, {pixels.bgr(), pixels.size()}, custom.parameters,
                                 scope, cache_);
        require(detail.is_object() && detail.value("schema", 0) == 1,
                "CUSTOM_DETAIL_INVALID");
        if (cache_.results.size() + cache_.template_results.size() < 128) {
            std::size_t nodes = 256;
            if (bounded_json(detail, nodes)) {
                const auto payload_bytes = detail.dump().size();
                if (payload_bytes <= 8192 &&
                    cache_.result_bytes + cache_.template_result_bytes + key.size() + payload_bytes <= 1024 * 1024) {
                    cache_.results.emplace(key, detail);
                    cache_.result_bytes += key.size() + payload_bytes;
                }
            }
        }
    }
    const auto outcome = detail.at("outcome").get<std::string>();
    require(outcome == "Hit" || outcome == "NoHit", "CUSTOM_OUTCOME_INVALID");
    result.evidence = detail;
    result.action_eligible = detail.value("action_eligible", true);
    if (outcome == "Hit") {
        const auto values = detail.at("box").get<std::vector<int>>();
        require(values.size() == 4, "CUSTOM_BOX_INVALID");
        contracts::Box location{values[0], values[1], values[2], values[3]};
        require(within(location, pixels.size()) && inside(location, request.roi),
                "CUSTOM_BOX_INVALID");
        hit(result, location, detail.value("target", false));
    }
    return result;
}

contracts::Observation Service::recognize_ocr(const FramePixels &pixels,
                                                const Request &request,
                                                const contracts::FrameIdentity &basis) {
    require(!cancelled_.load(), "RECOGNITION_CANCELLED");
    contracts::Observation result;
    result.basis = basis;
    result.recognizer_id = request.recognizer_id;
    result.parameter_revision = request.parameter_revision;
    result.outcome = contracts::RecognitionOutcome::NoHit;
    result.error_stage = "ocr";
    const auto &parameters = std::get<OcrParameters>(request.parameters);
    require(!parameters.expected_text.empty() && parameters.expected_text.size() <= 32,
            "OCR_EXPECTED_INVALID");
    for (const auto &expected : parameters.expected_text) {
        require(!expected.empty() && expected.size() <= 1024 &&
                    expected.find('\0') == std::string::npos, "OCR_EXPECTED_INVALID");
        require(std::all_of(expected.begin(), expected.end(), [](unsigned char c) {
                    return c < 128;
                }), "OCR_LANGUAGE_UNSUPPORTED");
    }
    auto engine = ocr_.load();
    if (!engine) {
        for (const auto &[relative, expected] : std::vector<std::pair<std::string, std::string>>{
            {"model/ocr/det.onnx", "8fe4bf6abfb20402357827f2efc964c8b28cf980e29fe09a99b742ae29725fa9"},
            {"model/ocr/rec.onnx", "da12c6e863761d774b07d3bd40fbaaa55516f90570e0b1dd9dc112e457301cc9"},
            {"model/ocr/keys.txt", "5662df9d2d03f0e8ca0d3b0649d6acbab904b6a14b3d3521463c71c37c668ce3"}}) {
            bundle_.lease->require_member(relative);
            require(bundle_.lease->hash(relative) == expected, "OCR_MODEL_NOT_LOCKED");
        }
        require(!cancelled_.load(), "RECOGNITION_CANCELLED");
        engine = std::make_shared<OcrEngine>(bundle_.root / "model" / "ocr");
        ocr_.store(engine);
    }
    // 发布前发生的取消由该检查承接；发布后发生的取消直接命中同一引擎。
    if (cancelled_.load()) {
        engine->cancel();
        throw std::runtime_error("RECOGNITION_CANCELLED");
    }
    auto found = engine->recognize(pixels.mat()(rect(request.roi)));
    require(!cancelled_.load(), "RECOGNITION_CANCELLED");
    result.evidence = {{"language", "en"}, {"model", "locked-det-rec-2026"},
                       {"candidates", found.size()}};
    for (auto &candidate : found) {
        candidate.box.x += request.roi.x;
        candidate.box.y += request.roi.y;
        bool expected = std::any_of(parameters.expected_text.begin(),
                                    parameters.expected_text.end(), [&](const std::string &needle) {
                                        return candidate.text.find(needle) != std::string::npos;
                                    });
        if (expected && candidate.score >= 0.3)
            result.matches.push_back(candidate);
    }
    if (!result.matches.empty()) hit(result, result.matches.front().box, true);
    return result;
}

void Service::cancel() noexcept {
    cancelled_.store(true);
    if (cache_.match_budget) cache_.match_budget->wake();
    // 请求取消不等于推理已经退出；Session 仍持有所有资源直到调用实际返回。
    try { if (auto engine = ocr_.load()) engine->cancel(); }
    catch (...) { /* 取消标记仍有效，不能由控制线程抛出并破坏停止链。 */ }
}
ResourceStats Service::resource_stats() const {
    auto result = cache_.decoded->stats();
    const auto work = cache_.match_budget->stats();
    result.active_matches = work.active_matches;
    result.peak_matches = work.peak_matches;
    result.estimated_workspace_bytes = work.estimated_workspace_bytes;
    result.peak_estimated_workspace_bytes = work.peak_estimated_workspace_bytes;
    result.result_cache_entries = cache_.results.size() + cache_.template_results.size();
    result.result_cache_estimated_bytes = cache_.result_bytes + cache_.template_result_bytes;
    return result;
}
} // namespace wvd::recognition
