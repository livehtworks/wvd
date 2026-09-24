#pragma once
#include "contracts/behavior.hpp"

namespace wvd::runtime { class BehaviorRegistry; }

namespace wvd::games {
void register_wvd_confirmations(runtime::BehaviorRegistry &registry);
contracts::BehaviorBinding wvd_confirmation_binding();
}
