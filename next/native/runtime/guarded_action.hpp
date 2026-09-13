#pragma once
#include "maafw/gateway.hpp"

namespace wvd::runtime {
class GuardedAction {
  public:
    static bool execute(maafw::Context &context, devices::InputGate &gate,
                        storage::EventJournal &events, const nlohmann::json &parameters);
};
} // namespace wvd::runtime
