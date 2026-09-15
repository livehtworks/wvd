#include "sleep_visits.hpp"
#include "games/wvd/quests/sleep_visits.hpp"
#include "games/wvd/supply/inn.hpp"

namespace wvd::games::tasks {
void configure_sleep_units(runtime::RunDefinition &definition) {
    if (definition.max_business_units != 1 || !definition.continuation_units.empty())
        throw std::runtime_error("SLEEP_UNITS_ALREADY_CONFIGURED");
    definition.max_business_units = quests::SleepVisits::units;
    definition.continuation_units.assign(quests::SleepVisits::units - 1, definition.initial);
}
CompiledWorkflow sleep_visits(const WvdQuestDefinition &definition, const nlohmann::json &profile) {
    if (definition.id != "lovesleep" || definition.type != "quest")
        throw std::runtime_error("SLEEP_TASK_INVALID");
    using C = PipelineCompiler;
    // 保留全局有限时间约束，单个慢住宿不能让分批段无限延长；并非9999次的总预算。
    C graph("tasks.lovesleep", std::chrono::seconds{1600});
    const auto inn = C::image("Inn");
    const auto city = C::all({inn, C::absent(C::image("Stay"))});
    graph.route("Entry", {"Pending", "Loop"});
    graph.observe("Pending", C::business("/inn_payment_pending", true), {"Uncertain"});
    graph.recovery("Uncertain", "departure.inn_payment_unconfirmed");
    graph.route("Loop", {"BatchDone", "InProgress", "Begin"});
    graph.observe("BatchDone", C::business("/sleep/batch_complete", true), {"Terminal"});
    graph.observe("InProgress", C::business("/sleep/visit_active", true), {"Rest"});
    graph.confirm("Begin", "sleep.start", "sleep_visit_started", city, {"Rest"});
    const auto rest = graph.define_child("Inn", supply::rest_at_inn(profile.at("ACTIVE_ROYALSUITE_REST").get<bool>(), true));
    graph.call_child("Rest", rest, {"Rested"});
    // 已付钱但尚未返回城内时，不增加完成次数；恢复可继续退出，不再次购买。
    graph.confirm("Rested", "sleep.finish", "sleep_visit_completed", city, {"Loop"});
    graph.hit_limit("Loop", static_cast<int>(quests::SleepVisits::batch_size + 1));
    for (const auto *name : {"InProgress", "Begin", "Rest", "Rested"})
        graph.hit_limit(name, static_cast<int>(quests::SleepVisits::batch_size));
    graph.interrupt_on({{"mode", "blocking_screen"}, {"parallel_basic", true}}, "quest.sleep_common_screen_requires_dispatch");
    return graph.finish();
}
}
