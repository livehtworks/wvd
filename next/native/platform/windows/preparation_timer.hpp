#pragma once
#include <chrono>
#include <optional>
#include <functional>
#include <json.hpp>
#include <windows.h>

namespace wvd::platform {
using PreparationObserver = std::function<void(const char *, const char *, const nlohmann::json &)>;
// One sample per preparation phase; no polling or ownership of runtime objects.
class PreparationTimer {
  public:
    explicit PreparationTimer(const char *phase = nullptr, PreparationObserver observer = {})
        : wall_(std::chrono::steady_clock::now()), cpu_(thread_cpu()),
          phase_(phase), observer_(std::move(observer)) {
        if (phase_ && observer_) observer_(phase_, "started", nlohmann::json::object());
    }
    nlohmann::json sample() const {
        const auto end = thread_cpu();
        nlohmann::json result{{"wall_ms", std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - wall_).count()},
                {"thread_cpu_ms", cpu_ && end ? nlohmann::json(double(*end - *cpu_) / 10000)
                                               : nlohmann::json(nullptr)}};
        if (!reported_ && phase_ && observer_) {
            observer_(phase_, "completed", result);
            reported_ = true;
        }
        return result;
    }
  private:
    static std::optional<std::uint64_t> thread_cpu() {
        FILETIME created{}, exited{}, kernel{}, user{};
        if (!GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user)) return {};
        return ((std::uint64_t(kernel.dwHighDateTime) << 32) | kernel.dwLowDateTime) +
               ((std::uint64_t(user.dwHighDateTime) << 32) | user.dwLowDateTime);
    }
    std::chrono::steady_clock::time_point wall_;
    std::optional<std::uint64_t> cpu_;
    const char *phase_{};
    PreparationObserver observer_;
    mutable bool reported_{};
};
} // namespace wvd::platform
