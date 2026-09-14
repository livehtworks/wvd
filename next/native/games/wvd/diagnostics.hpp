#pragma once
#include "runtime/behavior_registry.hpp"

namespace wvd::games {
void register_wvd_confirmations(runtime::BehaviorRegistry &registry);
contracts::BehaviorBinding wvd_confirmation_binding();
}
