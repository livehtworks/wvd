#include "dialogue.hpp"
#include "games/wvd/vision/dialogue_probes.hpp"

namespace wvd::games::recovery {
tasks::CompiledWorkflow choose_special_dialogue(DialoguePolicy policy) {
    using C = tasks::PipelineCompiler;
    using J = nlohmann::json;
    if (policy != DialoguePolicy::Jier) throw std::runtime_error("SPECIAL_DIALOGUE_POLICY_REQUIRED");
    C graph("recovery.jier_dialogue", std::chrono::seconds{90});
    graph.use_dialogue(policy);
    const auto marker = C::image("bounty/cuthimdown");
    auto close = C::image("bondmate_close");
    close["roi"] = {277, 751, 330, 600};
    const J choice{{"mode", "special_dialogue"}};
    const J after{{"mode", "special_dialogue_post"}};
    graph.route("Entry", {"Pending", "Prepare"});
    graph.observe("Pending", C::business("/special_dialogue_pending", true), {"AfterChoice"});
    graph.confirm("Prepare", "dialogue.special.prepare", "special_dialogue_prepared", C::all({choice, marker}), {"Choose"});
    graph.click("Choose", C::all({choice, marker}), marker, after, {"AfterChoice"});
    graph.delay_after("Choose", 2000);
    graph.postcondition_budget("Choose", 10000);
    graph.route("AfterChoice", {"CloseBond", "Completed", "Unconfirmed"});
    graph.click("CloseBond", C::all({close, C::absent(marker)}), close, after, {"Completed", "Unconfirmed"});
    graph.confirm("Completed", "dialogue.special.done", "special_dialogue_completed",
        C::all({after, C::absent(close), C::absent(marker)}), {"Terminal"});
    graph.recovery("Unconfirmed", "dialogue.choice_outcome_unconfirmed");
    return graph.finish();
}
tasks::CompiledWorkflow choose_default_dialogue() {
    using C = tasks::PipelineCompiler;
    using J = nlohmann::json;
    C graph("recovery.default_dialogue", std::chrono::seconds{90});
    const J known{{"mode", "dialogue_post"}};
    J candidates = J::array();
    for (std::size_t i = 0; i < vision::default_dialogue_names.size(); ++i) {
        const auto suffix = std::to_string(i);
        const auto name = std::string(vision::default_dialogue_names[i]);
        const auto marker = C::image("dialogueChoices/" + name);
        candidates.push_back("Candidate" + suffix);
        // 候选扫描只决定分支；输入前用新帧重查完整优先级，不能沿用扫描时的选项。
        graph.observe("Candidate" + suffix, marker, {"Choose" + suffix});
        graph.click("Choose" + suffix, {{"mode", "default_dialogue"}, {"selected", name}},
                    marker, known, {"Changed" + suffix, "Unconfirmed"});
        graph.delay_after("Choose" + suffix, 2000);
        graph.postcondition_budget("Choose" + suffix, 10000);
        graph.hit_limit("Choose" + suffix, 1);
        graph.observe("Changed" + suffix, C::all({known, C::absent(marker)}), {"Terminal"});
    }
    graph.observe("Entry", {{"mode", "default_dialogue"}}, candidates);
    // 已输入但旧选项仍在，不能区分未处理和延迟处理；不重试购买/选择，不自动重启重放。
    graph.recovery("Unconfirmed", "dialogue.choice_outcome_unconfirmed");
    return graph.finish();
}
}
