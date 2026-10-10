#include "service.hpp"
#include "ocr_models.hpp"
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
                 std::uint64_t run_id, std::uint64_t generation,
                 storage::LoggingPolicy logging, std::shared_ptr<RunOcrModels> models)
    : bundle_(std::move(bundle)), handlers_(std::move(handlers)),
      models_(models ? std::move(models) : std::make_shared<RunOcrModels>()) {
    cache_.match_budget = budget ? std::move(budget) : std::make_shared<MatchBudget>();
    cache_.decoded = std::make_shared<DecodedAssetCache>(128ULL * 1024 * 1024, cache_.match_budget);
    cache_.cancelled = &cancelled_;
    cache_.diagnostics = std::make_shared<platform::MemoryDiagnostics>(
        diagnostics_path, run_id, generation,
        logging.memory && logging.accepts(storage::LogLevel::Debug), logging.memory_interval_ms);
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
    lifetime_.ready();
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
            try {
                auto ticket = cache_.match_budget->acquire(
                    std::uint64_t(frame.identity.recognition_size.width) * frame.identity.recognition_size.height * 12,
                    cancelled_);
                frame_pixels_ = std::make_unique<FramePixels>(frame, current, bundle_.revision);
            }
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
            ocr_frame_results_.clear();
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
    } catch (const platform::MissingBundleMember &error) {
        result.error_code = error.what();
        result.evidence = {{"missing_resource", error.member()}};
        result.outcome = contracts::RecognitionOutcome::Error;
        return result;
    } catch (const std::exception &error) {
        result.error_code = error.what();
        result.outcome = contracts::RecognitionOutcome::Error;
        return result;
    }
}

void Service::note_known_scene(const contracts::Observation &observation) {
    std::lock_guard lock(mutex_);
    require(!cancelled_.load() && observation.outcome == contracts::RecognitionOutcome::Hit,
            "KNOWN_SCENE_EVIDENCE_INVALID");
    const auto key = frame_key(observation.basis);
    require(key == cache_.frame_key && key == frame_pixels_key_, "KNOWN_SCENE_FRAME_MISMATCH");
    if (known_scene_frame_key_ == key) return;
    known_scene_frame_key_ = key;
    ++cache_.known_scene_epoch;
    // 状态性复合缓存失效，纯模板叶子仍有效；不清空模板像素、运动样本或业务状态。
    cache_.results.clear();
    cache_.result_bytes = 0;
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
    cache_.frame_identity = basis;
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
            auto definition = condition;
            definition.update({{"id", "custom.ocr"}, {"revision", bundle_.revision},
                               {"type", "ocr"}, {"roi", roi}});
            auto nested = parse_request(definition);
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
    validate_ocr_parameters(parameters);
    static const auto models = nlohmann::json::parse(wvd_ocr_models).at("models");
    const auto &model = models.at(parameters.language);
    auto &slot = ocr_[parameters.language == "en" ? 0 : 1];
    const auto started = std::chrono::steady_clock::now();
    auto engine = slot.load();
    if (!engine) {
        for (const auto &[name, spec] : model.at("files").items()) {
            const auto relative = model.at("bundle_directory").get<std::string>() + "/" + name;
            bundle_.lease->require_member(relative);
            require(bundle_.lease->hash(relative) == spec.at("sha256").get<std::string>(), "OCR_MODEL_NOT_LOCKED");
        }
        require(!cancelled_.load(), "RECOGNITION_CANCELLED");
        engine = models_->acquire(parameters.language == "en" ? 0 : 1, model.dump(), [&] {
            auto ticket = cache_.match_budget->acquire(192ULL * 1024 * 1024, cancelled_);
            return std::make_shared<OcrEngine>(bundle_.root /
                platform::BundleLease::checked_relative(model.at("bundle_directory").get<std::string>()),
                cache_.diagnostics, bundle_.lease);
        });
        slot.store(engine);
    }
    // 发布前发生的取消由该检查承接；发布后发生的取消直接命中同一引擎。
    if (cancelled_.load()) {
        engine->cancel();
        throw std::runtime_error("RECOGNITION_CANCELLED");
    }
    const auto initialized = std::chrono::steady_clock::now();
    const auto key = parameters.language + ":" + nlohmann::json::array({request.roi.x,
        request.roi.y, request.roi.width, request.roi.height}).dump();
    const auto cached = ocr_frame_results_.find(key);
    const bool cache_hit = cached != ocr_frame_results_.end();
    std::vector<contracts::RecognitionMatch> found;
    if (cache_hit) found = cached->second;
    else {
        auto ticket = cache_.match_budget->acquire(
            std::uint64_t(request.roi.width) * request.roi.height * 64 + 32ULL * 1024 * 1024, cancelled_);
        found = engine->recognize(pixels.mat()(rect(request.roi)));
    }
    if (!cache_hit) {
        std::size_t bytes = 0;
        for (const auto &candidate : found) bytes += candidate.text.size() + sizeof(candidate);
        if (found.size() <= 128 && bytes <= 65536 && ocr_frame_results_.size() < 4)
            ocr_frame_results_.emplace(key, found);
    }
    require(!cancelled_.load(), "RECOGNITION_CANCELLED");
    const auto finished = std::chrono::steady_clock::now();
    result.evidence = {{"language", parameters.language}, {"model", model.at("name")},
        {"candidates", found.size()}, {"cache_hit", cache_hit}, {"match", parameters.match},
        {"unique", parameters.unique}, {"threshold", parameters.threshold},
        {"init_ms", std::chrono::duration<double, std::milli>(initialized - started).count()},
        {"inference_ms", std::chrono::duration<double, std::milli>(finished - initialized).count()},
        {"texts", nlohmann::json::array()}};
    for (auto &candidate : found) {
        candidate.box.x += request.roi.x;
        candidate.box.y += request.roi.y;
        bool expected = std::any_of(parameters.expected_text.begin(),
                                    parameters.expected_text.end(), [&](const std::string &needle) {
                                        return parameters.match == "exact" ? candidate.text == needle :
                                            candidate.text.find(needle) != std::string::npos;
                                    });
        if (result.evidence["texts"].size() < 32)
            result.evidence["texts"].push_back({{"text", candidate.text.substr(0, 1024)},
                {"score", candidate.score}, {"box", {candidate.box.x, candidate.box.y,
                    candidate.box.width, candidate.box.height}}});
        if (expected && candidate.score >= parameters.threshold)
            result.matches.push_back(candidate);
    }
    if (parameters.unique && result.matches.size() > 1)
        result.evidence["reason"] = "OCR_AMBIGUOUS";
    else if (!result.matches.empty()) hit(result, result.matches.front().box, true);
    return result;
}

