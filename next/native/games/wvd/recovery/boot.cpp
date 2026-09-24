#include "boot.hpp"
#include "party_death.hpp"
#include "global_prompt.hpp"
#include "karma_prompt.hpp"
#include "dialogue.hpp"
#include "games/wvd/vision/harken_probes.hpp"

namespace wvd::games::recovery {
namespace {
using J = nlohmann::json;
using C = tasks::PipelineCompiler;
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
    // 资源下载弹窗同样跟随系统区域设置。繁中模板来自 900x1600 真实页面，
    // 只匹配“開始下載”按钮文字；保留英文模板，不通过降低阈值混淆语言版本。
    const auto download_en = scoped("startdownload", {222, 901, 465, 84}, .8);
    const auto download_zh_hant =
        scoped("startdownload_zh_hant", {222, 901, 465, 84}, .86);
    const auto download = C::any({download_en, download_zh_hant});
    const auto retry = C::image("retry");
    auto blank = C::image("retry_blank");
    blank["threshold"] = .65;
    auto low_retry = retry;
    low_retry["threshold"] = .60;
    const auto to_title = C::image("totitle"), resume = C::image("resume");
    J recognized = common ? C::any({J{{"mode", "boot_post"}}, panel}) : J{{"mode", "boot_post"}};
    if (!task_stop.is_null())
        recognized = C::any({task_stop, recognized});
    // 这里只等待离开刚处理的提示；它不是“游戏就绪”的证据。
    // recognized 包含当前提示，不能放进 any 后把页面未变化认作进展。
    const auto progressed = [](const J &current) { return C::absent(current); };
    J entry = common ? J{"DownloadEn", "DownloadZhHant", "RetryBlank", "Retry", "RetryLow", "ReturnTitle", "Resume", "Attention", "Title", "Pause", "Death", "Sandman", "Blessing", "Karma", "Dialogue", "Defeat", "Ready", "Poll"}
                     : J{"Ready", "DownloadEn", "DownloadZhHant", "RetryBlank", "Retry", "RetryLow", "ReturnTitle", "Resume", "Attention", "Title", "Pause", "Sandman", "Blessing", "Karma", "Dialogue", "Poll"};
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
    // 应用刚切到前台时可能仍是黑帧，免责声明也可能在首轮候选扫描后才出现。
    // NoHit 只做有界等待后重扫；任何识别 Error 仍通过各节点 on_error 立即退出。
    graph.wait("Poll", 500, {"Entry"});
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
        const auto marker = prompt == GlobalPrompt::Blessing
            ? C::any({vision::harken_buff_menu(), C::image("blessing")}) : C::image("sandman_recover");
        graph.observe(name, marker, {name + "Handle"});
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
    if (allow_download) {
        graph.click("DownloadEn", download_en, download_en, progressed(download), {"Entry"});
        graph.click("DownloadZhHant", download_zh_hant, download_zh_hant,
                    progressed(download), {"Entry"});
    } else {
        graph.observe("DownloadEn", download_en, {"DownloadBlocked"});
        graph.observe("DownloadZhHant", download_zh_hant, {"DownloadBlocked"});
        graph.recovery("DownloadBlocked", "boot.download_permission_missing");
    }
    graph.click("RetryBlank", blank, blank, progressed(blank), {"Entry"}, {0, 103});
    graph.click("Retry", retry, retry, progressed(retry), {"Entry"});
    graph.fixed_click("RetryLow", low_retry, progressed(low_retry), {450, 900}, {"Entry"});
    graph.click("ReturnTitle", to_title, to_title, progressed(to_title), {"Entry"});
    graph.click("Resume", resume, resume, progressed(resume), {"Entry"});
    graph.fixed_click("Attention", attention, progressed(attention), {450, 1450}, {"Entry"});
    graph.fixed_click("Title", title, progressed(title), {450, 1450}, {"Entry"});
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
    // 120 秒工作流总预算是最终上限；命中次数只防止观察节点过早结束轮询。
    graph.hit_limit("Entry", 240);
    graph.hit_limit("Poll", 240);
    for (auto name : {"DownloadEn", "DownloadZhHant", "RetryBlank", "Retry", "RetryLow", "ReturnTitle", "Resume", "Attention", "Title"}) {
        if (!allow_download && (std::string(name) == "DownloadEn" ||
                               std::string(name) == "DownloadZhHant"))
            continue;
        graph.hit_limit(name, 6);
        graph.delay_after(name, 1500);
        graph.postcondition_budget(name, 120000);
    }
    auto result = graph.finish();
    if (!task_stop.is_null()) {
        // 候选命中后到输入前仍可能出现停点，所有输入都用新帧重新排除。
        for (auto &node : result.nodes) {
            if (node.value("binding", "") != "Input") continue;
            auto &scene = node["operation_args"]["scene_recognition"]["parameters"];
            scene = C::all({scene, C::absent(task_stop)});
            node["observation_args"] = C::all({node.at("observation_args"), C::absent(task_stop)});
        }
        // 动作可能直接到达停点；例如Pause动作的后继不能继续点击或先选对话。
        std::vector<std::string> actions{"ChooseSpecial", "ChooseDialogue", "ChooseKarma",
            "SandmanHandle", "BlessingHandle", "DismissDeath", "AcknowledgeDefeat",
            "DownloadEn", "DownloadZhHant",
            "RetryBlank", "Retry", "RetryLow", "ReturnTitle", "Resume", "Attention", "Title"};
        for (unsigned i = 0; i < 6; ++i) actions.push_back("ResumePause" + std::to_string(i));
        for (const auto &name : actions) {
            if (!result.nodes.contains(name)) continue;
            auto &node = result.nodes.at(name);
            if (node.value("binding", "") != "Input" &&
                node.value("binding", "") != "Call") continue;
            // 仅本层成功后继可正常返回；错误不能跳过失败记录，也不能越出子图。
            if (node.contains("next")) node["next"].insert(node["next"].begin(), "TaskStop");
        }
        result.validate();
    }
    // 该图可能被内联进 30 分钟任务。入口轮询不能每轮重置原版 120 秒启动总预算。
    // 阶段只绑定当前原生 task 与会话，绝不持有游戏存档或假装回滚服务端状态。
    const auto phase = common ? "wvd.common-screen" : "wvd.boot";
    auto &phase_entry = result.nodes.at(result.entry);
    phase_entry["operation"] = "Registered";
    phase_entry["binding"] = "BeginObservationPhase";
    phase_entry["operation_args"] = {{"phase", phase}, {"budget_ms", 120000}};
    const std::string phase_end = "ObservationPhaseEnd";
    if (result.nodes.contains(phase_end)) throw std::runtime_error("BOOT_PHASE_NODE_COLLISION");
    for (auto &node : result.nodes)
        for (const auto *key : {"next", "on_error"})
            if (node.contains(key))
                for (auto &edge : node[key])
                    if (edge == result.terminal) edge = phase_end;
    result.nodes[phase_end] = {{"operation", "Registered"}, {"binding", "EndObservationPhase"},
        {"operation_args", {{"phase", phase}}}, {"next", {result.terminal}},
        {"on_error", {"RecoveryRequired"}}, {"pre_delay", 0}, {"post_delay", 0},
        {"rate_limit", 50}, {"timeout", 120000}, {"max_hit", 1}};
    result.validate();
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
    J confirmed{"RecoveredBoot", task_entry};
    graph.observe("RecoveredBoot", C::business("/lifecycle_recovery_active", true),
                  {"RestartConfirmed"});
    if (!stop.is_null()) {
        graph.confirm("RestartAtTaskStop", "game.restart", "game_restarted",
                      C::all({C::business("/lifecycle_recovery_active", true), stop}), {task_entry});
        confirmed.insert(confirmed.begin(), "RestartAtTaskStop");
    }
    graph.confirm("RestartConfirmed", "game.restart", "game_restarted", {{"mode", "boot_ready"}}, {task_entry});
    const auto boot = graph.append("Boot", boot_workflow(allow_download, false, task.dialogue_policy), confirmed);
    // 正常首段也必须先处理启动页。Task_Entry 常为 DirectHit，放在前面会使 Boot 永远不可达。
    // 非恢复首段不执行 game_restarted，避免把首次进入误记成崩溃/重置策略。
    graph.route("Entry", {boot});
    auto result = graph.finish();
    result.authoring = task.authoring;
    if (result.authoring.contains("source_paths")) {
        J renamed = J::object();
        for (const auto &[name, path] : result.authoring.at("source_paths").items()) renamed["Task_" + name] = path;
        result.authoring["source_paths"] = std::move(renamed);
    }
    return result;
}
}
