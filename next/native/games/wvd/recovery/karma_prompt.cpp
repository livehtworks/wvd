#include "karma_prompt.hpp"

namespace wvd::games::recovery {
tasks::CompiledWorkflow choose_karma_prompt() {
    using C = tasks::PipelineCompiler;
    using J = nlohmann::json;
    C graph("recovery.karma_choice", std::chrono::seconds{45});
    const auto ambush = C::image("ambush"), ignore = C::image("ignore");
    const auto prompt = C::any({ambush, ignore});
    const J known{{"mode", "boot_post"}};
    const auto complete = C::all({C::absent(prompt), J{{"mode", "boot_ready"}},
                                   C::absent(J{{"mode", "blocking_screen"}})});
    graph.route("Entry", {"Observe"});
    graph.confirm("Observe", "karma.observe", "karma_observed", prompt, {"Ambush", "Ignore"});
    graph.observe("Ambush", C::business("/karma_ambush", true), {"ChooseAmbush"});
    graph.observe("Ignore", C::business("/karma_ambush", false), {"ChooseIgnore"});
    graph.click("ChooseAmbush", prompt, ambush, known, {"Confirmed", "Uncertain"});
    graph.click("ChooseIgnore", prompt, ignore, known, {"Confirmed", "Uncertain"});
    for (const auto name : {"ChooseAmbush", "ChooseIgnore"}) {
        graph.delay_after(name, 2000);
        graph.postcondition_budget(name, 10000);
        graph.failure_route(name, {"Uncertain"});
    }
    // 不复点：同一提示仍在可能是输入未生效，也可能是连续出现的另一次选择。
    // 没有消失/已知后置的证据就停止对账，不能靠重启或重复消费来猜。
    graph.confirm("Confirmed", "karma.complete", "karma_completed", complete, {"Terminal"});
    graph.recovery("Uncertain", "karma.choice_outcome_unconfirmed");
    return graph.finish();
}
}
