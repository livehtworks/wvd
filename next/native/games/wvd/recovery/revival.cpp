#include "revival.hpp"

namespace wvd::games::recovery {
tasks::CompiledWorkflow revive_after_defeat() {
    using C = tasks::PipelineCompiler;
    using J = nlohmann::json;
    C graph("recovery.revival");
    const auto prompt = C::image("RiseAgain");
    const auto ready = C::all({J{{"mode", "boot_ready"}}, C::absent(prompt),
                               C::absent(J{{"mode", "blocking_screen"}})});
    const auto post = C::any({prompt, ready});
    graph.route("Entry", {"Observed"});
    graph.confirm("Observed", "revival.begin", "revival_observed", prompt, {"Accept"});
    // 旧 IdentifyState/StateChest 先点模板，再由 RiseAgainReset 点中心。
    // 若第一次已离开提示，不把第二个固定点发给新场景。
    graph.click("Accept", prompt, prompt, post, {"Confirmed", "Continue"});
    graph.hit_limit("Accept", 1);
    graph.fixed_click("Continue", prompt, post, {450, 750}, {"Confirmed", "Unchanged"});
    graph.hit_limit("Continue", 1);
    graph.delay_after("Continue", 10000);
    graph.postcondition_budget("Accept", 15000);
    graph.postcondition_budget("Continue", 15000);
    graph.confirm("Confirmed", "revival.completed", "resurrected", ready, {"Terminal"});
    graph.observe("Unchanged", prompt, {"Unconfirmed"});
    graph.recovery("Unconfirmed", "revival.outcome_unconfirmed");
    graph.stop_if_interrupted_after("Accept", "revival.outcome_unconfirmed");
    graph.stop_if_interrupted_after("Continue", "revival.outcome_unconfirmed");
    graph.interrupt_on({{"mode", "blocking_screen"}}, "revival.common_screen_requires_dispatch");
    return graph.finish();
}
}
