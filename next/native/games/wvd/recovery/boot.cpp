#include "boot.hpp"
#include "party_death.hpp"
#include "global_prompt.hpp"
#include "karma_prompt.hpp"
#include "dialogue.hpp"
#include "leap_wait.hpp"
#include <limits>

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
J task_stop_condition(DialoguePolicy policy) {
    J stops = J::array();
    for (auto name : dialogue_task_stops(policy))
        stops.push_back(C::image(std::string(name)));
    return stops.empty() ? J(nullptr) : C::all({C::any(std::move(stops)),
        C::absent(J{{"mode", "combat_active"}}), C::absent(C::image("RiseAgain"))});
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
    if (result.business.at("featured_visit").at("pending").get<bool>())
        return std::nullopt;
    if (result.business.at("golden_chest").at("leap_pending").get<bool>())
        return std::nullopt;
    if (result.business.at("sandman").at("leap_pending").get<bool>())
        return std::nullopt;
    if (result.business.at("gold_income").at("pending").get<bool>())
        return std::nullopt;
    if (result.business.at("bull_cave").at("leap_pending").get<bool>())
        return std::nullopt;
    if (result.business.at("steel_trial").at("pending").get<bool>())
        return std::nullopt;
    if (result.business.at("repel_forces").at("pending").get<bool>())
        return std::nullopt;
    // Leap/机关/领取/guild点击的意图跨恢复保留；未知后置不能通过重启重发。
    if (result.business.at("fordraig").at("pending").get<bool>() ||
        result.business.at("cave_of_separation").at("pending").get<bool>())
        return std::nullopt;
    if (result.business.at("bounty_cycle").at("transfer_pending").get<bool>())
        return std::nullopt;
    if (result.business.at("fishing").at("casting_pending").get<bool>() ||
        result.business.at("fishing").at("reward_pending").get<bool>() ||
        result.business.at("fishing").at("transfer_pending").get<bool>())
        return std::nullopt;
    // 转任务必须交给旧Run退出、结果保存后的调度入口，不能被普通重启吞掉。
    const auto intent = result.business.value("handoff_intent", J(nullptr));
    if (intent.is_object() && intent.at("kind") == "turn_to_7000G")
        return std::nullopt;
    const auto wait = result.business.value("leap_wait", J::object());
    if (wait.value("active", false) && !wait.at("ready").get<bool>()) {
        // 只承接本恢复链的明确边界；连接异常等不能借等待隐藏或自动升级。
        if (result.reason != "leap.unknown" && result.reason != "leap.wait_boundary")
            return std::nullopt;
        if (wait.at("slices").get<std::size_t>() >= LeapWait::max_slices)
            throw std::runtime_error("LEAP_WAIT_SLICE_BUDGET_EXHAUSTED");
        return leap_wait_session(previous);
    }
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
    if (previous.entry == "LeapWait_Entry")
        restore_leap_session_budget(next);
    next.lifecycle = std::move(plan);
    if (p.contains("boot_entries")) {
        // 同revision多阶段使用显式冻结映射，不按unit猜名称，也不缺键退回首段。
        const auto &entries = p.at("boot_entries");
        if (!entries.is_object() || !entries.contains(previous.checkpoint_node) ||
            !entries.at(previous.checkpoint_node).is_string() ||
            entries.at(previous.checkpoint_node).get<std::string>().empty())
            throw std::runtime_error("WVD_BOOT_ENTRY_MAPPING_INVALID");
        next.entry = entries.at(previous.checkpoint_node).get<std::string>();
    } else {
        if (previous.checkpoint_node != "Checkpoint")
            throw std::runtime_error("WVD_BOOT_ENTRY_MAPPING_REQUIRED");
        next.entry = "Boot_Entry";
    }
    return next;
}
}
namespace {
tasks::CompiledWorkflow boot_workflow(bool allow_download, bool common, DialoguePolicy policy = DialoguePolicy::Default) {
    C graph(common ? "recovery.common_screens" : "recovery.boot_ready", std::chrono::seconds{120});
    graph.use_dialogue(policy);
    const auto task_stop = task_stop_condition(policy);
    const auto panel = C::any({C::image("trait"), C::image("recover")});
    const J ready = C::all({common ? C::any({J{{"mode", "boot_ready"}}, panel, C::image("RiseAgain")}) : J{{"mode", "boot_ready"}},
                           C::absent(J{{"mode", "blocking_screen"}})});
    const auto title = scoped("boot_title_logo", {100, 300, 700, 470}, .86);
    // 首次免责声明跟随系统区域设置，游戏主体即使配置为英文也可能显示繁中。
    const auto attention = C::any({scoped("boot_attention", {250, 430, 420, 220}, .86),
                                   scoped("boot_attention_zh", {250, 430, 420, 220}, .86)});
    const auto download = scoped("startdownload", {222, 901, 465, 84}, .8);
    const auto retry = C::image("retry");
    auto blank = C::image("retry_blank");
    blank["threshold"] = .65;
    auto low_retry = retry;
    low_retry["threshold"] = .60;
    const auto to_title = C::image("totitle"), resume = C::image("resume");
    J recognized = common ? C::any({J{{"mode", "boot_post"}}, panel}) : J{{"mode", "boot_post"}};
    if (!task_stop.is_null())
        recognized = C::any({task_stop, recognized});
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
    if (!task_stop.is_null()) {
        // 任务停点先于普通/特殊对话。这里只正常返回子流程，不写UserStopped或COS完成。
        entry.insert(entry.begin(), "TaskStop");
        graph.observe("TaskStop", task_stop, {"Terminal"});
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
    auto result = graph.finish();
    if (!task_stop.is_null()) {
        // 候选命中后到输入前仍可能出现停点，所有输入都用新帧重新排除。
        for (auto &node : result.nodes) {
            if (node.value("custom_action", "") != "GuardedAction") continue;
            auto &scene = node["custom_action_param"]["scene_recognition"]["parameters"];
            scene = C::all({scene, C::absent(task_stop)});
            node["custom_recognition_param"] = C::all({node.at("custom_recognition_param"), C::absent(task_stop)});
        }
        // 动作可能直接到达停点；例如Pause动作的后继不能继续点击或先选对话。
        std::vector<std::string> actions{"ChooseSpecial", "ChooseDialogue", "ChooseKarma",
            "SandmanHandle", "BlessingHandle", "DismissDeath", "AcknowledgeDefeat", "Download",
            "RetryBlank", "Retry", "RetryLow", "ReturnTitle", "Resume", "Attention", "Title"};
        for (unsigned i = 0; i < 6; ++i) actions.push_back("ResumePause" + std::to_string(i));
        for (const auto &name : actions) {
            if (!result.nodes.contains(name)) continue;
            auto &node = result.nodes.at(name);
            if (node.value("custom_action", "") != "GuardedAction" &&
                node.value("custom_action", "") != "RunChild") continue;
            // 仅本层成功后继可正常返回；错误不能跳过失败记录，也不能越出子图。
            if (node.contains("next")) node["next"].insert(node["next"].begin(), "TaskStop");
        }
        result.validate();
    }
    return result;
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
    graph.use_dialogue(task.dialogue_policy);
    const auto task_entry = graph.append("Task", task, {"Terminal"});
    const auto stop = task_stop_condition(task.dialogue_policy);
    // Boot停点证明回到该任务的稳定游戏页；仍须回任务入口，由任务自身确认阶段终点。
    // 停点的boot_ready是boolean-only NoHit，不能与图片放进any后当作确认许可。
    J confirmed{"RestartConfirmed"};
    if (!stop.is_null()) {
        graph.confirm("RestartAtTaskStop", "game.restart", "game_restarted", stop, {task_entry});
        confirmed.insert(confirmed.begin(), "RestartAtTaskStop");
    }
    graph.confirm("RestartConfirmed", "game.restart", "game_restarted", {{"mode", "boot_ready"}}, {task_entry});
    const auto boot = graph.append("Boot", boot_workflow(allow_download, false, task.dialogue_policy), confirmed);
    // Boot 在正常候选链不是默认动作；恢复策略只在新代次选择这个已封存入口。
    graph.route("Entry", {task_entry, boot});
    return graph.finish();
}
void register_recovery(runtime::BehaviorRegistry &registry) {
    registry.add_recovery({"wvd.recovery", "1"}, decide);
}
contracts::BehaviorBinding recovery_binding(const devices::LifecycleTarget &t, const J &profile, bool force) {
    // 先按unsigned本身检查上界，不能让混合符号JSON比较或后续int64转换把超大值变成负数。
    if (!profile.is_object() || !profile.contains("AUTO_START_CLASH") ||
        !profile.at("AUTO_START_CLASH").is_boolean() || !profile.contains("MAX_CRASH_LIMIT") ||
        !profile.at("MAX_CRASH_LIMIT").is_number_integer() ||
        (profile.at("MAX_CRASH_LIMIT").is_number_unsigned() &&
            profile.at("MAX_CRASH_LIMIT").get<std::uint64_t>() >
                static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)())))
        throw std::runtime_error("WVD_RECOVERY_PROFILE_INVALID");
    const bool vpn = profile.at("AUTO_START_CLASH").get<bool>();
    // 配置只选择已授权能力，不能授予VPN权限或推断应用身份。
    if (vpn && (!t.vpn_required || t.vpn_application_id.empty()))
        throw std::runtime_error("WVD_VPN_TARGET_NOT_AUTHORIZED");
    return {"WvdRecovery", {"wvd.recovery", "1"}, {{"device_id", t.device_id}, {"instance_id", t.instance_id},
        {"application_id", t.application_id}, {"vpn_application_id", t.vpn_application_id},
        {"vpn_required", vpn}, {"force_restart_instance", force},
        {"max_crashes", profile.at("MAX_CRASH_LIMIT").get<std::int64_t>()}}};
}
void bind_initial_vpn(runtime::RunDefinition &run, const devices::LifecycleTarget &target, const J &profile) {
    const auto expected = recovery_binding(target, profile);
    if (!run.state_factory || run.state_factory->implementation.id != "wvd.state" ||
        run.state_factory->parameters.at("profile") != profile)
        throw std::runtime_error("WVD_INITIAL_PROFILE_MISMATCH");
    if (run.initial.lifecycle)
        throw std::runtime_error("WVD_INITIAL_LIFECYCLE_ALREADY_BOUND");
    for (const auto &unit : run.continuation_units)
        if (unit.lifecycle)
            throw std::runtime_error("LIFECYCLE_REQUIRES_RECOVERY_BOUNDARY");
    if (run.recover) {
        if (run.recover->implementation.id != "wvd.recovery" || run.recover->implementation.revision != "1")
            throw std::runtime_error("WVD_INITIAL_RECOVERY_MISMATCH");
        for (const auto *field : {"device_id", "instance_id", "application_id", "vpn_application_id", "vpn_required", "max_crashes"})
            if (!run.recover->parameters.contains(field) || run.recover->parameters.at(field) != expected.parameters.at(field))
                throw std::runtime_error("WVD_INITIAL_RECOVERY_MISMATCH");
    }
    if (!profile.at("AUTO_START_CLASH").get<bool>())
        return;
    if (!run.recover || !run.recovery_limit)
        throw std::runtime_error("WVD_INITIAL_RECOVERY_REQUIRED");
    if (target.device_id != run.policy.device_id || target.application_id != run.policy.application_id)
        throw std::runtime_error("LIFECYCLE_NOT_AUTHORIZED");
    // 只挂已选定的首段；原入口、检查点及同revision正常续段保持不变。
    devices::LifecyclePlan plan;
    plan.target = target;
    plan.attempt = 1;
    plan.operations = {O::EnsureVpn};
    run.initial.lifecycle = std::move(plan);
}
}
