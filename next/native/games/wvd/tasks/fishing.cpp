#include "fishing.hpp"
#include "games/wvd/recovery/boot.hpp"

namespace wvd::games::tasks {
CompiledWorkflow seek_fishing_position() {
    using C = PipelineCompiler;
    using J = nlohmann::json;
    C graph("quest.fishing.seek", std::chrono::seconds{180});
    const auto fishing = C::any({C::image("fishing/cast"), C::image("fishing/striking"), C::image("fishing/CloseFishInfo")});
    const auto dungeon = C::all({C::image("dungFlag"), C::absent(C::image("mapFlag")), C::absent(J{{"mode", "combat_active"}}), C::absent(fishing)});
    const auto known = C::any({dungeon, fishing});
    graph.route("Entry", {"Found", "Focus"});
    graph.observe("Found", fishing, {"Terminal"});
    graph.fixed_click("Focus", dungeon, known, {250, 1200}, {"Found", "Turn0"});
    for (int i = 0; i < 40; ++i) {
        const auto name = "Turn" + std::to_string(i);
        graph.swipe(name, dungeon, known, {250, 1200, 850, 1200}, {"Found", i == 39 ? "TurnExhausted" : "Turn" + std::to_string(i + 1)}, 100);
    }
    graph.recovery("TurnExhausted", "quest.fishing_turn_incomplete");
    graph.interrupt_on({{"mode", "blocking_screen"}, {"parallel_basic", true}}, "quest.fishing_common_screen_requires_dispatch");
    return graph.finish();
}
CompiledWorkflow collect_fishing_reward() {
    using C = PipelineCompiler;
    using J = nlohmann::json;
    C graph("quest.fishing.reward", std::chrono::seconds{60});
    const auto close = C::image("fishing/CloseFishInfo");
    const J blocked{{"mode", "blocking_screen"}, {"parallel_basic", true}};
    const auto closed = C::all({C::any({C::image("fishing/cast"), C::image("fishing/striking")}), C::absent(close), C::absent(blocked)});
    graph.route("Entry", {"Blocked", "Pending", "Prepare"});
    graph.observe("Blocked", blocked, {"BlockedExit"});
    graph.recovery("BlockedExit", "quest.fishing_common_screen_requires_dispatch");
    graph.observe("Pending", C::business("/fishing/reward_pending", true), {"Completed", "Close"});
    // 分类来自关闭前的同一新帧；完成数只在关闭后确认。恢复看到已关闭页时不再输入。
    graph.confirm("Prepare", "fishing.reward.prepare", "fishing_reward_prepared", {{"mode", "fishing_reward"}}, {"Close"});
    graph.click("Close", C::all({close, C::absent(blocked)}), close, closed, {"Completed"});
    graph.postcondition_budget("Close", 10000);
    graph.delay_after("Close", 5000);
    graph.confirm("Completed", "fishing.reward.done", "fishing_reward_completed", closed, {"Terminal"});
    return graph.finish();
}
CompiledWorkflow cast_fishing_line(bool far) {
    using C = PipelineCompiler;
    using J = nlohmann::json;
    C graph(far ? "quest.fishing.cast_far" : "quest.fishing.cast_near", std::chrono::seconds{90});
    const auto cast = C::image("fishing/cast"), striking = C::image("fishing/striking");
    const J empty{{"mode", "fishing_bait_empty"}};
    const auto ready = C::all({cast, C::absent(empty)});
    graph.route("Entry", {"NoBait", "Adjust0"});
    graph.observe("NoBait", C::all({cast, empty}), {"RefillRequired"});
    graph.recovery("RefillRequired", "quest.fishing_bait_required");
    // 原任务先右扫五次、左扫两次。每次仍重取当前场景许可，页面变化即停止，
    // 不把八次输入打成无法取消的连续命令。最后一次的后置必须是真正咬钩等待页。
    for (int i = 0; i < 7; ++i) {
        const auto name = "Adjust" + std::to_string(i);
        graph.swipe(name, ready, ready, i < 5 ? J{50, 1200, 850, 1200} : J{850, 1200, 50, 1200},
            {i == 6 ? "Cast" : "Adjust" + std::to_string(i + 1)}, 100);
    }
    graph.delay_after("Adjust6", 1000);
    graph.swipe("Cast", ready, C::all({striking, C::absent(cast)}), {400, 1200, 450, 1250}, {"Waiting"}, far ? 2250 : 4000);
    graph.postcondition_budget("Cast", 15000);
    graph.observe("Waiting", C::all({striking, C::absent(cast)}), {"Terminal"});
    graph.interrupt_on({{"mode", "blocking_screen"}, {"parallel_basic", true}}, "quest.fishing_common_screen_requires_dispatch");
    return graph.finish();
}
CompiledWorkflow fishing_round(bool far, bool allow_download) {
    using C = PipelineCompiler;
    using J = nlohmann::json;
    C graph(far ? "quest.fishing.round_far" : "quest.fishing.round_near", std::chrono::seconds{700});
    const auto cast = C::image("fishing/cast"), striking = C::image("fishing/striking"), reward = C::image("fishing/CloseFishInfo");
    const auto known = C::any({cast, striking, reward});
    const J bobber{{"mode", "fishing_bobber"}}, blocked{{"mode", "blocking_screen"}, {"parallel_basic", true}};
    const auto quiet_striking = C::all({striking, C::absent(blocked)});
    graph.route("Entry", {"PendingCast", "DispatchA"});
    graph.observe("PendingCast", C::business("/fishing/casting_pending", true), {"CastUncertain"});
    graph.recovery("CastUncertain", "quest.fishing_cast_unconfirmed");
    const auto casting = graph.define_child("Casting", cast_fishing_line(far));
    const auto collect = graph.define_child("Collect", collect_fishing_reward());
    const auto seek = graph.define_child("Seek", seek_fishing_position(), {"BlockedExit", "TurnExhausted"});
    const auto common = graph.define_child("Common", recovery::clear_common_screens(allow_download));
    graph.call_child("ClearCommon", common, {"DispatchA"});
    graph.hit_limit("ClearCommon", 16);
    graph.call_child("SeekPosition", seek, {"DispatchA"});
    graph.hit_limit("SeekPosition", 16);
    graph.recovery("UnknownTimeout", "quest.fishing_unknown_timeout");
    graph.confirm("PrepareCast", "fishing.cast.prepare", "fishing_cast_prepared", C::all({cast, C::absent(blocked)}), {"Cast"});
    graph.call_child("Cast", casting, {"CastStarted"});
    graph.confirm("CastStarted", "fishing.cast.done", "fishing_cast_completed", quiet_striking, {"DispatchA"});
    graph.delay_after("CastStarted", 10000);
    for (const auto *name : {"PrepareCast", "Cast", "CastStarted"}) graph.hit_limit(name, 16);
    graph.call_child("CollectReward", collect, {"Terminal"});
    graph.recovery("BaitExit", "quest.fishing_bait_required");
    // 两组交替等待，各自最多256次。这样保留每次1秒和300秒严格超时语义，
    // 不因单节点256次上限提前判失败，也不提高通用编译器的命中上限。
    for (const auto *side : {"A", "B"}) {
        const std::string s = side, other = s == "A" ? "B" : "A";
        graph.route("Dispatch" + s, {"Unknown" + s, "Blocked" + s, "Pending" + s, "Reward" + s, "NoBait" + s, "CastPage" + s, "TimedOut" + s, "StartWait" + s, "Reel" + s, "Wait" + s, "Dungeon" + s, "UnknownWait" + s});
        graph.observe("Unknown" + s, {{"mode", "fishing_unknown"}}, {"UnknownTimeout"});
        graph.observe("Blocked" + s, blocked, {"ClearCommon"});
        graph.observe("Dungeon" + s, C::image("dungFlag"), {"SeekPosition"});
        graph.route("UnknownWait" + s, {"Dispatch" + other});
        graph.delay_after("UnknownWait" + s, 1000);
        graph.observe("Pending" + s, C::business("/fishing/reward_pending", true), {"CollectReward"});
        graph.observe("Reward" + s, reward, {"CollectReward"});
        graph.observe("NoBait" + s, C::all({cast, J{{"mode", "fishing_bait_empty"}}}), {"BaitExit"});
        graph.observe("CastPage" + s, cast, {"PrepareCast"});
        graph.observe("TimedOut" + s, C::all({striking, C::business("/fishing/timed_out", true)}), {"Abort"});
        graph.observe("StartWait" + s, C::all({striking, C::business("/fishing/waiting", false)}), {"ObserveWait"});
        graph.swipe("Reel" + s, C::all({quiet_striking, C::absent(bobber)}), C::any({known, blocked}), {450, 700, 450, 50}, {"Dispatch" + other}, 100);
        graph.delay_after("Reel" + s, 3000);
        graph.observe("Wait" + s, C::all({striking, bobber}), {"Dispatch" + other});
        graph.delay_after("Wait" + s, 1000);
        for (const auto *prefix : {"Dispatch", "Unknown", "Blocked", "Pending", "Reward", "NoBait", "CastPage", "TimedOut", "StartWait", "Reel", "Wait", "Dungeon", "UnknownWait"})
            graph.hit_limit(std::string(prefix) + s, 256);
    }
    graph.confirm("ObserveWait", "fishing.wait", "fishing_wait_started", quiet_striking, {"DispatchA"});
    graph.click("Abort", quiet_striking, striking, C::all({C::any({cast, reward}), C::absent(striking), C::absent(blocked)}), {"FailedCast"});
    graph.delay_after("Abort", 5000);
    graph.confirm("FailedCast", "fishing.failed", "fishing_wait_failed", C::all({C::any({cast, reward}), C::absent(striking), C::absent(blocked)}), {"Terminal"});
    return graph.finish();
}
}
