#pragma once

#include "contracts/recognition.hpp"
#include <filesystem>
#include <json.hpp>
#include <memory>
#include <variant>

namespace wvd::maafw {
struct ResourceFile {
    std::string relative_path, sha256;
};
struct Bundle {
    std::filesystem::path root;
    std::string revision;
    std::vector<ResourceFile> files;
};
struct TemplateParameters {
    std::string image;
    double threshold{0.8};
};
struct OcrParameters {
    // 此适配边界只接受文字，不接受用户正则，避免无效正则被 SDK 折叠成 NoHit。
    std::vector<std::string> expected_text;
};
struct RecognitionRequest {
    std::string recognizer_id, parameter_revision;
    contracts::Box roi;
    struct CustomParameters {
        std::string binding;
        nlohmann::json parameters = nlohmann::json::object();
    };
    std::variant<TemplateParameters, OcrParameters, CustomParameters> parameters;
};

// M2 的离线识别入口：只绑定 Resource，不创建任何 Controller。
// 同一对象串行调用；evaluate 等原生任务静止后才返回，不宣称底层等待可中断。
// 与 ExecutionSession 共用 MaaGateway；此入口用于独立样本，不在 HTTP I/O 线程等待原生调用。
class OfflineRecognizer {
  public:
    explicit OfflineRecognizer(Bundle bundle);
    ~OfflineRecognizer();
    OfflineRecognizer(const OfflineRecognizer &) = delete;
    OfflineRecognizer &operator=(const OfflineRecognizer &) = delete;
    contracts::Observation evaluate(const contracts::FrameEnvelope &frame,
                                    const contracts::FrameIdentity &current,
                                    const RecognitionRequest &request);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace wvd::maafw
