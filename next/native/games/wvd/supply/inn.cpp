#include "inn.hpp"
#include "games/wvd/vision/location_probes.hpp"
#include "games/wvd/vision/dialogue_probes.hpp"
#include "games/wvd/vision/inn_leave_probes.hpp"

namespace wvd::games::supply {
tasks::CompiledWorkflow rest_at_inn(bool royal_suite, bool record_completion) {
    using C = tasks::PipelineCompiler;
    C graph("supply.inn");
    graph.check_policy("supply", {"wvd-network-retry", "wvd-download", "wvd-story", "wvd-dialogue"});
    const auto inn = vision::inn_button(), stay = C::image("Stay"), economy = C::image("Economy"),
               royal = C::image("royalsuite"), ok_en = C::image("OK");
    const auto ok_zh = nlohmann::json{{"mode", "template"}, {"image", "inn_confirm_zh_hant"},
        {"threshold", 0.8}, {"roi", {100, 820, 700, 340}}};
    const auto ok = C::any({ok_zh, ok_en});
    // 用户已明确普通资源不受宝石消费禁令限制。此确认只从已选住宿房型进入，
    // 不因语言/价格不同退役旧住宿功能；宝石购买组合单独阻断，不靠商品颜色判断。
    const auto premium = vision::resource("purchase.premium.button", "zh-Hant");
    const auto confirmation = C::all({ok, C::absent(premium)});
    graph.route("Entry", record_completion ? nlohmann::json{"Pending", "Receipt", "Unpaid"} : nlohmann::json{"Open"});
    const auto payment = "Payment";
    graph.click("Open", inn, inn, stay, {"Stay"});
    graph.retry_menu_input("Open", vision::menu_retry_ready(C::all({inn, C::absent(stay)})));
    graph.click("Stay", stay, stay, economy,
                royal_suite ? nlohmann::json{"Royal", "Economy"} : nlohmann::json{"Economy"});
    graph.retry_menu_input("Stay", vision::menu_retry_ready(C::all({stay, C::absent(economy)}), "supply"));
    if (royal_suite)
        graph.click("Royal", C::all({economy, royal}), royal, ok, {payment});
    graph.click("Economy", royal_suite ? C::all({economy, C::absent(royal)}) : economy, economy, ok,
                {payment});
    graph.retry_menu_input("Economy", vision::menu_retry_ready(C::all({economy, C::absent(ok)}), "supply"));
    if (royal_suite)
        graph.retry_menu_input("Royal", vision::menu_retry_ready(C::all({economy, royal, C::absent(ok)}), "supply"));
    const auto stayed = C::all({stay, C::absent(ok)});
    const auto rest_story = vision::ordinary_story_page();
    const auto character = vision::character_page();
    auto supply_arrow = C::image("chest_reward_advance");
    supply_arrow["roi"] = {775, 940, 125, 120};
    const auto supply_notice = C::all({supply_arrow, C::absent(vision::story_auto_control())});
    // 城市反证逐项展开，避免“城市 -> 非结果页 -> 剧情 -> 对话”反复嵌套。
    // 后置已有“无确认/无宝石购买”，其内部不再包一份完整城市判定。
    const auto city = C::all({inn, C::absent(stay), C::absent(ok), C::absent(premium),
        C::absent(rest_story), C::absent(supply_notice), C::absent(character)});
    const auto receipt_page = C::any({stay, rest_story, supply_notice});
    const auto after_payment = C::all({C::any({stay, rest_story, supply_notice,
        C::all({inn, C::absent(character)})}), C::absent(ok), C::absent(premium)});
    // pending恢复时可能先看见加载帧，再出现付款结果。不能落入只识别确认按钮的子路由。
    graph.route("SelectConfirm", record_completion
        ? nlohmann::json{"PendingPaid", "PremiumBlocked", "ConfirmZh", "ConfirmEn", "UnpaidMenu"}
        : nlohmann::json{"PremiumBlocked", "ConfirmZh", "ConfirmEn"});
    graph.route("Payment", {"PremiumBlocked", "PreparePayment"});
    graph.click("ConfirmZh", C::all({confirmation, ok_zh}), ok_zh, after_payment, {"Paid"});
    graph.click("ConfirmEn", C::all({confirmation, C::absent(ok_zh)}), ok_en, after_payment, {"Paid"});
    // 确认按钮仍在已排除付款完成页，无需再把整份后置条件取反嵌入重试。
    graph.retry_menu_input("ConfirmZh", vision::menu_retry_ready(C::all({confirmation, ok_zh}), "supply"), 5000, 0, "Entry");
    graph.retry_menu_input("ConfirmEn", vision::menu_retry_ready(C::all({confirmation, C::absent(ok_zh)}), "supply"), 5000, 0, "Entry");
    graph.input_effect("ConfirmZh", "WvdInnGoldEffect");
    graph.input_effect("ConfirmEn", "WvdInnGoldEffect");
    graph.observe("PremiumBlocked", premium, {"PaymentBlocked"});
    graph.recovery("PaymentBlocked", "purchase.premium_forbidden");
    graph.confirm("PreparePayment", "inn.prepare", "inn_payment_prepared", confirmation, {"SelectConfirm"});
    graph.confirm("Paid", "inn.paid", "inn_rest_completed", after_payment, {"Leave"});
    if (record_completion) {
        graph.observe("Pending", C::business("/inn_payment_pending", true), {"PendingPaid", "SelectConfirm"});
        // 重入不能仅凭城市图标伪造住宿成功；必须有实际提交和旅店结果页。
        graph.observe("PendingPaid", C::all({C::business("/inn_payment/submitted", true),
            receipt_page, C::absent(ok), C::absent(premium)}), {"Paid"});
        // 金币住宿允许重新办理：只在正常菜单重新选房，保留真实提交次数与费用。
        // 结果页优先进入PendingPaid；未知页/宝石购买页不授权开房。
        graph.observe("UnpaidMenu", C::all({C::business("/inn_payment_pending", true),
            C::any({inn, stay, economy}), C::absent(ok), C::absent(premium),
            C::absent(rest_story), C::absent(supply_notice)}), {"Rooms", "Stay", "Open"});
        // 已确认付费后只允许退出旅店，不因下一段/恢复重复购买房间。
        // 业务条件只负责选路；Paid 用新视觉帧确认，不能以状态标志授权输入。
        graph.observe("Receipt", C::business("/inn_rest_completed", true), {"Rested", "Leave"});
        graph.observe("Unpaid", C::business("/inn_rest_completed", false), {"ResumeConfirmation", "Rooms", "Stay", "Open"});
        // 新任务可能从上一轮留下的标准房确认页开始；只认具体住宿句，不借任意OK付款。
        graph.observe("ResumeConfirmation", C::all({vision::resource("inn.standard.gold.confirmation", "zh-Hant"),
            confirmation}), {"Payment"});
        graph.observe("Rooms", economy, royal_suite ? nlohmann::json{"Royal", "Economy"} : nlohmann::json{"Economy"});
    }
    const auto leave_zh = vision::inn_leave_zh();
    graph.route("Leave", {"AtCity", "ContinueStory", "ContinueSupply", "CloseCharacter", "BackFromStayZh", "BackFromStay"});
    // 分派不是住宿次数，不加默认经过次数；剧情/补给输入仍各自有界。
    // 已有住宿结果则仅退出；没有结果且回到普通菜单时允许重新办理金币住宿。
    graph.observe("AtCity", city, {"Rested"});
    graph.click("ContinueStory", rest_story, vision::story_advance_arrow(),
                after_payment, {"Leave"});
    graph.hit_limit("ContinueStory", 12);
    graph.click("ContinueSupply", supply_notice, supply_arrow, after_payment, {"Leave"});
    graph.hit_limit("ContinueSupply", 6);
    graph.fixed_click("CloseCharacter", character, after_payment, {66, 1500}, {"Leave"});
    graph.retry_menu_input("CloseCharacter", character, 3000);
    const auto leave_result = C::any({city, character, rest_story, supply_notice});
    graph.click("BackFromStayZh", C::all({stayed, leave_zh, C::absent(rest_story),
                    C::absent(supply_notice), C::absent(character),
                    nlohmann::json{{"mode", "input_clear"}, {"phase", "supply"}}}),
                leave_zh, leave_result, {"Leave"});
    graph.retry_menu_input("BackFromStayZh", C::all({stayed, leave_zh, C::absent(character)}), 3000);
    graph.click("BackFromStay", C::all({stayed, C::absent(leave_zh), C::absent(character), C::absent(rest_story), C::absent(supply_notice),
                    nlohmann::json{{"mode", "input_clear"}, {"phase", "supply"}}}),
                C::image("Stay.png"), leave_result, {"Leave"}, vision::inn_leave_offset());
    graph.retry_menu_input("BackFromStay", vision::menu_retry_ready(
        C::all({stayed, C::absent(rest_story), C::absent(supply_notice)}), "supply"));
    // 只有真正走过确认住宿和退出旅店的路径，才能到达本段业务终点。
    graph.observe("Rested", city, {"Terminal"});
    return graph.finish();
}
} // namespace wvd::games::supply
