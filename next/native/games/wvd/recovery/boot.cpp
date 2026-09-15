#include "boot.hpp"
#include "party_death.hpp"
#include "global_prompt.hpp"
#include "karma_prompt.hpp"
#include "dialogue.hpp"

namespace wvd::games::recovery {
namespace {
using J = nlohmann::json;
using C = tasks::PipelineCompiler;
using O = devices::LifecycleOperation;
J scoped(const char *name, J roi, double threshold) {
    auto result = C::image(name);
    result["roi"] = roi;
    result["threshold"] = threshold;
    return result;
}
std::optional<runtime::SessionDefinition> decide(const contracts::SessionResult &result,
                                                const runtime::SessionDefinition &previous, const J &p) {
    if ((result.end != contracts::SessionEnd::RecoveryRequired && !contracts::connection_failed_before_task(result)) || !result.quiescent)
        return std::nullopt;
    // 重启无法判断刚才的副作用是否已生效。对此类明确的业务歧义，不升级、不重发。
    if (result.reason == "boot.download_permission_missing" ||
        result.reason == "combat.skill_outcome_unconfirmed" ||
        result.reason == "supply.healing_outcome_unconfirmed" ||
        result.reason == "chest.disarm_outcome_unconfirmed" ||
        result.reason == "chest.retry_pending" ||
        result.reason == "revival.outcome_unconfirmed" ||
        result.reason == "departure.inn_payment_unconfirmed" ||
        result.reason == "karma.choice_outcome_unconfirmed" ||
        result.reason == "dialogue.choice_outcome_unconfirmed" ||
        result.reason == "quest.manual_transfer_unconfirmed" || result.reason == "quest.bounty_report_unconfirmed" ||
        result.reason == "quest.bounty_transfer_unconfirmed" || result.reason == "quest.fishing_cast_unconfirmed" ||
        result.reason == "quest.fishing_bait_required" || result.reason == "quest.fishing_transfer_unconfirmed" ||
        result.reason == "quest.fishing_bait_still_empty")
        return std::nullopt;
    if (!result.business.is_object() || result.business.value("kind", "") != "wvd" ||
        !result.business.at("lifecycle_recovery_active").is_boolean())
        throw std::runtime_error("WVD_RECOVERY_STATE_INVALID");
    // 付款意图跨代次保留；重启无法证明付款未发生，不能借恢复重新住宿。
    if (result.business.at("inn_payment_pending").get<bool>())
        return std::nullopt;
    if (result.business.at("manual_separation").at("transfer_pending").get<bool>())
        return std::nullopt;
    if (result.business.at("bounty_report_pending").get<bool>())
        return std::nullopt;
    if (result.business.at("special_dialogue_pending").get<bool>())
        return std::nullopt;
    if (result.business.at("bounty_cycle").at("transfer_pending").get<bool>())
        return std::nullopt;
    if (result.business.at("fishing").at("casting_pending").get<bool>() ||
        result.business.at("fishing").at("reward_pending").get<bool>() ||
        result.business.at("fishing").at("transfer_pending").get<bool>())
        return std::nullopt;
    const bool continuing = previous.lifecycle && result.business.at("lifecycle_recovery_active").get<bool>();
    const unsigned attempt = continuing ? previous.lifecycle->attempt + 1 : 1;
    if (attempt > 3)
        return std::nullopt;
    devices::LifecyclePlan plan;
    plan.target = {p.at("device_id"), p.at("instance_id"), p.at("application_id"),
                    p.at("vpn_application_id"), p.at("vpn_required")};
    plan.attempt = attempt;
    bool force = p.at("force_restart_instance").get<bool>();
    if (attempt == 1 && result.business.is_object()) {
        if (result.business.value("kind", "") != "wvd" || !result.business.at("crashes").is_number_unsigned())
            throw std::runtime_error("WVD_RECOVERY_STATE_INVALID");
        const auto limit = p.at("max_crashes").get<std::int64_t>();
        force = force || limit < 0 || result.business.at("crashes").get<std::size_t>() >= static_cast<std::size_t>(limit);
    }
    if (attempt == 3 || (attempt == 1 && force))
        plan.operations.push_back(O::RestartInstance);
    else if (attempt == 2)
        plan.operations.push_back(O::Reconnect);
    // VPN 不是通用点击许可；它由专属端口证明状态。开着则跳过，不重复切换。
    if (plan.target.vpn_required)
        plan.operations.push_back(O::EnsureVpn);
    plan.operations.push_back(O::StopApplication);
    plan.operations.push_back(O::StartApplication);
    auto next = previous;
    next.lifecycle = std::move(plan);
    next.entry = "Boot_Entry";
    return next;
}
}
namespace {
tasks::CompiledWorkflow boot_workflow(bool allow_download, bool common, DialoguePolicy policy = DialoguePolicy::Default) {
    C graph(common ? "recovery.common_screens" : "recovery.boot_ready", std::chrono::seconds{120});
    graph.use_dialogue(policy);
    const auto panel = C::any({C::image("trait"), C::image("recover")});
    const J ready = C::all({common ? C::any({J{{"mode", "boot_ready"}}, panel, C::image("RiseAgain")}) : J{{"mode", "boot_ready"}},
                           C::absent(J{{"mode", "blocking_screen"}})});
    const auto title = scoped("boot_title_logo", {100, 300, 700, 470}, .86);
    const auto attention = scoped("boot_attention", {250, 430, 420, 220}, .86);
    const auto download = scoped("startdownload", {222, 901, 465, 84}, .8);
    const auto retry = C::image("retry");
    auto blank = C::image("retry_blank");
    blank["threshold"] = .65;
    auto low_retry = retry;
    low_retry["threshold"] = .60;
    const auto to_title = C::image("totitle"), resume = C::image("resume");
    const J recognized = common ? C::any({J{{"mode", "boot_post"}}, panel}) : J{{"mode", "boot_post"}};
    J entry = common ? J{"Download", "RetryBlank", "Retry", "RetryLow", "ReturnTitle", "Resume", "Attention", "Title", "Pause", "Death", "Sandman", "Blessing", "Karma", "Dialogue", "Defeat", "Ready"}
                     : J{"Ready", "Download", "RetryBlank", "Retry", "RetryLow", "ReturnTitle", "Resume", "Attention", "Title", "Pause", "Sandman", "Blessing", "Karma", "Dialogue"};
    if (policy != DialoguePolicy::Default) {
        entry.insert(entry.begin(), "SpecialDialogue");
        const auto special = graph.define_child("SpecialChoice", choose_special_dialogue(policy));
        graph.observe("SpecialDialogue", {{"mode", "special_dialogue"}}, {"ChooseSpecial"});
        graph.call_child("ChooseSpecial", special, {"Entry"});
        graph.hit_limit("SpecialDialogue", 6);
        graph.hit_limit("ChooseSpecial", 6);
    }
    graph.route("Entry", entry);
    const auto dialogue = graph.define_child("DefaultDialogue", choose_default_dialogue());
    graph.observe("Dialogue", {{"mode", "default_dialogue"}}, {"ChooseDialogue"});
    graph.call_child("ChooseDialogue", dialogue, {"Entry"});
    graph.hit_limit("Dialogue", 6);
    graph.hit_limit("ChooseDialogue", 6);
    const auto karma = graph.define_child("KarmaPrompt", choose_karma_prompt());
    graph.observe("Karma", C::any({C::image("ambush"), C::image("ignore")}), {"ChooseKarma"});
    graph.call_child("ChooseKarma", karma, {"Entry"});
    graph.hit_limit("Karma", 6);
    graph.hit_limit("ChooseKarma", 6);
    for (const auto &[prefix, prompt] : {std::pair{"Sandman", GlobalPrompt::SandmanRecovery},
                                         std::pair{"Blessing", GlobalPrompt::Blessing}}) {
        const std::string name = prefix;
        const auto child = graph.define_child(name + "Prompt", dismiss_global_prompt(prompt));
        graph.observe(name, C::image(prompt == GlobalPrompt::Blessing ? "blessing" : "sandman_recover"), {name + "Handle"});
        graph.call_child(name + "Handle", child, {"Entry"});
        graph.hit_limit(name, 6);
        graph.hit_limit(name + "Handle", 6);
    }
    if (common) {
        const auto death = graph.define_child("PartyDeath", dismiss_party_death(), {"BlockedExit"});
        graph.observe("Death", {{"mode", "party_death"}}, {"DismissDeath"});
        graph.call_child("DismissDeath", death, {"Entry"});
        graph.hit_limit("Death", 6);
        graph.hit_limit("DismissDeath", 6);
        const auto defeat = graph.define_child("PartyDefeat", acknowledge_party_defeat());
        graph.observe("Defeat", {{"mode", "party_defeat"}}, {"AcknowledgeDefeat"});
        graph.call_child("AcknowledgeDefeat", defeat, {"Entry"});
        graph.hit_limit("Defeat", 6);
        graph.hit_limit("AcknowledgeDefeat", 6);
    }
    graph.observe("Ready", ready, common ? J{"PendingDeathCleared", "Terminal"} : J{"Terminal"});
    if (common) {
        graph.observe("PendingDeathCleared", C::business("/death_prompt_pending", true), {"ConfirmDeathCleared"});
        graph.confirm("ConfirmDeathCleared", "party.death.clear", "party_death_cleared", ready, {"Terminal"});
    }
    if (allow_download)
        graph.click("Download", download, download, recognized, {"Entry"});
    else {
        graph.observe("Download", download, {"DownloadBlocked"});
        graph.recovery("DownloadBlocked", "boot.download_permission_missing");
    }
    graph.click("RetryBlank", blank, blank, recognized, {"Entry"}, {0, 103});
    graph.click("Retry", retry, retry, recognized, {"Entry"});
    graph.fixed_click("RetryLow", low_retry, recognized, {450, 900}, {"Entry"});
    graph.click("ReturnTitle", to_title, to_title, recognized, {"Entry"});
    graph.click("Resume", resume, resume, recognized, {"Entry"});
    graph.fixed_click("Attention", attention, recognized, {450, 1450}, {"Entry"});
    graph.fixed_click("Title", title, recognized, {450, 1450}, {"Entry"});
    const J pause{{"mode", "pause"}};
    graph.observe("Pause", pause, {"ResumePause0"});
    graph.hit_limit("Pause", 6);
    // 按旧版连续六次无效点击判定冻结，但第六次仍须新帧确认：
    // 最后一次已恢复不能重启；角色/技能详情负例由 pause 识别器排除。
    for (unsigned i = 0; i < 6; ++i) {
        const auto name = "ResumePause" + std::to_string(i);
        graph.fixed_click(name, pause, recognized, {450, 760},
            {"PauseCleared", i < 5 ? "ResumePause" + std::to_string(i + 1) : "PauseFrozen"});
        graph.hit_limit(name, 6);
        graph.delay_after(name, 2000);
        graph.postcondition_budget(name, 10000);
    }
    graph.observe("PauseCleared", C::absent(pause), {"Entry"});
    graph.hit_limit("PauseCleared", 6);
    graph.observe("PauseFrozen", pause, {"PauseFrozenExit"});
    graph.recovery("PauseFrozenExit", "pause.physics_frozen");
    graph.hit_limit("Entry", 30);
    for (auto name : {"Download", "RetryBlank", "Retry", "RetryLow", "ReturnTitle", "Resume", "Attention", "Title"}) {
        if (!allow_download && std::string(name) == "Download")
            continue;
        graph.hit_limit(name, 6);
        graph.delay_after(name, 1500);
        graph.postcondition_budget(name, 10000);
    }
    return graph.finish();
}
}
tasks::CompiledWorkflow wait_boot_ready(bool allow_download) {
    return boot_workflow(allow_download, false);
}
tasks::CompiledWorkflow clear_common_screens(bool allow_download, DialoguePolicy policy) {
    return boot_workflow(allow_download, true, policy);
}
tasks::CompiledWorkflow with_boot_recovery(const tasks::CompiledWorkflow &task, bool allow_download) {
    task.validate();
    C graph("recovery.restartable_task", task.time_limit + std::chrono::seconds{120});
    const auto task_entry = graph.append("Task", task, {"Terminal"});
    graph.confirm("RestartConfirmed", "game.restart", "game_restarted", {{"mode", "boot_ready"}}, {task_entry});
    const auto boot = graph.append("Boot", boot_workflow(allow_download, false, task.dialogue_policy), {"RestartConfirmed"});
    // Boot 在正常候选链不是默认动作；恢复策略只在新代次选择这个已封存入口。
    graph.route("Entry", {task_entry, boot});
    return graph.finish();
}
void register_recovery(runtime::BehaviorRegistry &registry) {
    registry.add_recovery({"wvd.recovery", "1"}, decide);
}
contracts::BehaviorBinding recovery_binding(const devices::LifecycleTarget &t, bool force, std::int64_t max_crashes) {
    return {"WvdRecovery", {"wvd.recovery", "1"}, {{"device_id", t.device_id}, {"instance_id", t.instance_id},
        {"application_id", t.application_id}, {"vpn_application_id", t.vpn_application_id},
        {"vpn_required", t.vpn_required}, {"force_restart_instance", force}, {"max_crashes", max_crashes}}};
}
}
