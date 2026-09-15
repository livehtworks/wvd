#include "recognition.hpp"
#include "buffers.hpp"
#include "gateway.hpp"
#include "preflight.hpp"
#include "platform/windows/runtime_files.hpp"
#include <cmath>
#include <mutex>
#include <set>

namespace wvd::maafw {
using contracts::RecognitionOutcome;
namespace {
void require(bool condition, const char *error) {
    if (!condition)
        throw std::runtime_error(error);
}
bool valid_box(const contracts::Box &b, contracts::Size size) {
    return b.x >= 0 && b.y >= 0 && b.width > 0 && b.height > 0 && b.width <= size.width &&
           b.height <= size.height && b.x <= size.width - b.width && b.y <= size.height - b.height;
}
void read_detail(MaaTasker *tasker, contracts::Observation &result) {
    auto entry = string_buffer(), node_name = string_buffer(), algorithm = string_buffer(),
         details = string_buffer();
    bool direct = result.engine_reco_id != 0;
    MaaBool completed{};
    if (!direct) {
        MaaSize count = 0;
        MaaStatus status{};
        require(MaaTaskerGetTaskDetail(tasker, result.engine_task_id, entry.get(), nullptr, &count,
                                       &status) &&
                    count == 1,
                "TASK_DETAIL_INVALID");
        MaaNodeId node{};
        require(MaaTaskerGetTaskDetail(tasker, result.engine_task_id, entry.get(), &node, &count,
                                       &status) &&
                    count == 1 && status == result.engine_status,
                "TASK_DETAIL_INCONSISTENT");
        MaaActId action{};
        require(MaaTaskerGetNodeDetail(tasker, node, node_name.get(), &result.engine_reco_id,
                                       &action, &completed) &&
                    result.engine_reco_id != MaaInvalidId,
                "NODE_DETAIL_INVALID");
    }
    MaaBool hit{};
    MaaRect box{};
    require(MaaTaskerGetRecognitionDetail(tasker, result.engine_reco_id, node_name.get(),
                                          algorithm.get(), &hit, &box, details.get(), nullptr,
                                          nullptr),
            "RECO_DETAIL_UNAVAILABLE");
    auto parsed = nlohmann::json::parse(MaaStringBufferGet(details.get()));
    if (std::string(MaaStringBufferGet(algorithm.get())) == "Custom") {
        // 固定 SDK 将回调详情放在 detail 字段；三态来自我们声明的版本契约。
        require(parsed.contains("all") && parsed["all"].is_array() && parsed["all"].size() == 1,
                "CUSTOM_DETAIL_INVALID");
        auto custom = parsed["all"][0].at("detail");
        if (custom.is_string())
            custom = nlohmann::json::parse(custom.get<std::string>());
        require(custom.is_object() && custom.value("schema", 0) == 1, "CUSTOM_DETAIL_INVALID");
        result.evidence = custom;
        result.action_eligible = custom.value("action_eligible", true);
        const auto outcome = custom.at("outcome").get<std::string>();
        if (outcome == "Error")
            throw std::runtime_error(custom.value("error", std::string("CUSTOM_RECO_ERROR")));
        require((outcome == "Hit" || outcome == "NoHit") && bool(hit) == (outcome == "Hit"),
                "CUSTOM_DETAIL_INCONSISTENT");
        result.outcome = hit ? RecognitionOutcome::Hit : RecognitionOutcome::NoHit;
        if (hit) {
            contracts::Box found{box.x, box.y, box.width, box.height};
            require(valid_box(found, result.basis.recognition_size), "CUSTOM_BOX_INVALID");
            result.box = found;
            if (custom.value("target", false))
                result.center =
                    contracts::Point{found.x + found.width / 2, found.y + found.height / 2};
        }
        return;
    }
    require(parsed.is_object() && !parsed.contains("error") && parsed.contains("all") &&
                parsed["all"].is_array() && parsed.contains("filtered") &&
                parsed["filtered"].is_array(),
            "RECO_DETAIL_INVALID");
    require(bool(hit) == !parsed["filtered"].empty() && (direct || bool(completed) == bool(hit)),
            "RECO_DETAIL_INCONSISTENT");
    for (const auto &value : parsed["filtered"]) {
        auto rectangle = value.at("box").get<std::vector<int>>();
        require(rectangle.size() == 4, "RECO_BOX_INVALID");
        contracts::RecognitionMatch match{{rectangle[0], rectangle[1], rectangle[2], rectangle[3]},
                                          value.at("score").get<double>(),
                                          value.value("text", std::string{})};
        require(valid_box(match.box, result.basis.recognition_size) && std::isfinite(match.score),
                "RECO_MATCH_INVALID");
        result.matches.push_back(std::move(match));
    }
    if (hit) {
        contracts::Box found{box.x, box.y, box.width, box.height};
        require(valid_box(found, result.basis.recognition_size), "RECO_BOX_INVALID");
        result.box = found;
        result.center = contracts::Point{found.x + found.width / 2, found.y + found.height / 2};
    }
    // NoHit 只能来自有效资源、成功的原生识别任务和一致的识别详情；不使用根业务终态反推。
    result.outcome = hit ? RecognitionOutcome::Hit : RecognitionOutcome::NoHit;
}
} // namespace

RecognitionRequest parse_recognition_request(const nlohmann::json &value) {
    const auto roi = value.at("roi").get<std::vector<int>>();
    require(roi.size() == 4, "ROI_INVALID");
    RecognitionRequest result{
        value.at("id"), value.at("revision"), {roi[0], roi[1], roi[2], roi[3]}, {}};
    require(!result.recognizer_id.empty() && !result.parameter_revision.empty(),
            "RECO_IDENTITY_INVALID");
    const auto type = value.value("type", std::string("template"));
    if (type == "template")
        result.parameters = TemplateParameters{value.at("image"), value.value("threshold", 0.8)};
    else if (type == "ocr")
        result.parameters = OcrParameters{value.at("expected").get<std::vector<std::string>>()};
    else if (type == "custom") {
        auto params = value.at("parameters");
        auto binding = value.at("binding").get<std::string>();
        require(!binding.empty() && params.is_object() && params.dump().size() <= 32768,
                "CUSTOM_PARAMETERS_INVALID");
        result.parameters = RecognitionRequest::CustomParameters{binding, params};
    } else
        throw std::runtime_error("RECO_TYPE_NOT_SUPPORTED");
    return result;
}

struct OfflineRecognizer::Impl {
    std::mutex mutex;
    std::string initialization_error;
    MaaGateway gateway;
    explicit Impl(Bundle value) : gateway(std::move(value)) {
        try {
            gateway.initialize();
        } catch (const std::filesystem::filesystem_error &) {
            initialization_error = "RESOURCE_IO_ERROR";
        } catch (const std::exception &e) {
            initialization_error = e.what();
        } catch (...) {
            initialization_error = "INITIALIZATION_EXCEPTION";
        }
    }
    // 离线入口与执行会话复用同一 Gateway 所有权，不保留第二套资源生命周期。
};

OfflineRecognizer::OfflineRecognizer(Bundle bundle)
    : impl_(std::make_unique<Impl>(std::move(bundle))) {}
OfflineRecognizer::~OfflineRecognizer() = default;
nlohmann::json OfflineRecognizer::bundle_status() const { return impl_->gateway.bundle_status(); }

contracts::Observation OfflineRecognizer::evaluate(const contracts::FrameEnvelope &frame,
                                                   const contracts::FrameIdentity &current,
                                                   const RecognitionRequest &request) {
    std::lock_guard lock(impl_->mutex);
    if (impl_->initialization_error.empty())
        return impl_->gateway.recognize(frame, current, request);
    contracts::Observation result;
    result.basis = frame.identity;
    result.recognizer_id = request.recognizer_id;
    result.parameter_revision = request.parameter_revision;
    result.error_stage = "initialization";
    result.error_code = impl_->initialization_error;
    return result;
}

contracts::Observation MaaGateway::recognize(const contracts::FrameEnvelope &frame,
                                             const contracts::FrameIdentity &current,
                                             const RecognitionRequest &request,
                                             MaaContext *context) {
    std::lock_guard direct_lock(direct_recognition_mutex_);
    struct InvocationCleanup {
        std::mutex &mutex;
        std::optional<VerifiedInvocation> &active;
        ~InvocationCleanup() {
            std::lock_guard lock(mutex);
            active.reset();
        }
    } invocation_cleanup{recognition_mutex_, verified_invocation_};
    contracts::Observation result;
    result.basis = frame.identity;
    result.recognizer_id = request.recognizer_id;
    result.parameter_revision = request.parameter_revision;
    result.error_stage = "initialization";
    try {
        auto checkpoint = std::chrono::steady_clock::now();
        auto stage = [&](const char *name) {
            auto now = std::chrono::steady_clock::now();
            result.timing_ms[name] =
                std::chrono::duration<double, std::milli>(now - checkpoint).count();
            checkpoint = now;
        };
        require(initialized_ && !hooks_.cancelled() && !integrity_failed_, "SESSION_NOT_RUNNING");
        result.error_stage = "frame_preflight";
        auto image = validate_frame(frame, current, bundle_.revision);
        stage("frame_decode_preflight");
        result.error_stage = "resource_preflight";
        // Custom 通过一次性凭据复用这次检查。SDK 内置识别没有 Custom 回调，
        // 在 Node.Recognition.Starting 检查一次；这里不再重复遍历目录。
        if (std::holds_alternative<RecognitionRequest::CustomParameters>(request.parameters))
            verify_bundle(bundle_);
        stage("bundle_verification");
        result.error_stage = "parameter_preflight";
        if (const auto *custom =
                std::get_if<RecognitionRequest::CustomParameters>(&request.parameters))
            require(recognitions_.contains(custom->binding), "CUSTOM_RECO_NOT_REGISTERED");
        auto parameters = validate_parameters(bundle_, request, frame.identity.recognition_size);
        if (const auto *custom =
                std::get_if<RecognitionRequest::CustomParameters>(&request.parameters)) {
            std::lock_guard lock(recognition_mutex_);
            auto token = platform::unique_id();
            verified_invocation_ = VerifiedInvocation{
                token,       custom->binding,           parameters.at("custom_recognition_param"),
                request.roi, ++recognition_invocation_, false};
            parameters["custom_recognition_param"]["_wvd_verified_invocation"] = token;
        }
        stage("parameter_preflight");
        result.error_stage = "native_recognition";
        const char *type = std::holds_alternative<TemplateParameters>(request.parameters)
                               ? "TemplateMatch"
                           : std::holds_alternative<OcrParameters>(request.parameters) ? "OCR"
                                                                                       : "Custom";
        if (context) {
            result.engine_task_id = MaaContextGetTaskId(context);
            result.engine_reco_id = MaaContextRunRecognitionDirect(
                context, type, parameters.dump().c_str(), image.get());
            require(result.engine_reco_id != MaaInvalidId, "RECO_NATIVE_FAILED");
            result.engine_status = MaaTaskerStatus(tasker_.get(), result.engine_task_id);
        } else {
            result.engine_task_id = MaaTaskerPostRecognition(
                tasker_.get(), type, parameters.dump().c_str(), image.get());
            require(result.engine_task_id != MaaInvalidId, "RECO_POST_FAILED");
            result.engine_status = MaaTaskerWait(tasker_.get(), result.engine_task_id);
            require(result.engine_status == MaaStatus_Succeeded, "RECO_NATIVE_FAILED");
        }
        if (integrity_failed_)
            throw std::runtime_error(integrity_error());
        result.error_stage = "recognition_detail";
        stage("native_recognition_including_custom_preflight");
        read_detail(tasker_.get(), result);
        stage("detail_conversion");
        result.error_stage.clear();
    } catch (const std::filesystem::filesystem_error &) {
        result.error_code = "RESOURCE_IO_ERROR";
    } catch (const nlohmann::json::exception &) {
        result.error_code = "RECO_DETAIL_INVALID";
    } catch (const std::exception &e) {
        result.error_code = e.what();
    } catch (...) {
        result.error_code = "RECO_UNKNOWN_EXCEPTION";
    }
    if (!result.error_code.empty()) {
        result.outcome = RecognitionOutcome::Error;
        result.box.reset();
        result.center.reset();
        result.matches.clear();
    }
    return result;
}
} // namespace wvd::maafw
