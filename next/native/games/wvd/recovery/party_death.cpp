#include "party_death.hpp"

namespace wvd::games::recovery {
tasks::CompiledWorkflow dismiss_party_death() {
    using C = tasks::PipelineCompiler;
    using J = nlohmann::json;
    C graph("recovery.party_death", std::chrono::seconds{90});
    const J dead{{"mode", "party_death"}};
    const auto ready = C::any({J{{"mode", "boot_ready"}}, C::image("RiseAgain")});
    const auto cleared = C::all({ready, C::absent(dead), C::absent(J{{"mode", "blocking_screen"}})});
    const auto known = C::any({J{{"mode", "boot_post"}}, C::image("RiseAgain")});
    graph.route("Entry", {"Observed"});
    graph.confirm("Observed", "party.death", "party_death_observed", dead, {"Dismiss0"});
    graph.delay_after("Observed", 1000);
    // 旧版在中心 100x100 区域无条件连点五次。保留区域/间隔，逐次确认仍是
    // 死亡提示才点；提示消失立即离开，不能继续把下一角色/新地图当点击对象。
    for (unsigned i = 0; i < 5; ++i) {
        const auto name = "Dismiss" + std::to_string(i);
        graph.fixed_click(name, dead, known, {450, 800},
            {"Cleared", "OtherBlocking", i < 4 ? "Dismiss" + std::to_string(i + 1) : "Unchanged"});
        graph.delay_after(name, 1000);
        graph.postcondition_budget(name, 10000);
    }
    graph.confirm("Cleared", "party.death.clear", "party_death_cleared", cleared, {"Terminal"});
    graph.observe("OtherBlocking", C::all({J{{"mode", "blocking_screen"}}, C::absent(dead)}), {"BlockedExit"});
    graph.recovery("BlockedExit", "party.death_interrupted");
    graph.observe("Unchanged", dead, {"UnchangedExit"});
    graph.recovery("UnchangedExit", "party.death_prompt_unchanged");
    return graph.finish();
}
}
