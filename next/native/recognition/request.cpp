#include "request.hpp"
#include <stdexcept>
#include <algorithm>
#include <cmath>

namespace wvd::recognition {
void validate_ocr_parameters(const OcrParameters &p, bool positional) {
    if (p.language != "en" && p.language != "zh-Hant")
        throw std::runtime_error("OCR_LANGUAGE_UNSUPPORTED");
    if (p.match != "exact" && p.match != "contains")
        throw std::runtime_error("OCR_MATCH_INVALID");
    if (!std::isfinite(p.threshold) || p.threshold < 0 || p.threshold > 1)
        throw std::runtime_error("OCR_THRESHOLD_INVALID");
    if (p.expected_text.empty() || p.expected_text.size() > 32)
        throw std::runtime_error("OCR_EXPECTED_INVALID");
    for (const auto &text : p.expected_text) {
        if (text.empty() || text.size() > 1024 || text.find('\0') != std::string::npos)
            throw std::runtime_error("OCR_EXPECTED_INVALID");
        if (p.language == "en" && !std::all_of(text.begin(), text.end(), [](unsigned char c) { return c < 128; }))
            throw std::runtime_error("OCR_LANGUAGE_UNSUPPORTED");
    }
    if (positional && (p.match != "exact" || !p.unique))
        throw std::runtime_error("OCR_POSITION_REQUIRES_EXACT_UNIQUE");
}
OcrParameters parse_ocr_parameters(const nlohmann::json &value) {
    OcrParameters p{value.at("expected").get<std::vector<std::string>>(),
        value.value("language", std::string("en")), value.value("match", std::string("contains")),
        value.value("threshold", 0.3), value.value("unique", false)};
    validate_ocr_parameters(p);
    return p;
}
nlohmann::json ocr_parameters_json(const OcrParameters &p) {
    return {{"expected", p.expected_text}, {"language", p.language}, {"match", p.match},
        {"threshold", p.threshold}, {"unique", p.unique}};
}
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
        result.parameters = parse_ocr_parameters(value);
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
