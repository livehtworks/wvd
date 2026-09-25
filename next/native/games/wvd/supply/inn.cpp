#include "inn.hpp"
#include "games/wvd/vision/location_probes.hpp"
#include "games/wvd/vision/dialogue_probes.hpp"

namespace wvd::games::supply {
tasks::CompiledWorkflow rest_at_inn(bool royal_suite, bool record_completion) {
    using C = tasks::PipelineCompiler;
    C graph("supply.inn");
    const auto inn = vision::inn_button(), stay = C::image("Stay"), economy = C::image("Economy"),
               royal = C::image("royalsuite"), ok_en = C::image("OK");
    const auto ok_zh = nlohmann::json{{"mode", "template"}, {"image", "inn_confirm_zh_hant"},
        {"threshold", 0.8}, {"roi", {470, 900, 300, 180}}};
    const auto ok = C::any({ok_zh, ok_en});
    graph.route("Entry", record_completion ? nlohmann::json{"Pending", "Receipt", "Unpaid"} : nlohmann::json{"Open"});
    const auto payment = record_completion ? "PreparePayment" : "SelectConfirm";
    graph.click("Open", inn, inn, stay, {"Stay"});
    graph.click("Stay", stay, stay, economy,
                royal_suite ? nlohmann::json{"Royal", "Economy"} : nlohmann::json{"Economy"});
    if (royal_suite)
        graph.click("Royal", C::all({economy, royal}), royal, ok, {payment});
    graph.click("Economy", royal_suite ? C::all({economy, C::absent(royal)}) : economy, economy, ok,
                {payment});
    const auto stayed = C::all({stay, C::absent(ok)});
    const auto city = C::all({inn, C::absent(stay)});
    const auto rest_story = vision::ordinary_story_page();
    auto supply_arrow = C::image("chest_reward_advance");
    supply_arrow["roi"] = {775, 940, 125, 120};
    const auto supply_notice = C::all({supply_arrow, C::absent(vision::story_auto_control())});
    const auto after_payment = C::any({stayed, rest_story, supply_notice, city});
    graph.route("SelectConfirm", {"ConfirmZh", "Confirm"});
    const auto after_confirm = record_completion ? nlohmann::json{"Paid"} : nlohmann::json{"Leave"};
    graph.click("ConfirmZh", ok_zh, ok_zh, after_payment, after_confirm);
    graph.click("Confirm", ok_en, ok_en, after_payment, after_confirm);
    if (record_completion) {
        graph.observe("Pending", C::business("/inn_payment_pending", true), {"Uncertain"});
        graph.recovery("Uncertain", "departure.inn_payment_unconfirmed");
        graph.confirm("PreparePayment", "inn.prepare", "inn_payment_prepared", ok, {"SelectConfirm"});
        // 已确认付费后只允许退出旅店，不因下一段/恢复重复购买房间。
        // 业务条件只负责选路；Paid 用新视觉帧确认，不能以状态标志授权输入。
        graph.observe("Receipt", C::business("/inn_rest_completed", true), {"Rested", "Leave"});
        graph.observe("Unpaid", C::business("/inn_rest_completed", false), {"Open"});
        graph.confirm("Paid", "inn.paid", "inn_rest_completed", after_payment, {"Leave"});
    }
    graph.route("Leave", {"AtCity", "ContinueStory", "ContinueSupply", "BackFromStay"});
    graph.observe("AtCity", city, {"Rested"});
    graph.click("ContinueStory", rest_story, vision::story_advance_arrow(),
                after_payment, {"Leave"});
    graph.hit_limit("ContinueStory", 12);
    graph.click("ContinueSupply", supply_notice, supply_arrow, after_payment, {"Leave"});
    graph.hit_limit("ContinueSupply", 6);
    graph.back("BackFromStay", stayed, city, {"Rested"});
    // 只有真正走过确认住宿和退出旅店的路径，才能到达本段业务终点。
    graph.observe("Rested", city, {"Terminal"});
    return graph.finish();
}
} // namespace wvd::games::supply
