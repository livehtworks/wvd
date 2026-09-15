#pragma once
#include <cstddef>
#include <json.hpp>
#include <optional>

namespace wvd::games::quests {
// 只保存一次性任务的业务回执。Maa 持有流程执行，RunState 持有本对象的生命周期。
class ManualSeparation {
  public:
    enum class Phase { FirstRoute, FirstBack, SecondBack, Rest, Leap, SecondRoute, Completed };
    void route_completed(std::size_t points, std::size_t unit);
    void started_in_city(std::size_t unit);
    void prepare(Phase);
    void transferred(Phase);
    void rested(bool confirmed_payment);
    nlohmann::json summary() const;

  private:
    Phase phase_{Phase::FirstRoute};
    std::optional<Phase> pending_;
};
}
