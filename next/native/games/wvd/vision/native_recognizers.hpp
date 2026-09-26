#pragma once

#include "recognition/custom.hpp"
#include "games/wvd/recovery/dialogue_policy.hpp"
#include <string>

namespace wvd::games::vision {
recognition::Handlers native_handlers(const nlohmann::json &aliases,
                                      const std::string &resource_locale,
                                      recovery::DialoguePolicy dialogue_policy = recovery::DialoguePolicy::Default);
} // namespace wvd::games::vision
