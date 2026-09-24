#pragma once

#include "recognition/custom.hpp"

namespace wvd::games::vision {
recognition::Handlers native_handlers(const nlohmann::json &aliases);
} // namespace wvd::games::vision
