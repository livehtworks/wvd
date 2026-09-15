#include "dialogue.hpp"
#include "games/wvd/vision/dialogue_probes.hpp"

namespace wvd::games::recovery {
tasks::CompiledWorkflow choose_special_dialogue(DialoguePolicy policy) {
    using C = tasks::PipelineCompiler;
    using J = nlohmann::json;
    if (policy == DialoguePolicy::Default) throw std::runtime_error("SPECIAL_DIALOGUE_POLICY_REQUIRED");
    C graph("recovery.special_dialogue." + dialogue_policy_name(policy), std::chrono::seconds{90});
    graph.use_dialogue(policy);
    auto close = C::image("bondmate_close");
    close["roi"] = {277, 751, 330, 600};
    const J after{{"mode", "special_dialogue_post"}};
    J entry{"Pending"};
    graph.observe("Pending", C::business("/special_dialogue_pending", true), {"Unconfirmed"});
    const auto stops = dialogue_task_stops(policy);
    J stop_images = J::array();
    for (const auto marker : stops) stop_images.push_back(C::image(std::string(marker)));
    const bool can_stop = !stops.empty();
    if (can_stop) {
        entry.push_back("TaskStop");
        graph.observe("TaskStop", {{"mode", "task_stop"}}, {"Terminal"});
    }
    std::size_t option_index = 0;
    for (const auto option : special_dialogue_options(policy)) {
        const auto suffix = std::to_string(option_index++);
        const auto marker = C::image(std::string(option));
        const J choice{{"mode", "special_dialogue"}, {"selected", option}};
        entry.push_back("Prepare" + suffix);
        graph.confirm("Prepare" + suffix, "dialogue.special.prepare", "special_dialogue_prepared", C::all({choice, marker}), {"Choose" + suffix});
        graph.click("Choose" + suffix, C::all({choice, marker}), marker, after, {"AfterChoice" + suffix});
        graph.delay_after("Choose" + suffix, 2000);
        graph.postcondition_budget("Choose" + suffix, 10000);
        auto close_scene = C::all({close, C::absent(marker)});
        J after_choice{"CloseBond" + suffix, "Completed" + suffix, "Unconfirmed"};
        J after_close{"Completed" + suffix, "Unconfirmed"};
        if (can_stop) {
            after_choice.insert(after_choice.begin(), "StoppedAfterChoice" + suffix);
            after_close.insert(after_close.begin(), "StoppedAfterChoice" + suffix);
            // 已发送选项后，停点的新帧且旧选项消失才确认其副作用。
            // 即使同时出现 bondmate_close，也只归还控制权，不继续点关闭按钮。
            graph.confirm("StoppedAfterChoice" + suffix, "dialogue.special.done", "special_dialogue_completed",
                C::all({C::any(stop_images), C::absent(marker)}), {"Terminal"});
            close_scene = C::all({close_scene, C::absent(J{{"mode", "task_stop"}})});
        }
        graph.route("AfterChoice" + suffix, after_choice);
        graph.click("CloseBond" + suffix, close_scene, close, after, after_close);
        graph.confirm("Completed" + suffix, "dialogue.special.done",
            option == "sandman/sandman_bondmate" ? "sandman_bondmate_completed" : "special_dialogue_completed",
            C::all({after, C::absent(close), C::absent(marker)}), {"Terminal"});
    }
    graph.route("Entry", entry);
    graph.recovery("Unconfirmed", "dialogue.choice_outcome_unconfirmed");
    return graph.finish();
}
tasks::CompiledWorkflow choose_default_dialogue() {
    using C = tasks::PipelineCompiler;
    using J = nlohmann::json;
    C graph("recovery.default_dialogue", std::chrono::seconds{90});
    const J known{{"mode", "dialogue_post"}};
    const J stop{{"mode", "task_stop"}};
    J candidates = J::array();
    for (std::size_t i = 0; i < vision::default_dialogue_names.size(); ++i) {
        const auto suffix = std::to_string(i);
        const auto name = std::string(vision::default_dialogue_names[i]);
        const auto marker = C::image("dialogueChoices/" + name);
        candidates.push_back("Candidate" + suffix);
        // 候选扫描只决定分支；输入前用新帧重查完整优先级，不能沿用扫描时的选项。
        graph.observe("Candidate" + suffix, C::all({marker, C::absent(stop)}), {"Choose" + suffix});
        graph.click("Choose" + suffix, {{"mode", "default_dialogue"}, {"selected", name}},
                    marker, known, {"TaskStop", "Changed" + suffix, "Unconfirmed"});
        graph.delay_after("Choose" + suffix, 2000);
        graph.postcondition_budget("Choose" + suffix, 10000);
        graph.hit_limit("Choose" + suffix, 1);
        graph.observe("Changed" + suffix, C::all({known, C::absent(marker)}), {"Terminal"});
    }
    graph.route("Entry", {"TaskStop", "Options"});
    graph.observe("TaskStop", stop, {"Terminal"});
    graph.observe("Options", {{"mode", "default_dialogue"}}, candidates);
    // 已输入但旧选项仍在，不能区分未处理和延迟处理；不重试购买/选择，不自动重启重放。
    graph.recovery("Unconfirmed", "dialogue.choice_outcome_unconfirmed");
    return graph.finish();
}
}
