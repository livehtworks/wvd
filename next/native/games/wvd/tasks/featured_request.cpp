#include "featured_request.hpp"
#include "games/wvd/supply/inn.hpp"

namespace wvd::games::tasks {
CompiledWorkflow accept_featured_request(FeaturedRequest request, bool royal_suite) {
    using C = PipelineCompiler;
    using J = nlohmann::json;
    if (request != FeaturedRequest::BullCave && request != FeaturedRequest::GoldenChest)
        throw std::runtime_error("FEATURED_REQUEST_INVALID");
    const bool bull = request == FeaturedRequest::BullCave;
    C graph(bull ? "quest.featured.bull_cave" : "quest.featured.golden_chest", std::chrono::seconds{240});
    const auto inn = C::image("Inn"), guild = C::image("guild"), menu = C::image("guildRequest");
    const auto featured = C::image("guildFeatured"), target = C::image(bull ? "LBC/request" : "SSC/Request");
    const auto list = C::any({featured, target});
    const auto menus = C::any({menu, list});
    const auto city = C::all({inn, C::absent(C::image("Stay")), C::absent(target)});
    const auto left_list = C::all({C::any({city, menu}), C::absent(target)});
    const J accepted{{"mode", "featured_request_accepted"}, {"image", "LBC/request"}, {"accepted", true}};
    auto eligible = accepted;
    eligible["accepted"] = false;
    graph.route("Entry", {"Pending", "Resume", "Start"});
    graph.observe("Pending", C::business("/featured_visit/pending", true), {"Uncertain"});
    graph.recovery("Uncertain", "quest.featured_request_unconfirmed");
    graph.observe("Resume", C::business("/featured_visit/active", true), {"Rest"});
    graph.confirm("Start", "featured.visit.start", "featured_visit_started", city, {"Rest"});
    const auto rest = graph.define_child("Inn", supply::rest_at_inn(royal_suite, true));
    graph.call_child("Rest", rest, {"FindMenu"});
    graph.route("FindMenu", {"Request", "Guild"});
    graph.click("Guild", C::all({guild, C::absent(menu)}), guild, menus, {"FindMenu"});
    graph.click("Request", menu, menu, menus, {"Featured"});
    graph.click("Featured", featured, featured, list, {"Scroll0"});
    const int count = bull ? 3 : 1;
    for (int i = 0; i < count; ++i) {
        const auto name = "Scroll" + std::to_string(i);
        graph.swipe(name, list, list, bull ? J{150, 1000, 150, 200} : J{150, 1300, 150, 200},
            i + 1 == count ? J{"FindTarget"} : J{"Scroll" + std::to_string(i + 1)});
        graph.delay_after(name, i + 1 == count ? 2000 : 1000);
    }
    graph.route("FindTarget", bull ? J{"AlreadyAccepted", "Prepare", "FineScroll"} : J{"Prepare", "FineScroll"});
    if (bull) graph.observe("AlreadyAccepted", accepted, {"Exit"});
    const auto ready = bull ? C::all({target, eligible}) : target;
    graph.confirm("Prepare", "featured.prepare", "featured_request_prepared", ready, {"Select"});
    // 偏移仍基于输入前这一帧的位置，不保留滚动前的pos；未知后置保留pending，不能重新领一次。
    graph.click("Select", ready, target, left_list, {"Exit"}, bull ? J{266, 257} : J{300, 150});
    graph.postcondition_budget("Select", 10000);
    graph.delay_after("Select", 1000);
    graph.swipe("FineScroll", C::all({list, C::absent(target)}), list, {150, 200, 150, 250}, {"FindTarget"});
    graph.delay_after("FineScroll", 1000);
    graph.route("Exit", {"AtCity", "Back"});
    graph.back("Back", C::all({menus, C::absent(city)}), C::any({menus, city}), {"Exit"});
    graph.observe("AtCity", city, {"ConfirmIfPending", "VisitDone"});
    graph.observe("ConfirmIfPending", C::business("/featured_visit/pending", true), {"Complete"});
    graph.confirm("Complete", "featured.done", "featured_request_completed", city, {"VisitDone"});
    graph.confirm("VisitDone", "featured.visit.done", "featured_visit_completed", city, {"Terminal"});
    for (const auto *name : {"FindMenu", "Guild", "Request", "FindTarget", "FineScroll", "Exit", "Back"})
        graph.hit_limit(name, 32);
    graph.interrupt_on({{"mode", "blocking_screen"}, {"parallel_basic", true}}, "quest.featured_common_screen_requires_dispatch");
    return graph.finish();
}
}
