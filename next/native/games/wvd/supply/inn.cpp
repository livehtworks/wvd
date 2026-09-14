#include "inn.hpp"

namespace wvd::games::supply {
tasks::CompiledWorkflow rest_at_inn(bool royal_suite, bool record_completion) {
    using C = tasks::PipelineCompiler;
    C graph("supply.inn");
    const auto inn = C::image("Inn"), stay = C::image("Stay"), economy = C::image("Economy"),
               royal = C::image("royalsuite"), ok = C::image("OK");
    graph.route("Entry", record_completion ? nlohmann::json{"Receipt", "Unpaid"} : nlohmann::json{"Open"});
    graph.click("Open", inn, inn, stay, {"Stay"});
    graph.click("Stay", stay, stay, economy,
                royal_suite ? nlohmann::json{"Royal", "Economy"} : nlohmann::json{"Economy"});
    if (royal_suite)
        graph.click("Royal", C::all({economy, royal}), royal, ok, {"Confirm"});
    graph.click("Economy", royal_suite ? C::all({economy, C::absent(royal)}) : economy, economy, ok,
                {"Confirm"});
    const auto stayed = C::all({stay, C::absent(ok)});
    const auto city = C::all({inn, C::absent(stay)});
    graph.click("Confirm", ok, ok, stayed, record_completion ? nlohmann::json{"Paid"} : nlohmann::json{"Leave"});
    if (record_completion) {
        // 已确认付费后只允许退出旅店，不因下一段/恢复重复购买房间。
        // 业务条件只负责选路；Paid 用新视觉帧确认，不能以状态标志授权输入。
        graph.observe("Receipt", C::business("/inn_rest_completed", true), {"Rested", "Leave"});
        graph.observe("Unpaid", C::business("/inn_rest_completed", false), {"Open"});
        graph.confirm("Paid", "inn.paid", "inn_rest_completed", stayed, {"Leave"});
    }
    graph.back("Leave", stayed, city, {"Rested"});
    // 只有真正走过确认住宿和退出旅店的路径，才能到达本段业务终点。
    graph.observe("Rested", city, {"Terminal"});
    return graph.finish();
}
} // namespace wvd::games::supply
