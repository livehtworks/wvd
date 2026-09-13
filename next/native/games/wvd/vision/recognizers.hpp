#pragma once
#include "runtime/behavior_registry.hpp"

namespace wvd::games::vision {
void register_wvd(runtime::BehaviorRegistry &registry);
contracts::BehaviorBinding binding(const nlohmann::json &aliases);
} // namespace wvd::games::vision
