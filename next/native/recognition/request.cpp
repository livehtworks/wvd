#include "request.hpp"
#include <stdexcept>

namespace wvd::recognition {
Request parse_request(const nlohmann::json &value) {
    const auto roi = value.at("roi").get<std::vector<int>>();
    if (roi.size() != 4)
        throw std::runtime_error("ROI_INVALID");
    Request result{value.at("id"), value.at("revision"), {roi[0], roi[1], roi[2], roi[3]}, {}};
    if (result.recognizer_id.empty() || result.parameter_revision.empty())
        throw std::runtime_error("RECO_IDENTITY_INVALID");
    const auto type = value.value("type", std::string("template"));
    if (type == "template")
        result.parameters = TemplateParameters{value.at("image"), value.value("threshold", 0.8)};
    else if (type == "ocr")
        result.parameters = OcrParameters{value.at("expected").get<std::vector<std::string>>()};
    else if (type == "custom") {
        auto binding = value.at("binding").get<std::string>();
        auto parameters = value.at("parameters");
        if (binding.empty() || !parameters.is_object() || parameters.dump().size() > 32768)
            throw std::runtime_error("CUSTOM_PARAMETERS_INVALID");
        result.parameters = CustomParameters{std::move(binding), std::move(parameters)};
    } else
        throw std::runtime_error("RECO_TYPE_NOT_SUPPORTED");
    return result;
}
} // namespace wvd::recognition
