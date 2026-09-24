#include "bounty_visit.hpp"
#include "games/wvd/vision/location_probes.hpp"

namespace wvd::games::tasks {
CompiledWorkflow visit_bounty_board(BountyVisit operation, const PublicFlowLibrary &library,
                                    const nlohmann::json &root, const std::string &locale) {
    using C = PipelineCompiler;
    using J = nlohmann::json;
    if (operation != BountyVisit::Reveal && operation != BountyVisit::Report)
        throw std::runtime_error("BOUNTY_VISIT_INVALID");
    const bool report = operation == BountyVisit::Report;
    C graph(report ? "quest.bounty.report" : "quest.bounty.reveal", std::chrono::seconds{180});
    if (report) {
        // 尚无可靠繁中提交证据；进入此阶段前停止，不把领奖按钮误当作悬赏提交。
        graph.route("Entry", {"ReportUnavailable"});
        graph.recovery("ReportUnavailable", "quest.bounty_report_resource_unconfirmed");
        return graph.finish();
    }
    const auto compiled = library.compile(root,
        [](const J &) -> CompiledWorkflow { throw std::runtime_error("BOUNTY_PUBLIC_NATIVE_BINDING_FORBIDDEN"); },
        J{{"location", 0}}, locale);
    const auto board = graph.define_child("PublicBoard", compiled.workflow);
    graph.call_child("Entry", board, {"Exit"});
    const auto edge = vision::edge_of_town_button();
    const auto menu = C::any({C::image("guildRequest"), C::image("Bounties"),
        C::image("guildFeatured"), C::image("CompletionReported"), vision::guild_button()});
    const auto post = C::any({menu, edge});
    graph.route("Exit", {"AtEdge", "Back"});
    graph.back("Back", menu, post, {"Exit"});
    graph.confirm("AtEdge", "bounty.reveal.done", "bounty_revealed", edge, {"Terminal"});
    graph.hit_limit("Exit", 16);
    graph.hit_limit("Back", 16);
    graph.interrupt_on({{"mode", "blocking_screen"}, {"parallel_basic", true}}, "quest.bounty_common_screen_requires_dispatch");
    return graph.finish();
}
}