void Service::cancel() noexcept {
    cancelled_.store(true);
    if (cache_.match_budget) cache_.match_budget->wake();
    // 请求取消不等于推理已经退出；Session 仍持有所有资源直到调用实际返回。
    try { for (auto &slot : ocr_) if (auto engine = slot.load()) engine->cancel(); }
    catch (...) { /* 取消标记仍有效，不能由控制线程抛出并破坏停止链。 */ }
}
nlohmann::json Service::ownership_snapshot() const {
    nlohmann::json models = nlohmann::json::array();
    for (std::size_t i = 0; i < ocr_.size(); ++i) {
        const auto engine = ocr_[i].load();
        models.push_back({{"language", i ? "zh-Hant" : "en"}, {"initialized", bool(engine)},
            {"owners_excluding_probe", engine ? engine.use_count() - 1 : 0},
            {"runtime_allocation_bytes", nullptr}});
    }
    const auto bytes = bundle_.lease->storage_stats();
    return {{"owner_id", lifetime_.id()}, {"ocr", models},
        {"bundle_lease", {{"identity", bundle_.lease->identity()}, {"owners", bundle_.lease.use_count()},
            {"held_file_bytes", bytes.size_bytes}, {"held_capacity_bytes", bytes.capacity_bytes},
            {"model_file_bytes", bytes.model_bytes}, {"image_file_bytes", bytes.image_bytes},
            {"json_file_bytes", bytes.json_bytes}, {"other_file_bytes", bytes.other_bytes},
            {"files", bundle_.lease->file_count()}}},
        {"temporal_state_entries", cache_.assets.size()},
        {"result_cache_entries", cache_.results.size() + cache_.template_results.size()},
        {"decoded_frame_bytes", frame_pixels_ ? frame_pixels_->bgr().size() : 0}};
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
    result.result_cache_entries += ocr_frame_results_.size();
    for (const auto &[key, values] : ocr_frame_results_) {
        result.result_cache_estimated_bytes += key.size();
        for (const auto &value : values)
            result.result_cache_estimated_bytes += sizeof(value) + value.text.size();
    }
    return result;
}
} // namespace wvd::recognition
