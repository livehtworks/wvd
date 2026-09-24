#pragma once

#include "contracts/recognition.hpp"
#include <filesystem>
#include <json.hpp>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace wvd::platform {
class BundleLease;
}

namespace wvd::recognition {
struct ResourceFile {
    std::string relative_path;
    std::string sha256;
};

struct Bundle {
    std::filesystem::path root;
    std::string revision;
    std::vector<ResourceFile> files;
    std::filesystem::path snapshot_parent;
    std::shared_ptr<const platform::BundleLease> lease;
};

struct TemplateParameters {
    std::string image;
    double threshold{0.8};
};

struct OcrParameters {
    std::vector<std::string> expected_text;
};

struct CustomParameters {
    std::string binding;
    nlohmann::json parameters = nlohmann::json::object();
};

struct Request {
    std::string recognizer_id;
    std::string parameter_revision;
    contracts::Box roi;
    std::variant<TemplateParameters, OcrParameters, CustomParameters> parameters;
};

Request parse_request(const nlohmann::json &value);
} // namespace wvd::recognition
