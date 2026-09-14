#pragma once
#include "quest_catalog.hpp"
#include <array>
#include <optional>
#include <variant>

namespace wvd::games {
using TaskPoint = std::array<int, 2>;
struct TaskSwipe {
    TaskPoint from, to;
};
struct TaskPattern {
    std::string name;
};
// 旧 fallback 列表是顺序动作，不是 Maa next 的识别候选。
struct TaskAction {
    std::variant<std::monostate, TaskPoint, TaskSwipe, TaskPattern, std::vector<TaskAction>> value;
};
struct WorldDestination {
    std::string target;
    std::optional<TaskSwipe> swipe;
    TaskPoint dismiss{550, 1};
};
struct EntryStep {
    enum class Kind { FindAndPress, WorldMap, Event } kind;
    std::string target;
    TaskAction fallback;
    std::optional<WorldDestination> world;
    double interval_seconds;
    bool final_step;
};
struct MapTarget {
    std::string target;
    std::vector<std::optional<TaskSwipe>> swipes;
    // 第三个旧参数按目标类型解释，不能一概当 ROI。
    enum class Hint { None, Position, StairReference, Regions } hint;
    std::optional<TaskPoint> position;
    std::string stair_reference;
    std::vector<std::array<int, 4>> regions;
};

class WvdTaskPlan {
  public:
    static WvdTaskPlan parse(const WvdQuestDefinition &definition);
    nlohmann::json inspect() const;
    const std::vector<EntryStep> &entry_steps() const { return entry_; }
    const std::vector<MapTarget> &route() const { return route_; }
    const std::optional<std::string> &pre_entry() const { return pre_entry_; }
    const std::optional<std::string> &floor() const { return floor_; }
    const std::optional<WorldDestination> &return_destination() const { return return_; }
    const WvdQuestDefinition &definition() const { return definition_; }

  private:
    WvdQuestDefinition definition_;
    std::vector<EntryStep> entry_;
    std::vector<MapTarget> route_;
    std::optional<WorldDestination> return_;
    std::optional<std::string> pre_entry_, floor_;
};
} // namespace wvd::games
