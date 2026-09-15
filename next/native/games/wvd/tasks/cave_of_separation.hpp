#pragma once
#include "dungeon_route.hpp"
#include "games/wvd/quests/cave_of_separation.hpp"
#include "runtime/run_coordinator.hpp"
#include <array>

namespace wvd::games::tasks {
using CaveOfSeparationSegment = quests::CaveOfSeparation::Segment;
recovery::DialoguePolicy cave_of_separation_dialogue(CaveOfSeparationSegment segment);
// 仅承接源码扩展，不新增或替换基础58项目录中的任务。
WvdTaskPlan cave_of_separation_plan(const WvdQuestDefinition &definition, CaveOfSeparationSegment segment);
CompiledWorkflow cave_of_separation_segment(const WvdQuestDefinition &definition,
    const nlohmann::json &profile, const std::set<std::string> &images,
    CaveOfSeparationSegment segment, bool allow_download = true);
// 调用前将六个独立图发布到同一个封存 revision，各 Session 保留自己的对话绑定；
// 任何单个 Session 都不能同时包含去程和回程策略。
void configure_cave_of_separation_units(runtime::RunDefinition &definition,
    const std::array<runtime::SessionDefinition, quests::CaveOfSeparation::segments_per_cycle> &segments,
    std::size_t cycles = 1);
}
