#pragma once
#include "buffers.hpp"
#include "recognition.hpp"
#include <json.hpp>

namespace wvd::maafw {
void verify_file(const Bundle &bundle, const std::string &relative);
void verify_bundle(const Bundle &bundle);
Image validate_frame(const contracts::FrameEnvelope &frame, const contracts::FrameIdentity &current,
                     const std::string &revision);
nlohmann::json validate_parameters(const Bundle &bundle, const RecognitionRequest &request,
                                   contracts::Size size);
} // namespace wvd::maafw
