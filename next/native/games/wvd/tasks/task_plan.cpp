#include "task_plan.hpp"
#include <charconv>
#include <cmath>
#include <sstream>

namespace wvd::games {
namespace {
using J = nlohmann::json;
std::string pattern(const J &value) {
    if (!value.is_string())
        throw std::runtime_error("TASK_PATTERN_TYPE");
    auto name = value.get<std::string>();
    if (name.empty() || name.find("..") != std::string::npos || name.front() == '/' ||
        name.find_first_of("\\:;\r\n\t ") != std::string::npos)
        throw std::runtime_error("TASK_PATTERN_INVALID");
    return name;
}
TaskPoint point(const J &value) {
    if (!value.is_array() || value.size() != 2 || !value[0].is_number_integer() ||
        !value[1].is_number_integer())
        throw std::runtime_error("TASK_POINT_TYPE");
    auto x = value[0].get<std::int64_t>(), y = value[1].get<std::int64_t>();
    if (x < 0 || x >= 900 || y < 0 || y >= 1600)
        throw std::runtime_error("TASK_POINT_BOUNDS");
    return {static_cast<int>(x), static_cast<int>(y)};
}
TaskSwipe swipe(const J &value) {
    if (!value.is_array() || value.size() != 4)
        throw std::runtime_error("TASK_SWIPE_TYPE");
    return {point(J::array({value[0], value[1]})), point(J::array({value[2], value[3]}))};
}
TaskSwipe shell_swipe(const std::string &text) {
    // 只解析固定旧数据的 input swipe 四整数语法，不保存或执行任意 shell。
    std::istringstream input(text);
    std::vector<std::string> words;
    for (std::string token; input >> token;)
        words.push_back(token);
    if (words.size() != 6 || words[0] != "input" || words[1] != "swipe")
        throw std::runtime_error("TASK_COMMAND_UNSUPPORTED");
    J coords = J::array();
    for (std::size_t i = 2; i < words.size(); ++i) {
        int n{};
        const auto &token = words[i];
        auto [end, error] = std::from_chars(token.data(), token.data() + token.size(), n);
        if (error != std::errc{} || end != token.data() + token.size())
            throw std::runtime_error("TASK_COMMAND_UNSUPPORTED");
        coords.push_back(n);
    }
    return swipe(coords);
}
TaskAction action(const J &value, unsigned depth = 0) {
    if (depth > 16)
        throw std::runtime_error("TASK_ACTION_DEPTH");
    if (value.is_null())
        return {std::monostate{}};
    if (value.is_string()) {
        const auto text = value.get<std::string>();
        if (text.starts_with("input"))
            return {shell_swipe(text)};
        return {TaskPattern{pattern(value)}};
    }
    if (!value.is_array() || value.empty())
        throw std::runtime_error("TASK_ACTION_TYPE");
    if (value.size() == 2 && value[0].is_number())
        return {point(value)};
    std::vector<TaskAction> sequence;
    for (const auto &child : value)
        sequence.push_back(action(child, depth + 1));
    return {std::move(sequence)};
}
WorldDestination destination(const J &value, bool returning) {
    if (!value.is_array() || value.empty() || value.size() > 3 || (!returning && value.size() < 2))
        throw std::runtime_error("TASK_WORLD_ARGUMENTS");
    WorldDestination result{pattern(value[0]), {}, {550, 1}};
    if (value.size() > 1 && !value[1].is_null()) {
        if (!value[1].is_string())
            throw std::runtime_error("TASK_WORLD_SWIPE_TYPE");
        result.swipe = shell_swipe(value[1].get<std::string>());
    }
    if (value.size() == 3)
        result.dismiss = point(value[2]);
    return result;
}
const J map_exclusions = {{0, 0, 900, 208},     {0, 1265, 900, 335}, {0, 636, 137, 222},
                          {763, 636, 137, 222}, {336, 208, 228, 77}, {336, 1168, 228, 97}};
MapTarget map_target(const J &value) {
    if (!value.is_array() || value.empty() || value.size() > 3)
        throw std::runtime_error("TASK_TARGET_ARGUMENTS");
    MapTarget result{pattern(value[0]), {}, MapTarget::Hint::None, {}, {}, {}};
    J gestures = value.size() > 1 ? value[1] : J(nullptr);
    if (gestures.is_null())
        gestures = {nullptr,
                    {100, 100, 700, 1200},
                    {400, 1200, 400, 100},
                    {700, 800, 100, 800},
                    {400, 100, 400, 1200},
                    {100, 800, 700, 800}};
    if (gestures.is_string()) {
        const J directions = {{"左上", {{100, 250, 700, 1200}}},
                              {"右上", {{700, 250, 100, 1200}}},
                              {"右下", {{700, 1200, 100, 250}}},
                              {"左下", {{100, 1200, 700, 250}}}};
        auto key = gestures.get<std::string>();
        if (!directions.contains(key))
            throw std::runtime_error("TASK_SWIPE_DIRECTION_UNKNOWN");
        gestures = directions.at(key);
    }
    if (!gestures.is_array() || gestures.empty())
        throw std::runtime_error("TASK_SWIPE_SEQUENCE_INVALID");
    for (const auto &gesture : gestures)
        result.swipes.push_back(gesture.is_null() ? std::nullopt : std::optional(swipe(gesture)));
    J hint = value.size() > 2 ? value[2] : J(nullptr);
    if (result.target == "position" || result.target.starts_with("stair")) {
        result.hint = MapTarget::Hint::Position;
        result.position = point(hint);
    } else if ((result.target == "harken" || result.target == "Bharken") && hint.is_string() &&
               hint != "default") {
        result.hint = MapTarget::Hint::StairReference;
        result.stair_reference = pattern(hint);
    } else {
        if (hint == "default") {
            hint = J::array({{0, 0, 900, 1600}});
            for (const auto &rect : map_exclusions)
                hint.push_back(rect);
        }
        if (result.target == "chest") {
            if (hint.is_null())
                hint = J::array({{0, 0, 900, 1600}});
            if (!hint.is_array())
                throw std::runtime_error("TASK_ROI_TYPE");
            // 原 setter 在 default 之后仍追加 exclusions；保留原顺序，不去重改写语义。
            for (const auto &rect : map_exclusions)
                hint.push_back(rect);
        }
        if (!hint.is_null()) {
            if (!hint.is_array() || hint.empty())
                throw std::runtime_error("TASK_ROI_TYPE");
            result.hint = MapTarget::Hint::Regions;
            for (const auto &rect : hint) {
                if (!rect.is_array() || rect.size() != 4)
                    throw std::runtime_error("TASK_ROI_TYPE");
                for (const auto &n : rect)
                    if (!n.is_number_integer())
                        throw std::runtime_error("TASK_ROI_TYPE");
                const auto x = rect[0].get<std::int64_t>(), y = rect[1].get<std::int64_t>();
                const auto w = rect[2].get<std::int64_t>(), h = rect[3].get<std::int64_t>();
                if (x < 0 || y < 0 || x >= 900 || y >= 1600 || w <= 0 || h <= 0 || w > 900 - x ||
                    h > 1600 - y)
                    throw std::runtime_error("TASK_ROI_BOUNDS");
                result.regions.push_back({static_cast<int>(x), static_cast<int>(y),
                                          static_cast<int>(w), static_cast<int>(h)});
            }
        }
    }
    return result;
}
J swipe_json(const TaskSwipe &s) { return {s.from[0], s.from[1], s.to[0], s.to[1]}; }
J action_json(const TaskAction &a) {
    if (std::holds_alternative<std::monostate>(a.value))
        return {{"kind", "none"}};
    if (auto p = std::get_if<TaskPoint>(&a.value))
        return {{"kind", "point"}, {"point", *p}};
    if (auto s = std::get_if<TaskSwipe>(&a.value))
        return {{"kind", "swipe"}, {"coordinates", swipe_json(*s)}};
    if (auto p = std::get_if<TaskPattern>(&a.value))
        return {{"kind", "pattern"}, {"pattern", p->name}};
    J sequence = J::array();
    for (const auto &child : std::get<std::vector<TaskAction>>(a.value))
        sequence.push_back(action_json(child));
    return {{"kind", "sequence"}, {"actions", sequence}};
}
J world_json(const WorldDestination &w) {
    return {{"target", w.target},
            {"swipe", w.swipe ? swipe_json(*w.swipe) : J(nullptr)},
            {"dismiss", w.dismiss}};
}
} // namespace
WvdTaskPlan WvdTaskPlan::parse(const WvdQuestDefinition &definition) {
    WvdTaskPlan plan;
    plan.definition_ = definition;
    const auto &source = definition.source;
    auto optional_pattern = [&](const char *name) -> std::optional<std::string> {
        return source.contains(name) && !source.at(name).is_null()
                   ? std::optional(pattern(source.at(name)))
                   : std::nullopt;
    };
    plan.pre_entry_ = optional_pattern("_preEOTcheck");
    plan.floor_ = optional_pattern("_FloorCheck");
    if (source.contains("_TARGETINFOLIST") && !source.at("_TARGETINFOLIST").is_null())
        for (const auto &target : source.at("_TARGETINFOLIST"))
            plan.route_.push_back(map_target(target));
    if (source.contains("_RTT") && !source.at("_RTT").is_null())
        plan.return_ = destination(source.at("_RTT"), true);
    if (source.contains("_EOT") && !source.at("_EOT").is_null()) {
        const auto &entries = source.at("_EOT");
        for (std::size_t i = 0; i < entries.size(); ++i) {
            const auto &row = entries[i];
            if (!row.is_array() || row.size() != 4 || row[0] != "press" || !row[3].is_number())
                throw std::runtime_error("TASK_ENTRY_COMMAND_INVALID");
            const auto interval = row[3].get<double>();
            if (!std::isfinite(interval) || interval <= 0)
                throw std::runtime_error("TASK_INTERVAL_INVALID");
            EntryStep step{EntryStep::Kind::FindAndPress, pattern(row[1]), {}, {}, interval,
                           i + 1 == entries.size()};
            if (step.target == "intoWorldMap") {
                step.kind = EntryStep::Kind::WorldMap;
                step.world = destination(row[2], false);
            } else {
                if (step.target == "EVENT")
                    step.kind = EntryStep::Kind::Event;
                step.fallback = action(row[2]);
            }
            plan.entry_.push_back(std::move(step));
        }
    }
    if (definition.type == "dungeon" && (plan.entry_.empty() || plan.route_.empty()))
        throw std::runtime_error("TASK_DUNGEON_ROUTE_EMPTY");
    return plan;
}
WvdTaskPlan WvdTaskPlan::with_route(const J &targets) const {
    if (!targets.is_array() || targets.empty() || targets.size() > 64)
        throw std::runtime_error("TASK_LOCAL_ROUTE_INVALID");
    auto plan = *this;
    plan.route_.clear();
    for (const auto &target : targets)
        plan.route_.push_back(map_target(target));
    return plan;
}
WvdTaskPlan WvdTaskPlan::with_last_harken_arrival() const {
    if (route_.empty() || route_.back().target != "position")
        throw std::runtime_error("TASK_HARKEN_ARRIVAL_REQUIRES_POSITION");
    auto plan = *this;
    plan.route_.back().harken_arrival = true;
    return plan;
}
WvdTaskPlan WvdTaskPlan::with_entry(const J &steps) const {
    if (!steps.is_array() || steps.empty() || steps.size() > 64)
        throw std::runtime_error("TASK_LOCAL_ENTRY_INVALID");
    auto local = definition_;
    local.source["_EOT"] = steps;
    auto plan = *this;
    plan.entry_ = parse(local).entry_;
    // 局部编译参数不回写源任务树；检查报告仍可追溯目录没有声明 EOT 的事实。
    return plan;
}
WvdTaskPlan WvdTaskPlan::with_floor(const std::string &image) const {
    auto plan = *this;
    plan.floor_ = pattern(image);
    return plan;
}
nlohmann::json WvdTaskPlan::inspect() const {
    J entries = J::array(), targets = J::array();
    for (const auto &step : entry_)
        entries.push_back({{"kind", step.kind == EntryStep::Kind::WorldMap ? "world_map"
                                    : step.kind == EntryStep::Kind::Event  ? "event"
                                                                           : "find_and_press"},
                           {"target", step.target},
                           {"fallback", action_json(step.fallback)},
                           {"world", step.world ? world_json(*step.world) : J(nullptr)},
                           {"interval_seconds", step.interval_seconds},
                           {"final_step", step.final_step}});
    for (const auto &target : route_) {
        J gestures = J::array();
        for (const auto &s : target.swipes)
            gestures.push_back(s ? swipe_json(*s) : J(nullptr));
        targets.push_back(
            {{"target", target.target},
             {"swipes", gestures},
             {"hint_kind", target.hint == MapTarget::Hint::Position         ? "position"
                           : target.hint == MapTarget::Hint::StairReference ? "stair_reference"
                           : target.hint == MapTarget::Hint::Regions        ? "regions"
                                                                            : "none"},
             {"position", target.position ? J(*target.position) : J(nullptr)},
             {"stair_reference", target.stair_reference},
             {"regions", target.regions},
             {"harken_arrival", target.harken_arrival}});
    }
    // 解析产物不是可运行 Pipeline。特别是 quest 的代码分支不能由这里伪造成功入口。
    return {{"task_id", definition_.id},
            {"legacy_type", definition_.type},
            {"source", definition_.source},
            {"entry_steps", entries},
            {"route", targets},
            {"pre_entry", pre_entry_ ? J(*pre_entry_) : J(nullptr)},
            {"floor", floor_ ? J(*floor_) : J(nullptr)},
            {"return", return_ ? world_json(*return_) : J(nullptr)},
            {"requires_special_case", definition_.type == "quest"},
            {"pipeline_available", false},
            {"remaining", "BUSINESS_ACTIONS_AND_PIPELINE_COMPILER_REQUIRED"}};
}
} // namespace wvd::games
