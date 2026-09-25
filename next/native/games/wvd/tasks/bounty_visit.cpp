#include "bounty_visit.hpp"
#include "authoring/semantic_assets.hpp"
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
        // 仅在看到可点击的达成报告时提交；悬赏名称不参与判断。
        const auto completed = library.resource_condition("guild.report.action",
            locale.empty() ? "en" : locale, authoring::ResourceUse::Position);
        const auto guild = vision::guild_button(), edge = vision::edge_of_town_button();
        const auto request = library.resource_condition("guild.commissions.entry",
            locale.empty() ? "en" : locale, authoring::ResourceUse::Position);
        const auto bounty = library.resource_condition("guild.bounties.entry",
            locale.empty() ? "en" : locale, authoring::ResourceUse::Position);
        const auto bounty_page = locale == "zh-Hant" ?
            library.resource_condition("guild.bounties.page", locale,
                authoring::ResourceUse::Observation) : bounty;
        const auto menu = C::any({guild, request, bounty, bounty_page, completed});
        const auto post = C::any({menu, edge});
        const J find{"PrepareReport", "Bounties", "Request", "Guild", "Swipe"};
        graph.route("Entry", {"Pending", "Find"});
        graph.observe("Pending", C::business("/bounty_report_pending", true), {"Uncertain"});
        graph.recovery("Uncertain", "quest.bounty_report_unconfirmed");
        graph.route("Find", find);
        graph.click("Guild", guild, guild, menu, find);
        graph.click("Request", request, request, menu, {"PrepareReport", "Bounties", "Swipe"});
        graph.click("Bounties", bounty, bounty, bounty_page, {"PrepareReport", "Swipe"});
        graph.swipe("Swipe", C::all({menu, C::absent(completed)}), menu,
            {600, 1400, 300, 1400}, {"PrepareReport", "Bounties", "Request", "Swipe"});
        graph.delay_after("Swipe", 1000);
        graph.confirm("PrepareReport", "bounty.report.prepare", "bounty_report_prepared", completed, {"Report"});
        if (locale == "zh-Hant") {
            const auto receipt = library.resource_condition("guild.report.receipt", locale,
                authoring::ResourceUse::Observation);
            const auto close = library.resource_condition("guild.report.receipt.close", locale,
                authoring::ResourceUse::Position);
            graph.click("Report", completed, completed, receipt, {"CloseReceipt"});
            graph.click("CloseReceipt", receipt, close,
                C::all({bounty_page, C::absent(receipt)}), {"ReportConfirmed"});
            graph.confirm("ReportConfirmed", "bounty.report.receipt", "bounty_report_completed",
                bounty_page, {"ReportLoop"});
            graph.route("ReportLoop", {"PrepareReport", "NoMoreReports"});
            graph.observe("NoMoreReports", C::all({bounty_page, C::absent(completed)}), {"Exit"});
            for (const auto *name : {"PrepareReport", "Report", "CloseReceipt", "ReportLoop"})
                graph.hit_limit(name, 16);
        } else {
            graph.click("Report", completed, completed,
                C::all({post, C::absent(completed)}), {"Exit"});
        }
        graph.postcondition_budget("Report", 10000);
        graph.route("Exit", {"AtEdge", "Back"});
        graph.back("Back", menu, post, {"Exit"});
        if (locale == "zh-Hant")
            graph.observe("AtEdge", C::all({edge, C::absent(completed)}), {"Terminal"});
        else
            graph.confirm("AtEdge", "bounty.report.done", "bounty_report_completed",
                C::all({edge, C::absent(completed)}), {"Terminal"});
        for (const auto *name : {"Find", "Guild", "Request", "Bounties", "Swipe", "Exit", "Back"})
            graph.hit_limit(name, 16);
        graph.interrupt_on({{"mode", "blocking_screen"}, {"parallel_basic", true}},
                           "quest.bounty_common_screen_requires_dispatch");
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
    graph.route("Exit", locale == "zh-Hant" ? J{"AtEdge", "CloseReveal", "Back"}
                                          : J{"AtEdge", "Back"});
    if (locale == "zh-Hant") {
        const auto close_reveal = library.resource_condition("guild.bounty.reveal.close", locale,
            authoring::ResourceUse::Position);
        // 刷新后可能连续出现多张展示卡；相同关闭按钮留在下一张卡上也是有效过渡。
        graph.click("CloseReveal", close_reveal, close_reveal,
            C::any({post, close_reveal}), {"Exit"});
        graph.delay_after("CloseReveal", 700);
        graph.hit_limit("CloseReveal", 16);
    }
    graph.back("Back", menu, post, {"Exit"});
    graph.confirm("AtEdge", "bounty.reveal.done", "bounty_revealed", edge, {"Terminal"});
    graph.hit_limit("Exit", 48);
    graph.hit_limit("Back", 16);
    graph.interrupt_on({{"mode", "blocking_screen"}, {"parallel_basic", true}}, "quest.bounty_common_screen_requires_dispatch");
    return graph.finish();
}
}
