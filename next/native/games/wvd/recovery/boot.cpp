#include "boot.hpp"

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
    if (result.end != contracts::SessionEnd::RecoveryRequired || !result.quiescent)
        return std::nullopt;
    // 重启无法判断刚才的副作用是否已生效。对此类明确的业务歧义，不升级、不重发。
    if (result.reason == "boot.download_permission_missing" ||
        result.reason == "combat.skill_outcome_unconfirmed" ||
        result.reason == "supply.healing_outcome_unconfirmed" ||
        result.reason == "chest.disarm_outcome_unconfirmed" ||
        result.reason == "revival.outcome_unconfirmed" ||
        result.reason == "departure.inn_payment_unconfirmed")
        return std::nullopt;
    if (!result.business.is_object() || result.business.value("kind", "") != "wvd" ||
        !result.business.at("lifecycle_recovery_active").is_boolean())
        throw std::runtime_error("WVD_RECOVERY_STATE_INVALID");
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
tasks::CompiledWorkflow boot_workflow(bool allow_download, bool common) {
    C graph(common ? "recovery.common_screens" : "recovery.boot_ready", std::chrono::seconds{120});
    const auto panel = C::any({C::image("trait"), C::image("recover")});
    const J ready = C::all({common ? C::any({J{{"mode", "boot_ready"}}, panel}) : J{{"mode", "boot_ready"}},
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
    graph.route("Entry", common ? J{"Download", "RetryBlank", "Retry", "RetryLow", "ReturnTitle", "Resume", "Attention", "Title", "Pause", "Ready"}
                                 : J{"Ready", "Download", "RetryBlank", "Retry", "RetryLow", "ReturnTitle", "Resume", "Attention", "Title", "Pause"});
    graph.observe("Ready", ready, {"Terminal"});
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
tasks::CompiledWorkflow clear_common_screens(bool allow_download) {
    return boot_workflow(allow_download, true);
}
tasks::CompiledWorkflow with_boot_recovery(const tasks::CompiledWorkflow &task, bool allow_download) {
    task.validate();
    C graph("recovery.restartable_task", task.time_limit + std::chrono::seconds{120});
    const auto task_entry = graph.append("Task", task, {"Terminal"});
    graph.confirm("RestartConfirmed", "game.restart", "game_restarted", {{"mode", "boot_ready"}}, {task_entry});
    const auto boot = graph.append("Boot", wait_boot_ready(allow_download), {"RestartConfirmed"});
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
