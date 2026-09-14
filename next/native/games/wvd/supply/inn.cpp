#include "inn.hpp"

namespace wvd::games::supply {
tasks::CompiledWorkflow rest_at_inn(bool royal_suite) {
    using C = tasks::PipelineCompiler;
    C graph("supply.inn");
    const auto inn = C::image("Inn"), stay = C::image("Stay"), economy = C::image("Economy"),
               royal = C::image("royalsuite"), ok = C::image("OK");
    graph.route("Entry", {"Open"});
    graph.click("Open", inn, inn, stay, {"Stay"});
    graph.click("Stay", stay, stay, economy,
                royal_suite ? nlohmann::json{"Royal", "Economy"} : nlohmann::json{"Economy"});
    if (royal_suite)
        graph.click("Royal", C::all({economy, royal}), royal, ok, {"Confirm"});
    graph.click("Economy", royal_suite ? C::all({economy, C::absent(royal)}) : economy, economy, ok,
                {"Confirm"});
    const auto stayed = C::all({stay, C::absent(ok)});
    const auto city = C::all({inn, C::absent(stay)});
    graph.click("Confirm", ok, ok, stayed, {"Leave"});
    graph.back("Leave", stayed, city, {"Rested"});
    // 只有真正走过确认住宿和退出旅店的路径，才能到达本段业务终点。
    graph.observe("Rested", city, {"Terminal"});
    return graph.finish();
}
} // namespace wvd::games::supply
