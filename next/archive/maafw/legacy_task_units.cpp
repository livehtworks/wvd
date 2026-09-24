#include "runtime/run_coordinator.hpp"
#include "games/wvd/tasks/bounty_cycle.hpp"
#include "games/wvd/tasks/bull_cave.hpp"
#include "games/wvd/tasks/cave_of_separation.hpp"
#include "games/wvd/tasks/fishing_supply.hpp"
#include "games/wvd/tasks/fordraig.hpp"
#include "games/wvd/tasks/golden_chest.hpp"
#include "games/wvd/tasks/manual_separation.hpp"
#include "games/wvd/tasks/repel_forces.hpp"
#include "games/wvd/tasks/sandman.hpp"
#include "games/wvd/tasks/sleep_visits.hpp"

namespace wvd::games::tasks {
namespace {
void repeat(runtime::RunDefinition &definition, std::size_t units,
            std::size_t limit, const char *error) {
    if (!units || units > limit || definition.max_business_units != 1 ||
        !definition.continuation_units.empty()) throw std::runtime_error(error);
    definition.max_business_units = units;
    definition.continuation_units.assign(units - 1, definition.initial);
}
}
void configure_bounty_units(runtime::RunDefinition &definition, bool hands, std::size_t cycles) {
    const auto units = hands ? 4u : 3u;
    if (!cycles || cycles > 256 / units)
        throw std::runtime_error("BOUNTY_UNIT_BUDGET_INVALID");
    repeat(definition, cycles * units, 256, "BOUNTY_UNIT_BUDGET_INVALID");
}
void configure_bull_cave_units(runtime::RunDefinition &definition, bool rest, std::size_t cycles) {
    const auto units = rest ? 3u : 2u;
    if (!cycles || cycles > 256 / units)
        throw std::runtime_error("BULL_CAVE_UNIT_BUDGET_INVALID");
    repeat(definition, cycles * units, 256, "BULL_CAVE_UNIT_BUDGET_INVALID");
}
void configure_fishing_units(runtime::RunDefinition &definition, std::size_t units) {
    repeat(definition, units, 256, "FISHING_UNIT_BUDGET_INVALID");
}
void configure_golden_chest_units(runtime::RunDefinition &definition, std::size_t cycles) {
    if (!cycles || cycles > 128)
        throw std::runtime_error("GOLDEN_UNIT_BUDGET_INVALID");
    repeat(definition, cycles * 2, 256, "GOLDEN_UNIT_BUDGET_INVALID");
}
void configure_manual_separation_units(runtime::RunDefinition &definition) {
    repeat(definition, 2, 2, "MANUAL_SEPARATION_UNITS_ALREADY_CONFIGURED");
}
void configure_repel_forces_units(runtime::RunDefinition &definition,
                                  const nlohmann::json &profile) {
    repeat(definition, quests::RepelForces::rounds(profile) + 2, 256,
           "REPEL_UNITS_ALREADY_CONFIGURED");
}
void configure_sandman_units(runtime::RunDefinition &definition, std::size_t visits) {
    if (!visits || visits > 128)
        throw std::runtime_error("SANDMAN_UNIT_BUDGET_INVALID");
    repeat(definition, visits * 2, 256, "SANDMAN_UNIT_BUDGET_INVALID");
}
void configure_sleep_units(runtime::RunDefinition &definition) {
    repeat(definition, quests::SleepVisits::units, quests::SleepVisits::units,
           "SLEEP_UNITS_ALREADY_CONFIGURED");
}
void configure_fordraig_units(runtime::RunDefinition &definition,
    const std::vector<runtime::SessionDefinition> &stages, std::size_t cycles) {
    constexpr auto units = quests::FordraigCycle::units_per_cycle;
    if (!cycles || cycles > 256 / units || stages.size() != units ||
        definition.max_business_units != 1 || !definition.continuation_units.empty())
        throw std::runtime_error("FORDRAIG_UNIT_BUDGET_INVALID");
    for (const auto &stage : stages)
        if (stage.entry.empty() || stage.checkpoint_node.empty() ||
            stage.time_limit <= std::chrono::milliseconds::zero() ||
            stage.time_limit > std::chrono::seconds{1800})
            throw std::runtime_error("FORDRAIG_SEGMENT_DEFINITION_INVALID");
    definition.initial = stages.front();
    definition.max_business_units = cycles * units;
    definition.continuation_units.reserve(definition.max_business_units - 1);
    for (std::size_t unit = 1; unit < definition.max_business_units; ++unit)
        definition.continuation_units.push_back(stages[unit % units]);
}
void configure_cave_of_separation_units(runtime::RunDefinition &definition,
    const std::array<runtime::SessionDefinition, quests::CaveOfSeparation::segments_per_cycle> &segments,
    std::size_t cycles) {
    constexpr auto count = quests::CaveOfSeparation::segments_per_cycle;
    if (!cycles || cycles > 256 / count || definition.max_business_units != 1 ||
        !definition.continuation_units.empty())
        throw std::runtime_error("COS_UNIT_BUDGET_INVALID");
    for (std::size_t index = 0; index < count; ++index) {
        const auto &segment = segments[index];
        if (segment.entry.empty() || segment.terminal_node.empty() ||
            segment.checkpoint_node.empty() ||
            segment.time_limit <= std::chrono::milliseconds{0} ||
            segment.time_limit > std::chrono::seconds{1800} || segment.lifecycle)
            throw std::runtime_error("COS_SESSION_DEFINITION_INVALID");
        if (segment.bundle.revision.empty() ||
            segment.bundle.revision != segments.front().bundle.revision)
            throw std::runtime_error("COS_SESSION_REVISION_MISMATCH");
        const auto policy = recovery::dialogue_policy_name(
            cave_of_separation_dialogue(static_cast<CaveOfSeparationSegment>(index)));
        std::size_t vision_bindings = 0;
        for (const auto &binding : segment.recognitions) {
            if (binding.name != "WvdVision") continue;
            ++vision_bindings;
            if (!binding.parameters.is_object() ||
                binding.parameters.value("dialogue_task", "") != policy)
                throw std::runtime_error("COS_SESSION_DIALOGUE_MISMATCH");
        }
        if (vision_bindings != 1) throw std::runtime_error("COS_SESSION_DIALOGUE_MISSING");
    }
    definition.initial = segments.front();
    definition.max_business_units = cycles * count;
    for (std::size_t unit = 1; unit < definition.max_business_units; ++unit)
        definition.continuation_units.push_back(segments[unit % count]);
}
} // namespace wvd::games::tasks
