#pragma once

#include "recognition/custom.hpp"
#include <string>

namespace wvd::games::vision {
recognition::Handlers native_handlers(const nlohmann::json &aliases,
                                      const std::string &resource_locale);
} // namespace wvd::games::vision
