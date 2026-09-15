#include "dialogue.hpp"
#include "games/wvd/vision/dialogue_probes.hpp"

namespace wvd::games::recovery {
tasks::CompiledWorkflow choose_default_dialogue() {
    using C = tasks::PipelineCompiler;
    using J = nlohmann::json;
    C graph("recovery.default_dialogue", std::chrono::seconds{90});
    const J known{{"mode", "boot_post"}};
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
