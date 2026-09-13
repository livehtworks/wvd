#pragma once
#include "devices/input_gate.hpp"
#include <MaaFramework/MaaAPI.h>
#include <atomic>
#include <functional>

namespace wvd::maafw {
struct CallbackActivity {
    std::atomic<unsigned> count{};
    std::function<void(const std::string &)> failure;
};
class CallbackScope {
  public:
    explicit CallbackScope(CallbackActivity &activity) : activity_(activity) {
        ++activity_.count;
    }
    ~CallbackScope() {
        --activity_.count;
    }

  private:
    CallbackActivity &activity_;
};
class GuardedController {
  public:
    GuardedController(devices::InputGate &gate, CallbackActivity &activity);
    MaaCustomControllerCallbacks *callbacks() {
        return &callbacks_;
    }

  private:
    template <class F> static MaaBool invoke(void *argument, F &&function) noexcept {
        auto &self = *static_cast<GuardedController *>(argument);
        CallbackScope scope(self.activity_);
        try {
            return function(self);
        } catch (...) {
            try {
                self.activity_.failure("CONTROLLER_CALLBACK_EXCEPTION");
            } catch (...) {
            }
            return false;
        }
    }
    bool input(contracts::Command command) {
        return gate_.execute(command);
    }
    devices::InputGate &gate_;
    CallbackActivity &activity_;
    MaaCustomControllerCallbacks callbacks_{};
    std::atomic<bool> connected_{false};
};
} // namespace wvd::maafw
