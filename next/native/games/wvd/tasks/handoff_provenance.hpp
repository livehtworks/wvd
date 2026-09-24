#pragma once
#include <json.hpp>
#include <string>

namespace wvd::games::tasks {
std::string digest_handoff_json(const nlohmann::json &value);
void validate_handoff_source(const nlohmann::json &source,
                             const nlohmann::json &profile);
bool handoff_has_unconfirmed_effect(const nlohmann::json &business);
} // namespace wvd::games::tasks
