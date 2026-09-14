#include "state.hpp"
#include <algorithm>

namespace wvd::games {
using J = nlohmann::json;
WvdRunState::WvdRunState(J profile, const contracts::StateCreationContext &creation)
    : profile_(std::move(profile)),
      identity_(creation.instance_id + ":" + std::to_string(creation.run_id)),
      clock_(creation.clock), strategy_(profile_) {
    if (!clock_ || creation.instance_id.empty() || !creation.run_id)
        throw std::runtime_error("WVD_STATE_CONTEXT_INVALID");
    if (!profile_.at("RELOAD_STRATEGY_WHEN").is_string() || !profile_.at("LANGUAGE").is_string() ||
        !profile_.at("MAX_CRASH_LIMIT").is_number_integer())
        throw std::runtime_error("WVD_STATE_PROFILE_INVALID");
    started_ = clock_->now();
    strategy_.reload(task_step_);
}
bool WvdRunState::setting_is(const char *name, const char *zh, const char *en) const {
    return profile_.at(name) == (profile_.at("LANGUAGE") == "en_US" ? en : zh);
}
void WvdRunState::on_segment(contracts::SegmentBoundary, std::uint64_t generation,
                             std::size_t unit) {
    if (generation <= generation_)
        throw std::runtime_error("WVD_STATE_GENERATION_REUSED");
    generation_ = generation;
    unit_index_ = unit;
    prepared_.reset();
    prepared_portrait_.clear();
    // 正常段续接不补满策略；恢复边界也不等于游戏已重启，具体业务调用 restart_game。
}
void WvdRunState::enter_dungeon() {
    prepared_.reset();
    task_step_ = 0;
    need_initial_recover_ = true;
    if (setting_is("RELOAD_STRATEGY_WHEN", "每次副本开始", "Dungeon start"))
        strategy_.reload(task_step_);
}
void WvdRunState::target_point_completed() {
    prepared_.reset();
    ++task_step_;
    if (strategy_.uses_task_points())
        strategy_.reload(task_step_);
}
void WvdRunState::observe_combat() {
    if (!combat_started_)
        combat_started_ = clock_->now();
    pending_combat_ = true;
}
void WvdRunState::observe_chest() {
    if (!chest_started_)
        chest_started_ = clock_->now();
    pending_chest_ = true;
}
void WvdRunState::resume_dungeon() {
    prepared_.reset();
    auto now = clock_->now();
    const double combat =
        combat_started_ ? std::chrono::duration<double>(now - *combat_started_).count() : 0;
    const double chest =
        chest_started_ ? std::chrono::duration<double>(now - *chest_started_).count() : 0;
    if (combat < 0 || chest < 0)
        throw std::runtime_error("WVD_CLOCK_MOVED_BACKWARD");
    if (combat_started_ && setting_is("RELOAD_STRATEGY_WHEN", "每场战斗前", "Before combat"))
        strategy_.reload(task_step_);
    // 保留旧版重叠计时的“较长者扣除较短者”口径，不能无说明地改变统计。
    combat_seconds_ += combat && chest && combat > chest ? combat - chest : combat;
    chest_seconds_ += combat && chest && chest >= combat ? chest - combat : chest;
    combat_started_.reset();
    chest_started_.reset();
    if (pending_combat_) {
        ++combats_;
        met_encounter_ = true;
        pending_combat_ = false;
    }
    if (pending_chest_) {
        ++chests_;
        met_encounter_ = true;
        pending_chest_ = false;
    }
}
void WvdRunState::resurrected() {
    prepared_.reset();
    recover_after_rez_ = true;
    strategy_.reload(task_step_);
}
void WvdRunState::restart_game() {
    prepared_.reset();
    combat_speed_ = false;
    zoom_world_map_ = false;
    bypass_after_restart_ = false;
    combat_started_.reset();
    chest_started_.reset();
    ++crashes_;
    if (static_cast<std::int64_t>(crashes_) > profile_.at("MAX_CRASH_LIMIT").get<std::int64_t>())
        crashes_ = 0;
    strategy_.reload(task_step_);
}
void WvdRunState::dungeon_completed() {
    if (met_encounter_) {
        ++dungeons_;
        met_encounter_ = false;
    }
    const auto now = clock_->now();
    if (lap_started_) {
        const auto elapsed = std::chrono::duration<double>(now - *lap_started_).count();
        if (elapsed < 0)
            throw std::runtime_error("WVD_CLOCK_MOVED_BACKWARD");
        total_seconds_ += elapsed;
    }
    lap_started_ = now;
}
void WvdRunState::bag_clear_completed() {
    last_bag_clear_ = static_cast<std::size_t>(total_seconds_ / (6 * 3600));
}
supply::RestDecision WvdRunState::rest_decision(bool pickaxes_exhausted) const {
    return supply::decide_rest(
        profile_, {dungeons_, met_encounter_, total_seconds_, last_bag_clear_}, pickaxes_exhausted);
}
std::optional<SkillSelection>
WvdRunState::select_skill(const std::vector<PortraitScore> &scores) const {
    auto selected = strategy_.select(scores);
    if (selected) {
        selected->run_identity = identity_;
        selected->generation = generation_;
    }
    return selected;
}
bool WvdRunState::confirm_skill(const SkillSelection &selection, SkillOutcome outcome) {
    if (selection.run_identity != identity_ || selection.generation != generation_)
        throw std::runtime_error("STALE_BUSINESS_SELECTION");
    return strategy_.consume(selection, outcome);
}
void WvdRunState::prepare_skill(const std::vector<PortraitScore> &scores, const J &catalog) {
    if (!catalog.is_array() || catalog.size() > 128)
        throw std::runtime_error("COMBAT_SKILL_CATALOG_INVALID");
    auto selected = select_skill(scores);
    prepared_.reset();
    prepared_portrait_.clear();
    if (!selected)
        return;
    auto found = std::find(catalog.begin(), catalog.end(), selected->skill);
    if (found == catalog.end())
        throw std::runtime_error("COMBAT_SKILL_NOT_COMPILED");
    const auto role = selected->skill.at("role_var").get<std::string>();
    double best = -1;
    for (const auto &candidate : {role, role + "_sp", role + "_alt"})
        for (const auto &score : scores)
            if (score.portrait == candidate && score.score > best) {
                best = score.score;
                prepared_portrait_ = candidate;
            }
    prepared_index_ = static_cast<std::size_t>(std::distance(catalog.begin(), found));
    prepared_ = std::move(selected);
}
bool WvdRunState::finish_prepared_skill(std::size_t index, SkillOutcome outcome) {
    if (!prepared_ || index != prepared_index_)
        throw std::runtime_error("COMBAT_PREPARED_SELECTION_MISSING");
    const bool consumed = confirm_skill(*prepared_, outcome);
    if (consumed)
        prepared_.reset();
    return consumed;
}
std::string WvdRunState::confirmation_id(const std::string &operation, const std::string &event) const {
    auto id = identity_ + ":" + std::to_string(unit_index_) + ":" + operation;
    // 同一遭遇中的重复识别共用 ID，回到地下城确认结束后才开始下一次遭遇。
    // 不用帧号/代次作 ID：它们会让重试重复计数；也不能只用节点名吞掉第二场。
    if (event == "combat_observed")
        id += ":combat:" + std::to_string(combats_ + 1);
    else if (event == "chest_observed")
        id += ":chest:" + std::to_string(chests_ + 1);
    else if (event == "dungeon_resumed")
        id += ":resume:" + std::to_string(combats_ + (pending_combat_ ? 1 : 0)) + ":" +
              std::to_string(chests_ + (pending_chest_ ? 1 : 0));
    return id;
}
bool WvdRunState::confirm_event(const std::string &operation, const std::string &event,
                                 std::uint64_t generation, std::uint64_t frame_id,
                                 std::optional<std::size_t> expected_step) {
    if (operation.empty() || operation.size() > 256 || generation != generation_ || !frame_id)
        throw std::runtime_error("BUSINESS_CONFIRMATION_IDENTITY_INVALID");
    const J effect{{"event", event}, {"expected_step", expected_step ? J(*expected_step) : J(nullptr)}};
    if (auto old = confirmations_.find(operation); old != confirmations_.end()) {
        if (old->second != effect)
            throw std::runtime_error("BUSINESS_OPERATION_CONFLICT");
        return false;
    }
    if (confirmations_.size() >= 4096)
        throw std::runtime_error("BUSINESS_CONFIRMATION_CAPACITY");
    if (expected_step && *expected_step != task_step_)
        throw std::runtime_error("BUSINESS_TASK_STEP_MISMATCH");
    if (event == "target_completed") {
        if (!expected_step)
            throw std::runtime_error("BUSINESS_TASK_STEP_REQUIRED");
        target_point_completed();
    } else if (event == "dungeon_entered")
        enter_dungeon();
    else if (event == "combat_observed")
        observe_combat();
    else if (event == "chest_observed")
        observe_chest();
    else if (event == "dungeon_resumed")
        resume_dungeon();
    else if (event == "dungeon_completed")
        dungeon_completed();
    else if (event == "resurrected")
        resurrected();
    else
        throw std::runtime_error("BUSINESS_EVENT_UNKNOWN");
    confirmations_.emplace(operation, effect);
    last_confirmation_ = {{"operation_id", operation}, {"event", event},
                           {"generation", generation}, {"frame_id", frame_id}};
    return true;
}
J WvdRunState::summarize() const {
    return {{"kind", "wvd"},
            {"state_revision", "1"},
            {"run_identity", identity_},
            {"generation", generation_},
            {"unit_index", unit_index_},
            {"task_step", task_step_},
            {"strategy", strategy_.summary()},
            {"has_prepared_skill", prepared_.has_value()},
            {"prepared_skill_index", prepared_ ? J(prepared_index_) : J(nullptr)},
            {"prepared_portrait", prepared_ ? prepared_portrait_ : ""},
            {"dungeons", dungeons_},
            {"combats", combats_},
            {"chests", chests_},
            {"crashes", crashes_},
            {"elapsed_seconds", std::chrono::duration<double>(clock_->now() - started_).count()},
            {"combat_seconds", combat_seconds_},
            {"total_seconds", total_seconds_},
            {"chest_seconds", chest_seconds_},
            {"last_bag_clear", last_bag_clear_},
            {"confirmed_operations", confirmations_.size()},
            {"last_confirmation", last_confirmation_},
            {"combat_timer_active", combat_started_.has_value()},
            {"chest_timer_active", chest_started_.has_value()},
            {"pending_combat", pending_combat_},
            {"pending_chest", pending_chest_},
            {"met_encounter", met_encounter_},
            {"need_initial_recover", need_initial_recover_},
            {"recover_after_rez", recover_after_rez_},
            {"combat_speed", combat_speed_},
            {"zoom_world_map", zoom_world_map_},
            {"bypass_after_restart", bypass_after_restart_}};
}
namespace {
std::unique_ptr<contracts::BusinessRunState>
create_state(const J &parameters, const contracts::StateCreationContext &creation) {
    return std::make_unique<WvdRunState>(parameters.at("profile"), creation);
}
} // namespace
void register_wvd_state(runtime::BehaviorRegistry &registry) {
    registry.add_state_factory({"wvd.state", "1"}, create_state);
}
contracts::BehaviorBinding wvd_state_binding(const J &profile) {
    return {"WvdState", {"wvd.state", "1"}, {{"profile", profile}}};
}
} // namespace wvd::games
