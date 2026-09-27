#include "bounty_visit.hpp"
#include "authoring/semantic_assets.hpp"
#include "games/wvd/vision/location_probes.hpp"

namespace wvd::games::tasks {
CompiledWorkflow leave_bounty_board(const PublicFlowLibrary &library, const std::string &locale) {
    using C = PipelineCompiler;
    using J = nlohmann::json;
    C graph("quest.bounty.leave_board", std::chrono::seconds{180});
    const auto edge = vision::edge_of_town_button();
    if (locale == "zh-Hant") {
        const auto menu = library.resource_condition("guild.menu", locale, authoring::ResourceUse::Observation);
        const auto lists = C::any({
            library.resource_condition("guild.bounties.page", locale, authoring::ResourceUse::Observation),
            library.resource_condition("guild.commissions.page", locale, authoring::ResourceUse::Observation)});
        const auto back = library.resource_condition("guild.list.back", locale, authoring::ResourceUse::Position);
        const auto leave = library.resource_condition("guild.leave", locale, authoring::ResourceUse::Position);
        const auto reveal = library.resource_condition("guild.bounty.reveal.close", locale, authoring::ResourceUse::Position);
        const auto in_list = C::all({lists, back});
        const auto in_guild = C::all({menu, leave, C::absent(lists)});
        graph.route("Entry", {"CloseReveal", "AtCity", "ListBack", "GuildLeave"});
        // 联网刷新可以在列表已出现、甚至返回已提交之后才弹展示卡。
        // 它不是返回成功；先消化已识别的卡片，再重新定位当前页面。
        graph.click("CloseReveal", reveal, reveal, C::any({lists, reveal, in_guild}), {"Entry"});
        graph.delay_after("CloseReveal", 700);
        // 必须观察到不同页面，不能把来源菜单仍在当作返回成功而连续发送BACK。
        graph.click("ListBack", in_list, back, C::any({in_guild, reveal}), {"Entry"});
        graph.click("GuildLeave", in_guild, leave, C::all({edge, C::absent(menu)}), {"Entry"});
        graph.retry_menu_input("ListBack", vision::menu_retry_ready(in_list));
        graph.retry_menu_input("GuildLeave", vision::menu_retry_ready(in_guild));
    } else {
        const auto menu = C::any({C::image("guildRequest"), C::image("Bounties"), C::image("guildFeatured")});
        graph.route("Entry", {"AtCity", "Back"});
        graph.back("Back", menu, C::any({menu, edge}), {"Entry"});
        graph.delay_after("Back", 1000);
    }
    graph.observe("AtCity", edge, {"Terminal"});
    graph.interrupt_on({{"mode", "blocking_screen"}, {"parallel_basic", true}},
                       "quest.bounty_common_screen_requires_dispatch");
    return graph.finish();
}

namespace {
CompiledWorkflow report_bounties_zh_hant(const PublicFlowLibrary &library,
                                        const nlohmann::json &root) {
    using C = PipelineCompiler;
    using J = nlohmann::json;
    C graph("quest.bounty.report", std::chrono::seconds{180});
    const auto resource = [&](const char *id, authoring::ResourceUse use) {
        return library.resource_condition(id, "zh-Hant", use);
    };
    const auto page = resource("guild.bounties.page", authoring::ResourceUse::Observation);
    const auto completed = resource("guild.report.action", authoring::ResourceUse::Position);
    const auto receipt = resource("guild.report.receipt", authoring::ResourceUse::Observation);
    const auto close = resource("guild.report.receipt.close", authoring::ResourceUse::Position);
    const auto no_report = C::all({page, C::absent(completed), C::absent(receipt)});
    const auto opened = library.compile(root,
        [](const J &) -> CompiledWorkflow { throw std::runtime_error("BOUNTY_PUBLIC_NATIVE_BINDING_FORBIDDEN"); },
        J{{"location", 0}}, "zh-Hant");
    const auto board = graph.define_child("PublicBoard", opened.workflow);
    const auto exit = graph.define_child("LeaveBoard", leave_bounty_board(library, "zh-Hant"));
    graph.route("Entry", {"Pending", "OpenBoard"});
    graph.observe("Pending", C::business("/bounty_report_pending", true), {"Uncertain"});
    graph.recovery("Uncertain", "quest.bounty_report_unconfirmed");
    // 与刷新、开轮前检查共用同一公共开页链，不再另外猜公会/委托/悬赏的位置。
    graph.call_child("OpenBoard", board, {"CheckReports"});
    graph.route("CheckReports", {"PrepareReport", "EmptyCandidate"});
    graph.confirm("PrepareReport", "bounty.report.prepare", "bounty_report_prepared",
        C::all({page, completed, C::absent(receipt)}), {"Report"});
    graph.click("Report", C::all({page, completed}), completed, receipt, {"CloseReceipt"});
    graph.click("CloseReceipt", receipt, close, C::all({page, C::absent(receipt)}), {"ReportConfirmed"});
    graph.retry_menu_input("CloseReceipt", vision::menu_retry_ready(receipt));
    graph.confirm("ReportConfirmed", "bounty.report.receipt", "bounty_report_completed",
        C::all({page, C::absent(receipt)}), {"CheckReports"});
    // 没有报告属于负证据：列表必须已加载，并跨新帧复核。等待不是点击期限，
    // 期间若报告出现，优先提交；网络/转场不允许通过此分支离开或跳轮。
    graph.observe("EmptyCandidate", no_report, {"RecheckEmpty"});
    graph.wait("RecheckEmpty", 1000, {"PrepareReport", "NoMoreReports"});
    graph.observe("NoMoreReports", no_report, {"Exit"});
    graph.call_child("Exit", exit, {"Terminal"});
    graph.interrupt_on({{"mode", "blocking_screen"}, {"parallel_basic", true}},
                       "quest.bounty_common_screen_requires_dispatch");
    return graph.finish();
}
}
CompiledWorkflow visit_bounty_board(BountyVisit operation, const PublicFlowLibrary &library,
                                    const nlohmann::json &root, const std::string &locale) {
    using C = PipelineCompiler;
    using J = nlohmann::json;
    if (operation != BountyVisit::Reveal && operation != BountyVisit::Report)
        throw std::runtime_error("BOUNTY_VISIT_INVALID");
    const bool report = operation == BountyVisit::Report;
    if (report && locale == "zh-Hant") return report_bounties_zh_hant(library, root);
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
        const auto bounty_page = bounty;
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
        graph.click("Report", completed, completed,
            C::all({post, C::absent(completed)}), {"Exit"});
        graph.postcondition_budget("Report", 10000);
        graph.route("Exit", {"AtEdge", "Back"});
        graph.back("Back", menu, post, {"Exit"});
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
    if (locale == "zh-Hant") {
        const auto exit = graph.define_child("LeaveBoard", leave_bounty_board(library, locale));
        graph.call_child("Exit", exit, {"Revealed"});
        graph.confirm("Revealed", "bounty.reveal.done", "bounty_revealed",
            vision::edge_of_town_button(), {"Terminal"});
        return graph.finish();
    }
    const auto edge = vision::edge_of_town_button();
    const auto menu = C::any({C::image("guildRequest"), C::image("Bounties"),
        C::image("guildFeatured"), C::image("CompletionReported"), vision::guild_button()});
    const auto post = C::any({menu, edge});
    graph.route("Exit", {"AtEdge", "Back"});
    graph.back("Back", menu, post, {"Exit"});
    graph.confirm("AtEdge", "bounty.reveal.done", "bounty_revealed", edge, {"Terminal"});
    graph.hit_limit("Exit", 48);
    graph.hit_limit("Back", 16);
    graph.interrupt_on({{"mode", "blocking_screen"}, {"parallel_basic", true}}, "quest.bounty_common_screen_requires_dispatch");
    return graph.finish();
}
}
