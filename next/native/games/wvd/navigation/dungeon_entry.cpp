#include "dungeon_entry.hpp"
#include "world_travel.hpp"
#include "games/wvd/vision/location_probes.hpp"
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
    graph.check_policy("navigation", {"wvd-network-retry", "wvd-download", "wvd-story", "wvd-dialogue"});
    const auto inside = C::any({C::image("dungFlag"), C::image("mapFlag"), C::image("chestFlag"), J{{"mode", "combat_active"}}});
    const auto enter = C::image("GotoDung");
    J anchors{vision::city_screen(), C::image("openworldmap"), C::image("returntoTown"), enter};
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
    // known 只用于描述合法选择上下文，不能证明某次菜单转场已经完成。
    // fallback 的确认仅表示本次输入后能重新寻找当前目标，不推进 entry 的步骤号。
    const auto fallback_observed = C::any({known, inside, C::image("worldmapflag")});
    const auto successor_result = [&](std::size_t index) {
        J expected{inside, enter};
        if (index + 1 < steps.size()) {
            const auto &next_step = steps[index + 1];
            // WorldMap由独立导航块确认目的地；EVENT的后继是活动二级页，
            // intoWorldMap是城市入口而非抵达地点的凭据。
            const auto marker = next_step.kind == EntryStep::Kind::Event ? std::string("openworldmap")
                : next_step.kind == EntryStep::Kind::WorldMap ? std::string("worldmapflag") : next_step.target;
            // 同一图片不能既是源页面又当作页面已切换的证据。
            if (!marker.empty() && marker != steps[index].target)
                expected.push_back(C::image(marker));
        }
        return C::any(std::move(expected));
    };
    // 页面优先于旧步骤号。只观察可命名的后继页，不用known宣布前一步完成。
    J convergence{"Entered", "EnterNow"};
    for (std::size_t i = steps.size(); i-- > 0;)
        if (steps[i].kind == EntryStep::Kind::FindAndPress)
            convergence.push_back("Step" + std::to_string(i) + "Found");
        else if (steps[i].kind == EntryStep::Kind::Event)
            convergence.push_back("Step" + std::to_string(i) + "EventReady");
    if (plan.pre_entry()) { convergence.push_back("PreEntry"); convergence.push_back("PreAbsent"); }
    else convergence.push_back("Step0");
    graph.route("Entry", convergence);
    graph.observe("Entered", inside, {"Terminal"});
    graph.click("EnterNow", located_selection(enter), enter, inside, {"Entered"});
    graph.retry_menu_input("EnterNow", vision::menu_retry_ready(C::all({located_selection(enter), C::absent(inside)})));
    if (plan.pre_entry()) {
        const auto pre_target = C::image(*plan.pre_entry());
        J pre_expected{inside, enter};
        if (steps.front().kind == EntryStep::Kind::WorldMap) {
            // preEOT是当前位置的可选直接入口，不要求先倒退回城市地图按钮。
            pre_expected.push_back(C::image("openworldmap"));
            if (steps.size() > 1 && steps[1].target != *plan.pre_entry())
                pre_expected.push_back(C::image(steps[1].target));
        } else if (steps.front().kind == EntryStep::Kind::Event) {
            pre_expected.push_back(C::image("openworldmap"));
        } else if (steps.front().target != *plan.pre_entry())
            pre_expected.push_back(C::image(steps.front().target));
        const auto pre_result = C::any(std::move(pre_expected));
        graph.click("PreEntry", located_selection(pre_target), pre_target, pre_result, {"Entry"});
        graph.retry_menu_input("PreEntry", vision::menu_retry_ready(
            C::all({located_selection(pre_target), C::absent(pre_result)})));
        graph.observe("PreAbsent", C::absent(C::image(*plan.pre_entry())), {"Step0"});
    }
    for (std::size_t i = 0; i < steps.size(); ++i) {
        const auto &step = steps[i];
        if (!std::isfinite(step.interval_seconds) || step.interval_seconds <= 0 || step.interval_seconds > 10)
            throw std::runtime_error("DUNGEON_ENTRY_INTERVAL_INVALID");
        const auto s = "Step" + std::to_string(i);
        const J next = i + 1 < steps.size() ? J{"Step" + std::to_string(i + 1)} : J{"Entered", "EnterNow"};
        if (i + 1 < steps.size() && step.target == steps[i + 1].target)
            throw std::runtime_error("DUNGEON_ENTRY_IDENTICAL_PAGE_NEEDS_CONTRACT:" + step.target);
        if (step.kind == EntryStep::Kind::WorldMap) {
            if (!step.world)
                throw std::runtime_error("DUNGEON_ENTRY_WORLD_MISSING");
            const auto child = graph.append(s + "World", travel_world(*step.world, WorldArrival::DungeonEntrance), next);
            graph.route(s, {"Entered", child});
            continue;
        }
        const auto target = C::image(step.target);
        const bool event = step.kind == EntryStep::Kind::Event;
        const auto goal = event ? C::image("openworldmap") : target;
        const auto result = successor_result(i);
        const auto goal_node = event ? s + "EventReady" : s + "Found";
        // 原 fallback 顺序保留，但每一个动作之前都先重新看当前目标。
        // 上一步已进入郊外时，不再继续执行王城兜底的[1,1]。
        std::size_t fallback_index = 0;
        std::function<std::string(const TaskAction &, const J &)> append_fallback;
        append_fallback = [&](const TaskAction &action, const J &after) -> std::string {
            const auto name = s + "Fallback" + std::to_string(fallback_index++);
            const auto operation = name + "Action";
            J choices{"Entered", "EnterNow"};
            for (std::size_t later = steps.size(); later-- > i + 1;)
                if (steps[later].kind == EntryStep::Kind::FindAndPress)
                    choices.push_back("Step" + std::to_string(later) + "Found");
            choices.push_back(goal_node); choices.push_back(operation);
            graph.route(name, choices);
            const auto seeking = C::all({selection, C::absent(goal)});
            if (const auto *sequence = std::get_if<std::vector<TaskAction>>(&action.value)) {
                J successor = after;
                for (auto child = sequence->rbegin(); child != sequence->rend(); ++child)
                    successor = {append_fallback(*child, successor)};
                graph.route(operation, successor);
            } else if (const auto *point = std::get_if<TaskPoint>(&action.value)) {
                graph.fixed_click(operation, seeking, fallback_observed, *point, after);
                graph.delay_after(operation, static_cast<int>(step.interval_seconds * 1000));
            } else if (const auto *swipe = std::get_if<TaskSwipe>(&action.value)) {
                graph.swipe(operation, seeking, fallback_observed,
                    {swipe->from[0], swipe->from[1], swipe->to[0], swipe->to[1]}, after);
                graph.delay_after(operation, static_cast<int>(step.interval_seconds * 1000));
            } else if (const auto *pattern = std::get_if<TaskPattern>(&action.value)) {
                const auto image = C::image(pattern->name);
                graph.route(operation, {name + "Present", name + "Absent"});
                graph.click(name + "Present", C::all({located_selection(image), C::absent(goal)}),
                    image, fallback_observed, after);
                graph.delay_after(name + "Present", static_cast<int>(step.interval_seconds * 1000));
                graph.observe(name + "Absent", C::all({seeking, C::absent(image)}), after);
            } else graph.observe(operation, seeking, after);
            return name;
        };
        TaskAction fallback = step.fallback;
        if (event) {
            fallback.value = std::vector<TaskAction>{{TaskPoint{1, 1}}, {TaskPattern{"EVENT"}}, step.fallback};
            graph.route(s, {"Entered", s + "EventReady", s + "Find"});
            graph.observe(s + "EventReady", C::image("openworldmap"), next);
        } else {
            J choices{"Entered", "EnterNow"};
            for (std::size_t later = steps.size(); later-- > i;)
                if (steps[later].kind == EntryStep::Kind::FindAndPress)
                    choices.push_back("Step" + std::to_string(later) + "Found");
            choices.push_back(s + "Find");
            graph.route(s, choices);
            graph.click(s + "Found", located_selection(target), target, result, next);
            graph.retry_menu_input(s + "Found", vision::menu_retry_ready(
                C::all({located_selection(target), C::absent(result)})));
            graph.delay_after(s + "Found", static_cast<int>(step.interval_seconds * 1000));
        }
        const auto action = append_fallback(fallback, {s});
        graph.observe(s + "Find", event ? selection : C::all({selection, C::absent(target)}), {action});
        graph.delay_after(s + "Find", static_cast<int>(step.interval_seconds * 1000));
    }
    return graph.finish();
}
}
