#include "party_death.hpp"
#include "games/wvd/vision/location_probes.hpp"

namespace wvd::games::recovery {
tasks::CompiledWorkflow dismiss_party_death() {
    using C = tasks::PipelineCompiler;
    using J = nlohmann::json;
    C graph("recovery.party_death", std::chrono::seconds{90});
    graph.check_policy("exception", {"wvd-network-retry"}, 1000, 1000, false);
    const J dead{{"mode", "party_death"}};
    const auto ready = C::any({J{{"mode", "combat_active"}}, J{{"mode", "party_defeat"}}, C::image("dungFlag"),
        C::image("mapFlag"), C::image("RiseAgain"), vision::city_screen()});
    const auto cleared = C::all({ready, C::absent(dead), J{{"mode", "input_clear"}}});
    const J known{{"mode", "party_death_post"}};
    graph.route("Entry", {"Observed"});
    graph.confirm("Observed", "party.death", "party_death_observed", dead, {"Dismiss"});
    // 不预测缩圈时机：每次新帧仍确认救人页才点中心，直到成功或进入再起。
    // 输入后置确认保留，不能盲发连点到已经恢复的战斗界面。
    graph.fixed_click("Dismiss", dead, known, {450, 800},
        {"Dismiss", "Cleared", "OtherBlocking"});
    graph.delay_after("Dismiss", 100);
    graph.postcondition_budget("Dismiss", 10000);
    graph.confirm("Cleared", "party.death.clear", "party_death_cleared", cleared, {"Terminal"});
    // The death notice can resemble Pause before the revival choices appear.
    // Reobserve within the existing handler deadline; only Dismiss authorizes input.
    graph.poll("OtherBlocking", 250, {"Dismiss", "Cleared", "OtherBlocking"});
    graph.failure_route("OtherBlocking", {"BlockedExit"});
    graph.recovery("BlockedExit", "party.death_interrupted");
    return graph.finish();
}
tasks::CompiledWorkflow acknowledge_party_defeat() {
    using C = tasks::PipelineCompiler;
    using J = nlohmann::json;
    C graph("recovery.party_defeat", std::chrono::seconds{90});
    graph.check_policy("exception", {"wvd-network-retry", "wvd-party-defeat-manual"}, 1000, 1000, false);
    const J defeat{{"mode", "party_defeat"}};
    graph.observe("Entry", C::absent(defeat), {"Terminal"});
    graph.event_scope("Entry", J::array({J{{"id", "wvd-party-defeat-manual"},
        {"class", "overlay"}, {"priority", 1000}, {"source_node", "Entry"},
        {"detect", defeat}, {"disposition", "external_blocked"},
        {"reason", "party.multiple_deaths_manual_required"}}}));
    return graph.finish();
}
}
