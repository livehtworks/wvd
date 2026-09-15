#include "bounty_visit.hpp"

namespace wvd::games::tasks {
CompiledWorkflow visit_bounty_board(BountyVisit operation) {
    using C = PipelineCompiler;
    using J = nlohmann::json;
    if (operation != BountyVisit::Reveal && operation != BountyVisit::Report)
        throw std::runtime_error("BOUNTY_VISIT_INVALID");
    const bool report = operation == BountyVisit::Report;
    C graph(report ? "quest.bounty.report" : "quest.bounty.reveal", std::chrono::seconds{180});
    const auto guild = C::image("guild"), request = C::image("guildRequest"), bounty = C::image("Bounties");
    const auto completed = C::image("CompletionReported"), edge = C::image("EdgeOfTown");
    const auto menu = C::any({guild, request, bounty, C::image("guildFeatured"), completed});
    const auto post = C::any({menu, edge});
    const J find = report ? J{"PrepareReport", "Bounties", "Request", "Guild", "Swipe"} : J{"Request", "Bounties", "Guild", "Swipe"};
    graph.route("Entry", report ? J{"Pending", "Find"} : J{"Find"});
    graph.route("Find", find);
    graph.click("Guild", guild, guild, menu, find);
    graph.click("Request", request, request, menu, report ? J{"PrepareReport", "Bounties", "Swipe"} : J{"Bounties", "Swipe"});
    graph.click("Bounties", bounty, bounty, post, report ? J{"PrepareReport", "Swipe"} : J{"Exit"});
    // 源悬赏菜单横向滑动。只在已证明公会菜单且目标暂不可见时执行，不在未知页盲扫。
    graph.swipe("Swipe", C::all({menu, C::absent(report ? completed : bounty)}), menu,
        {600, 1400, 300, 1400}, report ? J{"PrepareReport", "Bounties", "Request", "Swipe"} : J{"Bounties", "Request", "Swipe"});
    graph.delay_after("Swipe", 1000);
    if (report) {
        graph.observe("Pending", C::business("/bounty_report_pending", true), {"Uncertain"});
        graph.recovery("Uncertain", "quest.bounty_report_unconfirmed");
        graph.confirm("PrepareReport", "bounty.report.prepare", "bounty_report_prepared", completed, {"Report"});
        graph.click("Report", completed, completed, C::all({post, C::absent(completed)}), {"Exit"});
        graph.postcondition_budget("Report", 10000);
    }
    graph.route("Exit", {"AtEdge", "Back"});
    graph.back("Back", menu, post, {"Exit"});
    if (report)
        graph.confirm("AtEdge", "bounty.report.done", "bounty_report_completed", C::all({edge, C::absent(completed)}), {"Terminal"});
    else
        graph.confirm("AtEdge", "bounty.reveal.done", "bounty_revealed", edge, {"Terminal"});
    for (const auto *name : {"Find", "Guild", "Request", "Bounties", "Swipe", "Exit", "Back"}) graph.hit_limit(name, 16);
    graph.interrupt_on({{"mode", "blocking_screen"}, {"parallel_basic", true}}, "quest.bounty_common_screen_requires_dispatch");
    return graph.finish();
}
}
