#include "recognition.hpp"
#include "buffers.hpp"
#include "gateway.hpp"
#include "preflight.hpp"
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
    contracts::Observation result;
    result.basis = frame.identity;
    result.recognizer_id = request.recognizer_id;
    result.parameter_revision = request.parameter_revision;
    result.error_stage = "initialization";
    try {
        require(initialized_ && !hooks_.cancelled(), "SESSION_NOT_RUNNING");
        result.error_stage = "frame_preflight";
        auto image = validate_frame(frame, current, bundle_.revision);
        result.error_stage = "resource_preflight";
        // 资源是发布快照，不允许运行期间替换磁盘文件后继续沿用引擎缓存。
        verify_bundle(bundle_);
        result.error_stage = "parameter_preflight";
        auto parameters = validate_parameters(bundle_, request, frame.identity.recognition_size);
        result.error_stage = "native_recognition";
        const char *type = std::holds_alternative<TemplateParameters>(request.parameters)
                               ? "TemplateMatch"
                               : "OCR";
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
        result.error_stage = "recognition_detail";
        read_detail(tasker_.get(), result);
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
