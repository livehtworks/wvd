#include "dungeon_entry.hpp"
#include "world_travel.hpp"
#include <algorithm>
#include <cmath>
#include <functional>

namespace wvd::games::navigation {
using J = nlohmann::json;
using C = tasks::PipelineCompiler;
namespace {
void collect_patterns(const TaskAction &action, J &conditions) {
    if (const auto *p = std::get_if<TaskPattern>(&action.value))
        conditions.push_back(C::image(p->name));
    if (const auto *sequence = std::get_if<std::vector<TaskAction>>(&action.value))
        for (const auto &child : *sequence)
            collect_patterns(child, conditions);
}
J any_chunked(J conditions) {
    // 组合识别单层最多 16 项；这是静态表达树分组，不是运行中的节点解释器。
    if (conditions.size() <= 16)
        return C::any(std::move(conditions));
    J groups = J::array();
    for (std::size_t offset = 0; offset < conditions.size(); offset += 16) {
        J chunk = J::array();
        for (auto i = offset; i < std::min(offset + 16, conditions.size()); ++i)
            chunk.push_back(conditions[i]);
        groups.push_back(C::any(std::move(chunk)));
    }
    return any_chunked(std::move(groups));
}
}
tasks::CompiledWorkflow enter_dungeon(const WvdTaskPlan &plan) {
    const auto &steps = plan.entry_steps();
    if (steps.empty() || steps.size() > 64)
        throw std::runtime_error("DUNGEON_ENTRY_STEPS_INVALID");
    C graph("navigation.dungeon_entry");
    const auto inside = C::any({C::image("dungFlag"), C::image("mapFlag"), C::image("chestFlag"), J{{"mode", "combat_active"}}});
    const auto enter = C::image("GotoDung");
    J anchors{C::image("Inn"), C::image("EdgeOfTown"), C::image("openworldmap"), C::image("returntoTown"), enter};
    for (const auto &step : steps) {
        if (step.kind != EntryStep::Kind::WorldMap)
            anchors.push_back(C::image(step.target));
        collect_patterns(step.fallback, anchors);
    }
    if (plan.pre_entry())
        anchors.push_back(C::image(*plan.pre_entry()));
    const auto known = any_chunked(anchors);
    const auto selection = C::all({known, C::absent(inside), C::absent(C::image("worldmapflag"))});
    // 模板点击本身已提供当前选择页的正证据，不再匹配全路线所有候选页。
    // 固定坐标 fallback 仍需 known；两者都排除已入本和世界地图，且不延长帧 TTL。
    const auto located_selection = [&](const J &target) {
        return C::all({target, C::absent(inside), C::absent(C::image("worldmapflag"))});
    };
    const auto post = C::any({known, inside, C::image("worldmapflag")});
    graph.route("Entry", plan.pre_entry() ? J{"Entered", "EnterNow", "PreEntry", "PreAbsent"} : J{"Entered", "EnterNow", "Step0"});
    graph.observe("Entered", inside, {"Terminal"});
    graph.click("EnterNow", located_selection(enter), enter, inside, {"Entered"});
    if (plan.pre_entry()) {
        graph.click("PreEntry", located_selection(C::image(*plan.pre_entry())), C::image(*plan.pre_entry()), post, {"Step0"});
        graph.observe("PreAbsent", C::absent(C::image(*plan.pre_entry())), {"Step0"});
    }
    for (std::size_t i = 0; i < steps.size(); ++i) {
        const auto &step = steps[i];
        if (!std::isfinite(step.interval_seconds) || step.interval_seconds <= 0 || step.interval_seconds > 10)
            throw std::runtime_error("DUNGEON_ENTRY_INTERVAL_INVALID");
        const auto s = "Step" + std::to_string(i);
        const J next = i + 1 < steps.size() ? J{"Step" + std::to_string(i + 1)} : J{"Entered", "EnterNow"};
        if (step.kind == EntryStep::Kind::WorldMap) {
            if (!step.world)
                throw std::runtime_error("DUNGEON_ENTRY_WORLD_MISSING");
            const auto child = graph.append(s + "World", travel_world(*step.world, WorldArrival::DungeonEntrance), next);
            graph.route(s, {"Entered", child});
            continue;
        }
        const auto target = C::image(step.target);
        // fallback 的源数组逐项编成明确后继。缺失的模板只是跳过该动作，不变成成功终点。
        std::size_t fallback_index = 0;
        std::function<std::string(const TaskAction &, const J &)> append_fallback;
        append_fallback = [&](const TaskAction &action, const J &after) -> std::string {
            const auto name = s + "Fallback" + std::to_string(fallback_index++);
            if (const auto *sequence = std::get_if<std::vector<TaskAction>>(&action.value)) {
                J successor = after;
                for (auto child = sequence->rbegin(); child != sequence->rend(); ++child)
                    successor = {append_fallback(*child, successor)};
                graph.route(name, successor);
            } else if (const auto *point = std::get_if<TaskPoint>(&action.value)) {
                graph.fixed_click(name, selection, post, *point, after);
            } else if (const auto *swipe = std::get_if<TaskSwipe>(&action.value)) {
                graph.swipe(name, selection, post, {swipe->from[0], swipe->from[1], swipe->to[0], swipe->to[1]}, after);
            } else if (const auto *pattern = std::get_if<TaskPattern>(&action.value)) {
                const auto image = C::image(pattern->name);
                graph.route(name, {name + "Present", name + "Absent"});
                graph.click(name + "Present", located_selection(image), image, post, after);
                graph.observe(name + "Absent", C::absent(image), after);
            } else
                graph.observe(name, selection, after);
            return name;
        };
        TaskAction fallback = step.fallback;
        const bool event = step.kind == EntryStep::Kind::Event;
        if (event) {
            fallback.value = std::vector<TaskAction>{{TaskPoint{1, 1}}, {TaskPattern{"EVENT"}}, step.fallback};
            graph.route(s, {"Entered", s + "EventReady", s + "Find"});
            graph.observe(s + "EventReady", C::image("openworldmap"), next);
        } else {
            graph.route(s, {"Entered", "EnterNow", s + "Found", s + "Find"});
            graph.click(s + "Found", located_selection(target), target, post, next);
            graph.delay_after(s + "Found", static_cast<int>(step.interval_seconds * 1000));
        }
        const auto action = append_fallback(fallback, {s});
        graph.observe(s + "Find", event ? selection : C::all({selection, C::absent(target)}), {action});
        graph.delay_after(s + "Find", static_cast<int>(step.interval_seconds * 1000));
    }
    return graph.finish();
}
}
