#include "run_builder.hpp"
#include "bull_cave.hpp"
#include "dark_light.hpp"
#include "dungeon_iteration.hpp"
#include "fishing_supply.hpp"
#include "fortress_trap.hpp"
#include "giant.hpp"
#include "gold_income.hpp"
#include "golden_chest.hpp"
#include "handoff_provenance.hpp"
#include "manual_separation.hpp"
#include "mining.hpp"
#include "repel_forces.hpp"
#include "sandman.hpp"
#include "sleep_visits.hpp"
#include "steel_trial.hpp"
#include "task_plan.hpp"
#include "games/wvd/quests/repel_forces.hpp"
#include "games/wvd/quests/sleep_visits.hpp"
#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace wvd::games::tasks {
using namespace std::chrono_literals;

CompiledWorkflow build_task_workflow(const WvdQuestDefinition &task, const J &values,
    const std::set<std::string> &images, const BountyBuilder &bounty) {
    const auto &id = task.id;
    const auto plan = WvdTaskPlan::parse(task);
    if (task.type == "dungeon") return dungeon_iteration(plan, values, images);
    if (id == "Scorpionesses" || id == "Scorpionesses_plus_6_hands" || id == "jier") {
        if (!bounty) throw std::runtime_error("BOUNTY_PUBLIC_LIBRARY_MISSING");
        return bounty(task, values, images);
    }
    if (id == "fishing" || id == "fishing2") return fishing_cycle(task, values, images);
    if (id == "SSC-goldenchest") return golden_chest_cycle(task, values, images);
    if (id == "sandman") return sandman_cycle(task, values, images);
    if (id == "7000G") return gold_income_cycle(task);
    if (id == "LBC-oneGorgon") return bull_cave_cycle(task, values, images);
    if (id == "steeltrail") return steel_trial_cycle(task, values, images);
    if (id == "repelEnemyForces") return repel_forces_cycle(task, values, images);
    if (id == "lovesleep") return sleep_visits(task, values);
    if (id == "manualSepDemon") return manual_separation(task, values, images);
    if (id == "FFXI-Org") return mining_iteration(task, values);
    if (id == "darkLight") return dark_light(task, values, images);
    if (id == "gaintKiller") return giant_iteration(task, values, images);
    if (id == "fortress-B8F_trap") return fortress_trap_iteration(task, values, images);
    throw std::runtime_error("TASK_EXECUTION_NOT_IMPLEMENTED");
}

std::size_t task_unit_count(const std::string &id, const J &values) {
    if (id == "Scorpionesses_plus_6_hands") return 4;
    if (id == "Scorpionesses" || id == "jier") return 3;
    if (id == "SSC-goldenchest" || id == "sandman" || id == "manualSepDemon") return 2;
    if (id == "LBC-oneGorgon") return values.at("ACTIVE_REST").get<bool>() ? 3 : 2;
    if (id == "repelEnemyForces") return quests::RepelForces::rounds(values) + 2;
    if (id == "lovesleep") return quests::SleepVisits::units;
    return 1;
}

std::function<std::optional<devices::LifecyclePlan>(const contracts::SessionResult &,
    const contracts::BusinessRunState &, unsigned)> recovery_policy(devices::LifecycleTarget target) {
    return [target = std::move(target)](const contracts::SessionResult &result,
        const contracts::BusinessRunState &business, unsigned attempt)
        -> std::optional<devices::LifecyclePlan> {
        if (result.end != contracts::SessionEnd::Failed || !result.quiescent ||
            attempt < 1 || attempt > 3) return std::nullopt;
        const auto facts = business.summary();
        if (handoff_has_unconfirmed_effect(facts)) return std::nullopt;
        const auto leap = facts.value("handoff_intent", J(nullptr));
        const bool deferred_leap = result.reason == "leap.unknown" && attempt == 1 &&
            leap.is_object() && leap.value("kind", "") == "wait_7300" &&
            facts.at("leap_wait").value("active", false);
        const bool frozen_pause = result.reason == "pause.physics_frozen" &&
            !leap.is_object() && !facts.at("leap_wait").value("active", false);
        if (!deferred_leap && !frozen_pause) return std::nullopt;
        devices::LifecyclePlan plan;
        plan.target = target;
        plan.attempt = attempt;
        if (deferred_leap) {
            const auto remaining = 7300000LL - facts.at("leap_wait").at("elapsed_ms").get<std::int64_t>();
            plan.defer_for = std::chrono::milliseconds{std::max(0LL, remaining)};
        }
        if (target.vpn_required) plan.operations.push_back(devices::LifecycleOperation::EnsureVpn);
        plan.operations.push_back(devices::LifecycleOperation::StopApplication);
        plan.operations.push_back(devices::LifecycleOperation::StartApplication);
        return plan;
    };
}

bool handoff_ready(const contracts::SessionResult &last,
                   const contracts::BusinessRunState &state, const J &source) {
    if (last.reason != "leap.unknown" || !last.quiescent) return false;
    const auto facts = state.summary();
    const auto intent = facts.value("handoff_intent", J(nullptr));
    return intent.is_object() && intent.value("kind", "") == "turn_to_7000G" &&
        facts.at("handoff_source") == source &&
        intent.at("run_identity") == facts.at("run_identity") &&
        intent.at("generation") == facts.at("generation") &&
        intent.at("unknown_samples").get<std::uint64_t>() >= 5 &&
        !handoff_has_unconfirmed_effect(facts);
}
} // namespace wvd::games::tasks
