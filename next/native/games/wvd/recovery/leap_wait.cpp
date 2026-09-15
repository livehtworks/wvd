#include "leap_wait.hpp"
#include "games/wvd/state.hpp"
#include <algorithm>
#include <thread>

namespace wvd::games::recovery {
using J = nlohmann::json;
using namespace std::chrono_literals;
std::chrono::milliseconds LeapWait::elapsed(TimePoint now) const {
    if (!started_)
        return 0ms;
    if (now < *started_ || (last_poll_ && now < *last_poll_))
        throw std::runtime_error("WVD_CLOCK_MOVED_BACKWARD");
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - *started_);
}
void LeapWait::begin(TimePoint now) {
    if (active_)
        throw std::runtime_error("LEAP_WAIT_ALREADY_ACTIVE");
    started_ = now; // 零时刻也是有效起点，不能用 time_since_epoch()==0 判空。
    last_poll_ = now;
    generation_ = 0;
    slices_ = 0;
    deadline_ = 0s;
    active_ = true;
}
bool LeapWait::poll(TimePoint now, std::uint64_t generation) {
    if (!active_ || !generation)
        throw std::runtime_error("LEAP_WAIT_NOT_ACTIVE");
    const auto spent = elapsed(now);
    if (generation != generation_) {
        if (generation < generation_ || slices_ >= max_slices ||
            (generation_ && spent < deadline_))
            throw std::runtime_error("LEAP_WAIT_SEGMENT_INVALID");
        generation_ = generation;
        ++slices_;
        // 截止点相对于原意图，段间清理/连接的耗时不会让7300秒重新起算。
        deadline_ = std::min(duration, slice * static_cast<std::chrono::seconds::rep>(slices_));
    }
    last_poll_ = now;
    return spent >= deadline_;
}
void LeapWait::restarted(TimePoint now) {
    if (!active_ || elapsed(now) < duration)
        throw std::runtime_error("LEAP_WAIT_RESTART_TOO_EARLY");
    active_ = false;
    last_poll_ = now;
}
J LeapWait::summary(TimePoint now) const {
    const auto spent = elapsed(now);
    return {{"active", active_}, {"started", started_.has_value()},
            {"elapsed_ms", spent.count()}, {"ready", active_ && spent >= duration},
            {"slices", slices_}, {"generation", generation_},
            {"deadline_ms", std::chrono::duration_cast<std::chrono::milliseconds>(deadline_).count()},
            {"duration_ms", 7300000}, {"slice_ms", 1460000}};
}
namespace {
bool wait(maafw::Context &context, const J &, const J &) {
    if (context.depth() != 0 || context.node() != "LeapWait_Entry")
        throw std::runtime_error("LEAP_WAIT_ROOT_REQUIRED");
    bool announced = false;
    while (!context.cancelled()) {
        bool boundary = false;
        J progress;
        const auto accepted = context.with_business_state([&](contracts::BusinessRunState &base) {
            if (context.cancelled())
                return false;
            auto &state = dynamic_cast<WvdRunState &>(base);
            boundary = state.poll_leap_wait();
            progress = state.summary().at("leap_wait");
            return true;
        });
        if (!accepted)
            return false;
        if (!announced || boundary) {
            context.business_event(boundary ? "leap.wait_boundary" : "leap.wait_started", progress);
            announced = true;
        }
        if (boundary)
            return !context.cancelled();
        // 这里只保证本协作等待每25ms检查取消，不替SDK、连接或清理的阻塞作保证。
        std::this_thread::sleep_for(25ms);
    }
    return false;
}
const contracts::BehaviorBinding &binding(const runtime::SessionDefinition &session) {
    const auto found = std::find_if(session.actions.begin(), session.actions.end(),
        [](const auto &item) { return item.name == "WvdLeapWait"; });
    if (found == session.actions.end() || found->implementation.id != "wvd.leap_wait" ||
        found->implementation.revision != "1")
        throw std::runtime_error("LEAP_WAIT_NOT_BOUND");
    return *found;
}
}
void register_leap_wait(runtime::BehaviorRegistry &registry) {
    registry.add_action({"wvd.leap_wait", "1"}, wait);
}
J leap_wait_nodes() {
    return {{"LeapWait_Entry", {{"recognition", "DirectHit"}, {"action", "Custom"},
                {"custom_action", "WvdLeapWait"}, {"next", {"LeapWait_Boundary"}},
                {"max_hit", 1}, {"pre_delay", 0}, {"post_delay", 0}}},
            {"LeapWait_Boundary", {{"recognition", "DirectHit"}, {"action", "Custom"},
                {"custom_action", "RequireRecovery"},
                {"custom_action_param", {{"reason", "leap.wait_boundary"}}},
                {"max_hit", 1}, {"pre_delay", 0}, {"post_delay", 0}}}};
}
void bind_leap_wait(runtime::RunDefinition &definition) {
    if (!definition.state_factory || !definition.recover || definition.recovery_limit < 3 ||
        definition.recovery_limit > 16 - LeapWait::max_slices)
        throw std::runtime_error("LEAP_WAIT_RECOVERY_BUDGET_INVALID");
    auto attach = [](runtime::SessionDefinition &session) {
        if (session.time_limit <= 0ms || session.time_limit > 1800s)
            throw std::runtime_error("LEAP_WAIT_RESUME_BUDGET_INVALID");
        for (const auto &item : session.actions)
            if (item.name == "WvdLeapWait")
                throw std::runtime_error("LEAP_WAIT_ALREADY_BOUND");
        session.actions.push_back({"WvdLeapWait", {"wvd.leap_wait", "1"},
            {{"resume_time_limit_ms", session.time_limit.count()}}});
    };
    // 先在副本校验全部段，失败不能留下半绑定定义。
    auto copy = definition;
    attach(copy.initial);
    for (auto &session : copy.continuation_units)
        attach(session);
    copy.recovery_limit += LeapWait::max_slices;
    definition = std::move(copy);
}
runtime::SessionDefinition leap_wait_session(const runtime::SessionDefinition &previous) {
    (void)binding(previous);
    auto next = previous;
    next.entry = "LeapWait_Entry";
    next.lifecycle.reset();
    next.time_limit = 1500s; // 每段1460秒，保留40秒启动余量；绝不放宽到7300秒。
    return next;
}
void restore_leap_session_budget(runtime::SessionDefinition &session) {
    const auto limit = binding(session).parameters.at("resume_time_limit_ms").get<std::int64_t>();
    if (limit <= 0 || limit > 1800000)
        throw std::runtime_error("LEAP_WAIT_RESUME_BUDGET_INVALID");
    session.time_limit = std::chrono::milliseconds{limit};
}
} // namespace wvd::games::recovery
