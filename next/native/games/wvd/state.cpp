#include "state.hpp"
#include "tasks/handoff_provenance.hpp"
#include <algorithm>

namespace wvd::games {
using J = nlohmann::json;
WvdRunState::WvdRunState(J profile, const contracts::StateCreationContext &creation,
                       std::unique_ptr<KarmaCommitPort> karma_writer, J handoff_source)
    : profile_(std::move(profile)),
      identity_(creation.instance_id + ":" + std::to_string(creation.run_id)),
      clock_(creation.clock), handoff_source_(std::move(handoff_source)),
      strategy_(profile_), karma_writer_(std::move(karma_writer)),
      karma_value_(profile_.at("KARMA_ADJUST").get<std::string>()) {
    if (!clock_ || creation.instance_id.empty() || !creation.run_id)
        throw std::runtime_error("WVD_STATE_CONTEXT_INVALID");
    if (!profile_.at("RELOAD_STRATEGY_WHEN").is_string() || !profile_.at("LANGUAGE").is_string() ||
        !profile_.at("MAX_CRASH_LIMIT").is_number_integer() ||
        !profile_.at("FARM_TARGET_TEXT").is_string())
        throw std::runtime_error("WVD_STATE_PROFILE_INVALID");
    started_ = clock_->now();
    if (!handoff_source_.is_null())
        tasks::validate_handoff_source(handoff_source_, profile_);
    strategy_.reload(task_step_);
}
bool WvdRunState::observe_unknown_leap(std::uint64_t samples, std::uint64_t generation,
                                      std::uint64_t frame_id) {
    if (!generation || generation != generation_ || !frame_id)
        throw std::runtime_error("LEAP_OBSERVATION_STALE");
    if (samples < 5)
        return false;
    if (!handoff_intent_.is_null())
        return false;
    if (tasks::handoff_has_unconfirmed_effect(summarize()))
        throw std::runtime_error("LEAP_SIDE_EFFECT_UNCONFIRMED");
    const bool money = profile_.at("ACTIVE_BEG_MONEY").get<bool>();
    if (money && handoff_source_.is_null())
        throw std::runtime_error("HANDOFF_SOURCE_REQUIRED");
    ++leap_sequence_;
    // 此时只记录意图，不能生成request_id或调用start；旧Run仍拥有设备和结果。
    handoff_intent_ = {{"kind", money ? "turn_to_7000G" : "wait_7300"},
        {"intent_id", identity_ + ":leap:" + std::to_string(leap_sequence_)},
        {"origin", "legacy/src/script.py:2238"}, {"run_identity", identity_},
        {"generation", generation}, {"frame_id", frame_id}, {"unknown_samples", samples}};
    if (!money)
        leap_wait_.begin(clock_->now());
    return true;
}
bool WvdRunState::poll_leap_wait() {
    if (!handoff_intent_.is_object() || handoff_intent_.at("kind") != "wait_7300")
        throw std::runtime_error("LEAP_WAIT_INTENT_REQUIRED");
    return leap_wait_.poll(clock_->now(), generation_);
}
bool WvdRunState::setting_is(const char *name, const char *zh, const char *en) const {
    return profile_.at(name) == (profile_.at("LANGUAGE") == "en_US" ? en : zh);
}
bool WvdRunState::giant_rest_due() const {
    if (!giant_unit_ || inn_rest_completed_)
        return false;
    const auto interval = profile_.at("REST_INTERVEL").get<std::int64_t>();
    if (interval < 0 || !dungeons_)
        throw std::runtime_error("GIANT_REST_INTERVAL_INVALID");
    // 先转无符号再加一，保留合法最大整数，不发生有符号溢出。
    return (dungeons_ - 1) % (static_cast<std::uint64_t>(interval) + 1) == 0;
}
bool WvdRunState::encounter_timed_out() const {
    const auto now = clock_->now();
    for (const auto &started : {combat_started_, chest_started_}) {
        if (started && now < *started)
            throw std::runtime_error("WVD_CLOCK_MOVED_BACKWARD");
        if (started && now - *started > std::chrono::seconds{400})
            return true;
    }
    return false;
}
void WvdRunState::on_segment(contracts::SegmentBoundary boundary, std::uint64_t generation,
                             std::size_t unit) {
    if (generation <= generation_)
        throw std::runtime_error("WVD_STATE_GENERATION_REUSED");
    if (boundary == contracts::SegmentBoundary::Continuation) {
        // 终点后unit_matches已经指向下一段；只核对上一段的真实业务回执。
        // 根检查点和真静止仍由同一个RunCoordinator证明，恢复不能冒充正常续段。
        if (fordraig_.sequence() && (unit != unit_index_ + 1 ||
            !fordraig_.continuation_ready(unit_index_)))
            throw std::runtime_error("FORDRAIG_CONTINUATION_NOT_CONFIRMED");
        if (cave_of_separation_.sequence() && (unit != unit_index_ + 1 ||
            !cave_of_separation_.segment_complete(unit_index_)))
            throw std::runtime_error("COS_CONTINUATION_NOT_CONFIRMED");
    }
    if (boundary == contracts::SegmentBoundary::Continuation && sleep_.completed() != 0) {
        if (unit != unit_index_ + 1 || !sleep_.batch_complete(unit_index_) || inn_payment_pending_)
            throw std::runtime_error("SLEEP_CONTINUATION_NOT_CONFIRMED");
        // 前段已真正静止，旧generation的回调不能进入当前状态。仅释放该住宿批次
        // 的幂等明细，累计次数、付款事实及其它业务回执保留；同批次恢复不清理。
        std::erase_if(confirmations_, [](const auto &entry) {
            const auto event = entry.second.value("event", "");
            return event == "sleep_visit_started" || event == "sleep_visit_completed" ||
                event == "inn_payment_prepared" || event == "inn_rest_completed";
        });
    }
    generation_ = generation;
    unit_index_ = unit;
    prepared_.reset();
    prepared_portrait_.clear();
    chest_selection_.clear_intent();
    healing_active_ = false;
    // 普通恢复不等于重启游戏。系统生命周期恢复按旧 restartGame 在首次请求时
    // 重置一次；同一请求的应用/重连/实例升级不重复增加崩溃计数或重装策略。
    if (boundary == contracts::SegmentBoundary::LifecycleRecovery && !lifecycle_recovery_active_) {
        lifecycle_recovery_active_ = true;
        ++lifecycle_recovery_sequence_;
        restart_game();
    }
}
void WvdRunState::enter_dungeon() {
    if (inn_payment_pending_)
        throw std::runtime_error("INN_PAYMENT_UNCONFIRMED");
    prepared_.reset();
    task_step_ = 0;
    need_initial_recover_ = true;
    healing_pending_ = false;
    healing_active_ = false;
    inn_rest_completed_ = false;
    ++supply_cycle_;
    if (setting_is("RELOAD_STRATEGY_WHEN", "每次副本开始", "Dungeon start"))
        strategy_.reload(task_step_);
}
void WvdRunState::target_point_completed() {
    prepared_.reset();
    ++task_step_;
    if (strategy_.uses_task_points())
        strategy_.reload(task_step_);
}
void WvdRunState::observe_combat(bool special) {
    if (!pending_combat_) {
        ++combat_sequence_;
        last_encounter_ = Encounter::Combat;
        healing_active_ = false;
        strategy_.begin_encounter(special);
    }
    if (!combat_started_)
        combat_started_ = clock_->now();
    pending_combat_ = true;
}
void WvdRunState::observe_chest() {
    if (!pending_chest_) {
        chest_selection_.reset();
        ++chest_sequence_;
        last_encounter_ = Encounter::Chest;
        healing_active_ = false;
    }
    if (!chest_started_)
        chest_started_ = clock_->now();
    pending_chest_ = true;
}
void WvdRunState::prepare_chest_character(const std::array<bool, 6> &fear, int preferred, std::uint32_t seed) {
    if (!pending_chest_)
        throw std::runtime_error("CHEST_NOT_OBSERVED");
    chest_selection_.prepare(fear, preferred, seed);
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
        healing_pending_ = healing_pending_ || !profile_.at("SKIP_COMBAT_RECOVER").get<bool>();
        ++combats_;
        met_encounter_ = true;
        pending_combat_ = false;
    }
    if (pending_chest_) {
        healing_pending_ = healing_pending_ || !profile_.at("SKIP_CHEST_RECOVER").get<bool>();
        ++chests_;
        met_encounter_ = true;
        pending_chest_ = false;
    }
}
bool WvdRunState::healing_required() const {
    return healing_pending_ || recover_after_rez_ ||
           (need_initial_recover_ && profile_.at("RECOVER_WHEN_BEGINNING").get<bool>());
}
void WvdRunState::resurrected() {
    prepared_.reset();
    // 旧版先计数再按死亡原因扣一；新版推迟到回到地下城才计数，因此撤销
    // 最后失败的待结算遭遇，不对 unsigned 成功次数做减法。已花费时间仍保留。
    if (last_encounter_ == Encounter::Combat)
        pending_combat_ = false;
    else if (last_encounter_ == Encounter::Chest)
        pending_chest_ = false;
    last_encounter_ = Encounter::None;
    revival_pending_ = false;
    suicide_requested_ = false;
    ++revivals_;
    recover_after_rez_ = true;
    healing_active_ = false;
    strategy_.reload(task_step_);
}
void WvdRunState::restart_game() {
    prepared_.reset();
    healing_active_ = false;
    combat_speed_ = false;
    zoom_world_map_ = false;
    wall_bypass_step_ = 0;
    ++wall_bypass_sequence_;
    combat_started_.reset();
    chest_started_.reset();
    ++crashes_;
    if (static_cast<std::int64_t>(crashes_) > profile_.at("MAX_CRASH_LIMIT").get<std::int64_t>())
        crashes_ = 0;
    strategy_.reload(task_step_);
}
void WvdRunState::dungeon_completed() {
    settle_legacy_lap(clock_->now());
    if (met_encounter_) {
        ++dungeons_;
        met_encounter_ = false;
    }
}
void WvdRunState::settle_legacy_lap(TimePoint now) {
    if (lap_started_) {
        const auto elapsed = std::chrono::duration<double>(now - *lap_started_).count();
        if (elapsed < 0)
            throw std::runtime_error("WVD_CLOCK_MOVED_BACKWARD");
        total_seconds_ += elapsed;
        last_lap_seconds_ = elapsed;
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
    // 真正生命周期恢复会重入旧 RestartableSequenceExecution 的 StateDungeon 调用。
    // 仅该局部路线开始新轮；住宿/善恶/专项开始回执仍保留，不能按 generation 全部重放。
    if (event == "dungeon_entered" || event == "target_completed")
        id += ":route:" + std::to_string(lifecycle_recovery_sequence_);
    else if (event == "combat_observed" || event == "combat_special_observed")
        id += ":combat:" + std::to_string(combat_sequence_ + (pending_combat_ ? 0 : 1));
    else if (event == "chest_observed")
        id += ":chest:" + std::to_string(chest_sequence_ + (pending_chest_ ? 0 : 1));
    else if (event == "dungeon_resumed")
        id += ":resume:" + std::to_string(combat_sequence_) + ":" + std::to_string(chest_sequence_);
    else if (event == "revival_observed")
        id += ":revival:" + std::to_string(revival_sequence_ + (revival_pending_ ? 0 : 1));
    else if (event == "resurrected")
        id += ":revival:" + std::to_string(revival_sequence_);
    else if (event == "party_death_observed")
        id += ":death_prompt:" + std::to_string(death_prompt_sequence_ + (death_prompt_pending_ ? 0 : 1));
    else if (event == "party_death_cleared")
        id += ":death_prompt:" + std::to_string(death_prompt_sequence_);
    else if (event == "party_defeat_observed")
        id += ":party_defeat:" + std::to_string(party_defeat_sequence_ + (suicide_requested_ ? 0 : 1));
    else if (event == "wall_turn_completed" || event == "wall_left_completed" || event == "wall_right_completed")
        id += ":wall:" + std::to_string(wall_bypass_sequence_);
    else if (event == "chest_character_attempted")
        id += ":chest:" + std::to_string(chest_sequence_) + ":selection:" +
              std::to_string(chest_selection_.attempts() + (chest_selection_.selected() ? 1 : 0));
    else if (event == "game_restarted")
        id += ":restart:" + std::to_string(lifecycle_recovery_sequence_);
    else if (event == "healing_requested")
        id += ":heal:" + std::to_string(healing_sequence_ + (healing_active_ ? 0 : 1));
    else if (event == "healing_completed")
        id += ":heal:" + std::to_string(healing_sequence_);
    else if (event == "inn_rest_completed" || event == "inn_payment_prepared")
        id += ":supply:" + std::to_string(supply_cycle_);
    else if (event == "party_reassembled")
        id += ":party:" + std::to_string(static_cast<std::size_t>(total_seconds_ / 21600));
    else if (event == "karma_observed")
        id += ":karma:" + std::to_string(karma_sequence_ + (karma_choice_ ? 0 : 1));
    else if (event == "karma_completed")
        id += ":karma:" + std::to_string(karma_sequence_);
    if (event == "mining_reward_observed" || event == "mining_reward_dismissed")
        id += ":mining:" + std::to_string(mining_.reward_sequence(event == "mining_reward_observed"));
    if (event == "special_dialogue_prepared" || event == "special_dialogue_completed" || event == "sandman_bondmate_completed")
        id += ":dialogue:" + std::to_string(special_dialogue_sequence_ + (event == "special_dialogue_prepared" && !special_dialogue_pending_ ? 1 : 0));
    if (event.starts_with("featured_"))
        id += ":featured:" + std::to_string(featured_visit_.sequence(event == "featured_visit_started"));
    if (event.starts_with("golden_"))
        id += ":golden:" + std::to_string(golden_chest_.sequence(event == "golden_started"));
    if (event.starts_with("sandman_") && event != "sandman_bondmate_completed")
        id += ":sandman:" + std::to_string(sandman_.sequence(event == "sandman_started"));
    if (event.starts_with("gold_income_"))
        id += ":gold-income:" + std::to_string(gold_income_.sequence(event == "gold_income_started"));
    if (event.starts_with("bull_cave_"))
        id += ":bull:" + std::to_string(bull_cave_.sequence(event == "bull_cave_started" || event == "bull_cave_started_rest"));
    if (event.starts_with("steel_trial_"))
        id += ":steel:" + std::to_string(steel_trial_.sequence(event == "steel_trial_started"));
    if (event.starts_with("fordraig_"))
        id += ":fordraig:" + std::to_string(fordraig_.sequence(event == "fordraig_started"));
    if (event.starts_with("cos_"))
        id += ":cos:" + std::to_string(cave_of_separation_.sequence(event == "cos_started"));
    if (event.starts_with("repel_")) {
        id += ":repel:" + std::to_string(repel_forces_.sequence(event == "repel_started"));
        if (event == "repel_battle_prepared" || event == "repel_battle_observed" || event == "repel_battle_completed")
            id += ":battle:" + std::to_string(repel_forces_.fight_sequence(event == "repel_battle_prepared"));
    }
    if (event == "fishing_reward_prepared" || event == "fishing_reward_completed")
        id += ":fishing:" + std::to_string(fishing_.sequence(event == "fishing_reward_prepared"));
    if (event == "fishing_wait_started" || event == "fishing_wait_failed")
        id += ":cast:" + std::to_string(fishing_.cast_sequence(event == "fishing_wait_started"));
    if (event == "fishing_cast_prepared" || event == "fishing_cast_completed")
        id += ":cast:" + std::to_string(fishing_.cast_intent_sequence(event == "fishing_cast_prepared"));
    if (event == "fishing_bait_requested" || event == "fishing_supplies_entered" || event == "fishing_transfer_prepared" ||
        event == "fishing_transferred" || event == "fishing_supplies_finished" || event == "fishing_supplies_returned" || event == "fishing_refilled") {
        id += ":refill:" + std::to_string(fishing_.refill_sequence(event == "fishing_bait_requested"));
        if (event == "fishing_transfer_prepared" || event == "fishing_transferred")
            id += ":transfer:" + std::to_string(fishing_.transfer_sequence(event == "fishing_transfer_prepared"));
    }
    if (event == "bounty_report_prepared" || event == "bounty_report_completed")
        id += ":report:" + std::to_string(bounty_reports_ + (event == "bounty_report_prepared" || bounty_report_pending_ ? 1 : 0));
    if (event == "sleep_visit_started" || event == "sleep_visit_completed")
        id += ":sleep:" + std::to_string(sleep_.completed() + (event == "sleep_visit_started" || sleep_.active() ? 1 : 0));
    if (event.starts_with("bounty_cycle_") || event.starts_with("bounty_leap_") || event.starts_with("bounty_travel_") ||
        event == "bounty_route_completed" || event == "bounty_return_completed" ||
        event == "scorpion_started" || event == "scorpion_hands_started" || event == "jier_started")
        id += ":bounty_cycle:" + std::to_string(bounty_cycle_.sequence() +
            ((event == "scorpion_started" || event == "scorpion_hands_started" || event == "jier_started") && !bounty_cycle_.active() ? 1 : 0));
    return id;
}
bool WvdRunState::confirm_event(const std::string &operation, const std::string &event,
                                 std::uint64_t generation, std::uint64_t frame_id,
                                 std::optional<std::size_t> expected_step, std::optional<std::size_t> reward_index) {
    if (operation.empty() || operation.size() > 256 || generation != generation_ || !frame_id)
        throw std::runtime_error("BUSINESS_CONFIRMATION_IDENTITY_INVALID");
    J effect{{"event", event}, {"expected_step", expected_step ? J(*expected_step) : J(nullptr)}};
    if (reward_index) {
        if (event != "mining_reward_observed" && event != "fishing_reward_prepared") throw std::runtime_error("MINING_REWARD_EVENT_INVALID");
        effect["reward_index"] = *reward_index;
    }
    if (karma_effect_.is_object() && karma_effect_.value("save_status", "") == "Failed")
        throw std::runtime_error("PROFILE_SAVE_FAILED");
    if (auto old = confirmations_.find(operation); old != confirmations_.end()) {
        if (old->second != effect)
            throw std::runtime_error("BUSINESS_OPERATION_CONFLICT");
        return false;
    }
    if (confirmations_.size() >= 4096)
        throw std::runtime_error("BUSINESS_CONFIRMATION_CAPACITY");
    if (expected_step && *expected_step != task_step_)
        throw std::runtime_error("BUSINESS_TASK_STEP_MISMATCH");
    if (event == "fordraig_started" || event == "cos_started") {
        const auto visit = featured_visit_.summary();
        if (pending_combat_ || pending_chest_ || visit.at("active").get<bool>() ||
            tasks::handoff_has_unconfirmed_effect(summarize()))
            throw std::runtime_error("EXTENSION_SIDE_EFFECT_PENDING");
        const auto now = clock_->now();
        if (lap_started_ && now < *lap_started_)
            throw std::runtime_error("WVD_CLOCK_MOVED_BACKWARD");
        if (event == "fordraig_started") {
            if (cave_of_separation_.summary(unit_index_).at("active").get<bool>())
                throw std::runtime_error("EXTENSION_CYCLE_CONFLICT");
            fordraig_.start(unit_index_, visit.at("visits_completed").get<std::size_t>());
        } else {
            if (fordraig_.summary(unit_index_).at("active").get<bool>())
                throw std::runtime_error("EXTENSION_CYCLE_CONFLICT");
            if (cave_of_separation_.sequence() && (!unit_index_ ||
                !cave_of_separation_.segment_complete(unit_index_ - 1)))
                throw std::runtime_error("COS_NEXT_CYCLE_UNIT_INVALID");
            cave_of_separation_.start(unit_index_);
        }
        if (event == "cos_started")
            settle_legacy_lap(now);
        else {
            if (lap_started_) total_seconds_ += std::chrono::duration<double>(now - *lap_started_).count();
            lap_started_ = now;
        }
        ++dungeons_;
        inn_rest_completed_ = false; ++supply_cycle_;
    } else if (event == "fordraig_leap_prepared" || event == "fordraig_trap1_prepared" ||
               event == "fordraig_trap2_prepared") {
        if (inn_payment_pending_ || special_dialogue_pending_ || pending_combat_ || pending_chest_)
            throw std::runtime_error("FORDRAIG_SIDE_EFFECT_PENDING");
        using Phase = quests::FordraigCycle::Phase;
        fordraig_.prepare(event == "fordraig_leap_prepared" ? Phase::Leap :
            event == "fordraig_trap1_prepared" ? Phase::Trap1Push : Phase::Trap2Push, unit_index_);
    } else if (event.starts_with("fordraig_")) {
        using Phase = quests::FordraigCycle::Phase;
        static const std::map<std::string, Phase> events{
            {"fordraig_leaped", Phase::Leap}, {"fordraig_requested", Phase::Request},
            {"fordraig_entered", Phase::Enter}, {"fordraig_trap1_routed", Phase::Trap1Route},
            {"fordraig_trap1_completed", Phase::Trap1Push}, {"fordraig_trap2_routed", Phase::Trap2Route},
            {"fordraig_trap2_completed", Phase::Trap2Push}, {"fordraig_trap3_completed", Phase::Trap3},
            {"fordraig_preboss_completed", Phase::PreBoss}, {"fordraig_boss_completed", Phase::Boss},
            {"fordraig_exited", Phase::Exit}, {"fordraig_completed", Phase::Return}};
        const auto phase = events.find(event);
        if (phase == events.end()) throw std::runtime_error("FORDRAIG_EVENT_INVALID");
        const auto visit = featured_visit_.summary();
        if (inn_payment_pending_ || special_dialogue_pending_ || pending_combat_ || pending_chest_ ||
            visit.at("active").get<bool>() || visit.at("pending").get<bool>())
            throw std::runtime_error("FORDRAIG_SIDE_EFFECT_PENDING");
        fordraig_.advance(phase->second, unit_index_, visit.at("visits_completed").get<std::size_t>(), task_step_);
    } else if (event == "cos_leap_prepared" || event == "cos_request_observed" ||
               event == "cos_request_prepared" || event == "cos_guild_prepared") {
        if (inn_payment_pending_ || special_dialogue_pending_ || pending_combat_ || pending_chest_)
            throw std::runtime_error("COS_SIDE_EFFECT_PENDING");
        if (event == "cos_leap_prepared") cave_of_separation_.prepare_leap(unit_index_);
        else if (event == "cos_request_observed") cave_of_separation_.request_observed(unit_index_);
        else if (event == "cos_request_prepared") cave_of_separation_.prepare_request(unit_index_);
        else cave_of_separation_.prepare_guild(unit_index_);
    } else if (event.starts_with("cos_")) {
        using Phase = quests::CaveOfSeparation::Phase;
        static const std::map<std::string, Phase> events{
            {"cos_leaped", Phase::Leap}, {"cos_fortress", Phase::Fortress}, {"cos_royal", Phase::RoyalCity},
            {"cos_requested", Phase::Request}, {"cos_rested", Phase::Rest}, {"cos_entered", Phase::Enter},
            {"cos_b1_completed", Phase::B1}, {"cos_ena_confirmed", Phase::B2},
            {"cos_request_confirmed", Phase::B3}, {"cos_back_completed", Phase::Back},
            {"cos_guild_entered", Phase::ReturnGuild}, {"cos_completed", Phase::ReturnInn}};
        const auto phase = events.find(event);
        if (phase == events.end()) throw std::runtime_error("COS_EVENT_INVALID");
        const auto visit = featured_visit_.summary();
        if (inn_payment_pending_ || special_dialogue_pending_ || pending_combat_ || pending_chest_ ||
            visit.at("active").get<bool>() || visit.at("pending").get<bool>())
            throw std::runtime_error("COS_SIDE_EFFECT_PENDING");
        // 停点图片只由这两个已注册的新帧事件确定，不接受可编辑参数伪造停点。
        const std::string_view stop = event == "cos_ena_confirmed" ? "COS/EnaTheAdventurer" :
            event == "cos_request_confirmed" ? "COS/requestwasfor" : "";
        cave_of_separation_.advance(phase->second, unit_index_, task_step_,
            inn_rest_completed_ && !inn_payment_pending_, stop);
    } else if (event == "repel_started") {
        if (inn_payment_pending_ || special_dialogue_pending_) throw std::runtime_error("REPEL_SIDE_EFFECT_PENDING");
        repel_forces_.start(unit_index_, quests::RepelForces::rounds(profile_));
        inn_rest_completed_ = false; ++supply_cycle_;
    } else if (event == "repel_rested") {
        repel_forces_.rested(unit_index_, inn_rest_completed_ && !inn_payment_pending_);
    } else if (event == "repel_arrived") {
        repel_forces_.arrived(unit_index_, task_step_);
    } else if (event == "repel_battle_prepared") {
        repel_forces_.prepare(unit_index_);
        // 原专项每场显式ReloadStrategy，不受通用“每副本”配置影响。
        prepared_.reset(); strategy_.reload(task_step_);
    } else if (event == "repel_battle_observed") {
        repel_forces_.observed(unit_index_); observe_combat();
    } else if (event == "repel_battle_completed") {
        if (!pending_combat_) throw std::runtime_error("REPEL_ENCOUNTER_NOT_OBSERVED");
        repel_forces_.fought(unit_index_);
        // 明确的战后对话也是这项任务的遭遇终点；复用计时结算，不伪造地下城识别。
        resume_dungeon();
    } else if (event == "repel_pair_completed") {
        repel_forces_.withdrawn(unit_index_);
    } else if (event == "repel_exited") {
        repel_forces_.exited(unit_index_, task_step_);
    } else if (event == "repel_completed") {
        if (inn_payment_pending_ || special_dialogue_pending_) throw std::runtime_error("REPEL_SIDE_EFFECT_PENDING");
        repel_forces_.complete(unit_index_);
    } else if (event == "steel_trial_started") {
        if (inn_payment_pending_ || special_dialogue_pending_) throw std::runtime_error("STEEL_TRIAL_SIDE_EFFECT_PENDING");
        const auto interval = profile_.at("REST_INTERVEL").get<std::int64_t>();
        if (interval < 0) throw std::runtime_error("STEEL_TRIAL_REST_INTERVAL_INVALID");
        const auto now = clock_->now();
        if (lap_started_ && now < *lap_started_) throw std::runtime_error("WVD_CLOCK_MOVED_BACKWARD");
        // 原钢试炼不读取ACTIVE_REST；首轮及每interval+1轮住宿。
        steel_trial_.start(unit_index_, dungeons_ % (static_cast<std::uint64_t>(interval) + 1) == 0);
        if (lap_started_) total_seconds_ += std::chrono::duration<double>(now - *lap_started_).count();
        lap_started_ = now; ++dungeons_;
        inn_rest_completed_ = false; ++supply_cycle_;
    } else if (event == "steel_trial_prepared") {
        steel_trial_.prepare(unit_index_);
    } else if (event == "steel_trial_entered") {
        steel_trial_.entered(unit_index_);
    } else if (event == "steel_trial_routed") {
        steel_trial_.routed(unit_index_, task_step_);
    } else if (event == "steel_trial_returned") {
        steel_trial_.returned(unit_index_);
    } else if (event == "steel_trial_completed") {
        if (inn_payment_pending_ || special_dialogue_pending_) throw std::runtime_error("STEEL_TRIAL_SIDE_EFFECT_PENDING");
        steel_trial_.complete(unit_index_, inn_rest_completed_);
    } else if (event == "bull_cave_started" || event == "bull_cave_started_rest") {
        if (inn_payment_pending_ || featured_visit_.summary().at("active").get<bool>()) throw std::runtime_error("BULL_CAVE_SIDE_EFFECT_PENDING");
        const auto now = clock_->now();
        if (lap_started_ && now < *lap_started_) throw std::runtime_error("WVD_CLOCK_MOVED_BACKWARD");
        bull_cave_.start(unit_index_, featured_visit_.summary().at("visits_completed").get<std::size_t>(), event == "bull_cave_started_rest");
        settle_legacy_lap(now);
        ++dungeons_;
    } else if (event == "bull_cave_leap_prepared") {
        bull_cave_.prepare_leap(unit_index_);
    } else if (event.starts_with("bull_cave_")) {
        using Phase = quests::BullCaveCycle::Phase;
        const std::map<std::string, Phase> events{{"bull_cave_leaped", Phase::Leap}, {"bull_cave_fortress", Phase::Fortress},
            {"bull_cave_royal", Phase::RoyalCity}, {"bull_cave_requested", Phase::Request}, {"bull_cave_first_entered", Phase::EnterFirst},
            {"bull_cave_first_routed", Phase::FirstRoute}, {"bull_cave_first_exited", Phase::FirstExit}, {"bull_cave_rested", Phase::Rest},
            {"bull_cave_second_entered", Phase::EnterSecond}, {"bull_cave_second_routed", Phase::SecondRoute}, {"bull_cave_completed", Phase::SecondExit}};
        const auto found = events.find(event);
        if (found == events.end()) throw std::runtime_error("BULL_CAVE_EVENT_INVALID");
        bull_cave_.advance(found->second, unit_index_, featured_visit_.summary().at("visits_completed").get<std::size_t>(), task_step_, inn_rest_completed_ && !inn_payment_pending_);
        if (event == "bull_cave_first_exited" && bull_cave_.summary(unit_index_).at("active").get<bool>()) {
            inn_rest_completed_ = false; ++supply_cycle_;
        }
    } else if (event == "gold_income_started") {
        if (inn_payment_pending_ || special_dialogue_pending_) throw std::runtime_error("GOLD_INCOME_SIDE_EFFECT_PENDING");
        const auto now = clock_->now();
        if (lap_started_ && now < *lap_started_) throw std::runtime_error("WVD_CLOCK_MOVED_BACKWARD");
        gold_income_.start(unit_index_);
        if (lap_started_) total_seconds_ += std::chrono::duration<double>(now - *lap_started_).count();
        lap_started_ = now;
        ++dungeons_;
    } else if (event == "gold_income_prepared") {
        gold_income_.prepare(unit_index_);
    } else if (event == "gold_income_advanced") {
        gold_income_.advance(unit_index_);
    } else if (event == "sandman_bondmate_completed") {
        if (!special_dialogue_pending_) throw std::runtime_error("SPECIAL_DIALOGUE_NOT_PREPARED");
        sandman_.bondmate();
        special_dialogue_pending_ = false;
        ++special_dialogues_completed_;
    } else if (event == "sandman_started") {
        if (inn_payment_pending_ || special_dialogue_pending_) throw std::runtime_error("SANDMAN_SIDE_EFFECT_PENDING");
        sandman_.start(unit_index_);
    } else if (event == "sandman_duke_prepared" || event == "sandman_triumph_prepared") {
        const auto expected = event == "sandman_duke_prepared" ? quests::SandmanCycle::Phase::LeapDuke : quests::SandmanCycle::Phase::LeapTriumph;
        if (sandman_.summary(unit_index_).at("phase") != static_cast<int>(expected)) throw std::runtime_error("SANDMAN_PHASE_INVALID");
        sandman_.prepare_leap(unit_index_);
    } else if (event.starts_with("sandman_")) {
        using Phase = quests::SandmanCycle::Phase;
        const std::map<std::string, Phase> events{{"sandman_entered", Phase::Enter}, {"sandman_routed", Phase::Route},
            {"sandman_exited", Phase::Exit}, {"sandman_decided", Phase::Decide}, {"sandman_duke_rested", Phase::RestDuke},
            {"sandman_duke_leaped", Phase::LeapDuke}, {"sandman_triumph_rested", Phase::RestTriumph}, {"sandman_completed", Phase::LeapTriumph}};
        const auto found = events.find(event);
        if (found == events.end()) throw std::runtime_error("SANDMAN_EVENT_INVALID");
        sandman_.advance(found->second, unit_index_, inn_rest_completed_ && !inn_payment_pending_, task_step_);
        // 每个真实住宿阶段有独立付款幂等域，不能让第一次回执跳过第二次住宿。
        if ((event == "sandman_decided" && sandman_.summary(unit_index_).at("active").get<bool>()) || event == "sandman_duke_leaped") {
            inn_rest_completed_ = false;
            ++supply_cycle_;
        }
    } else if (event == "golden_started") {
        if (inn_payment_pending_ || featured_visit_.summary().at("active").get<bool>())
            throw std::runtime_error("GOLDEN_SIDE_EFFECT_PENDING");
        const auto now = clock_->now();
        if (lap_started_ && now < *lap_started_) throw std::runtime_error("WVD_CLOCK_MOVED_BACKWARD");
        golden_chest_.start(unit_index_, featured_visit_.summary().at("visits_completed").get<std::size_t>());
        settle_legacy_lap(now);
        ++dungeons_;
    } else if (event == "golden_leap_prepared") {
        golden_chest_.prepare_leap(unit_index_);
    } else if (event.starts_with("golden_")) {
        using Phase = quests::GoldenChestCycle::Phase;
        const std::map<std::string, Phase> events{{"golden_leaped", Phase::Leap}, {"golden_travelled", Phase::Travel},
            {"golden_requested", Phase::Request}, {"golden_entered", Phase::Enter}, {"golden_trap_completed", Phase::Trap},
            {"golden_route_completed", Phase::Route}, {"golden_completed", Phase::Exit}};
        const auto found = events.find(event);
        if (found == events.end()) throw std::runtime_error("GOLDEN_EVENT_INVALID");
        golden_chest_.advance(found->second, unit_index_, featured_visit_.summary().at("visits_completed").get<std::size_t>(), task_step_);
    } else if (event == "featured_visit_started") {
        if (inn_payment_pending_) throw std::runtime_error("FEATURED_VISIT_PAYMENT_PENDING");
        featured_visit_.start();
        inn_rest_completed_ = false;
        ++supply_cycle_;
    } else if (event == "featured_visit_completed") {
        featured_visit_.finish(inn_rest_completed_ && !inn_payment_pending_);
    } else if (event == "featured_request_prepared") {
        featured_visit_.prepare();
    } else if (event == "featured_request_completed") {
        featured_visit_.selected();
    } else if (event == "special_dialogue_prepared") {
        if (special_dialogue_pending_) throw std::runtime_error("SPECIAL_DIALOGUE_ALREADY_PENDING");
        special_dialogue_pending_ = true;
        ++special_dialogue_sequence_;
    } else if (event == "special_dialogue_completed") {
        if (!special_dialogue_pending_) throw std::runtime_error("SPECIAL_DIALOGUE_NOT_PREPARED");
        special_dialogue_pending_ = false;
        ++special_dialogues_completed_;
    } else if (event == "fishing_bait_requested") {
        fishing_.request_bait();
    } else if (event == "fishing_supplies_entered") {
        fishing_.supplies_entered();
    } else if (event == "fishing_transfer_prepared") {
        fishing_.prepare_transfer();
    } else if (event == "fishing_transferred") {
        fishing_.transferred();
    } else if (event == "fishing_supplies_finished") {
        fishing_.supplies_finished();
    } else if (event == "fishing_supplies_returned") {
        fishing_.supplies_returned();
    } else if (event == "fishing_refilled") {
        fishing_.refilled();
    } else if (event == "fishing_cast_prepared") {
        fishing_.prepare_cast();
    } else if (event == "fishing_cast_completed") {
        fishing_.cast_completed(clock_->now());
    } else if (event == "fishing_wait_started") {
        fishing_.begin_wait(clock_->now());
    } else if (event == "fishing_wait_failed") {
        fishing_.failed(clock_->now());
    } else if (event == "fishing_reward_prepared") {
        if (!reward_index) throw std::runtime_error("FISHING_REWARD_INDEX_REQUIRED");
        fishing_.prepare(*reward_index);
    } else if (event == "fishing_reward_completed") {
        fishing_.complete();
    } else if (event == "scorpion_started" || event == "scorpion_hands_started" || event == "jier_started") {
        if (inn_payment_pending_ || bounty_report_pending_) throw std::runtime_error("BOUNTY_SIDE_EFFECT_PENDING");
        const auto interval = profile_.at("REST_INTERVEL").get<std::int64_t>();
        if (interval < 0) throw std::runtime_error("BOUNTY_REST_INTERVAL_INVALID");
        // 悬赏配置的 N 表示每出战 N 次休息；0 沿用每次休息的含义。
        const auto rest_every = std::max<std::uint64_t>(1, static_cast<std::uint64_t>(interval));
        const bool due = (dungeons_ + 1) % rest_every == 0;
        const auto now = clock_->now();
        if (lap_started_ && now < *lap_started_) throw std::runtime_error("WVD_CLOCK_MOVED_BACKWARD");
        bounty_cycle_.start(unit_index_, event == "scorpion_hands_started", due, bounty_reports_, bounty_reveals_);
        if (lap_started_) total_seconds_ += std::chrono::duration<double>(now - *lap_started_).count();
        lap_started_ = now;
        ++dungeons_;
        inn_rest_completed_ = false;
        ++supply_cycle_;
    } else if (event == "bounty_leap_prepared" || event == "bounty_travel_prepared") {
        bounty_cycle_.prepare_transfer(unit_index_, event == "bounty_leap_prepared" ? quests::BountyCycle::Phase::Leap : quests::BountyCycle::Phase::Travel);
    } else if (event == "bounty_leap_completed" || event == "bounty_travel_completed") {
        bounty_cycle_.transferred(unit_index_, event == "bounty_leap_completed" ? quests::BountyCycle::Phase::Leap : quests::BountyCycle::Phase::Travel);
    } else if (event == "bounty_cycle_revealed") {
        bounty_cycle_.revealed(unit_index_, bounty_reveals_);
    } else if (event == "bounty_travel_skipped") {
        if (!profile_.at("ACTIVE_BEAUTIFUL_ORE").get<bool>()) throw std::runtime_error("BOUNTY_TRAVEL_REQUIRED");
        bounty_cycle_.skip_travel(unit_index_);
    } else if (event == "bounty_route_completed") {
        bounty_cycle_.route_completed(unit_index_, task_step_);
    } else if (event == "bounty_return_completed") {
        bounty_cycle_.returned(unit_index_);
    } else if (event == "bounty_cycle_reported") {
        bounty_cycle_.reported(unit_index_, bounty_reports_);
    } else if (event == "bounty_cycle_completed") {
        if (inn_payment_pending_ || bounty_report_pending_) throw std::runtime_error("BOUNTY_SIDE_EFFECT_PENDING");
        bounty_cycle_.complete(unit_index_, inn_rest_completed_);
    } else if (event == "sleep_visit_started") {
        if (inn_payment_pending_) throw std::runtime_error("INN_PAYMENT_UNCONFIRMED");
        sleep_.start(unit_index_);
        inn_rest_completed_ = false;
        ++supply_cycle_;
    } else if (event == "sleep_visit_completed") {
        sleep_.finish(inn_rest_completed_ && !inn_payment_pending_);
    } else if (event == "bounty_revealed") {
        // 仅记录揭榜子流程到达城内的回执，不代表接取了某个指定悬赏或获得奖励。
        ++bounty_reveals_;
    } else if (event == "bounty_report_prepared") {
        if (bounty_report_pending_) throw std::runtime_error("BOUNTY_REPORT_ALREADY_PENDING");
        bounty_report_pending_ = true;
    } else if (event == "bounty_report_completed") {
        if (!bounty_report_pending_) throw std::runtime_error("BOUNTY_REPORT_NOT_PREPARED");
        ++bounty_reports_;
        bounty_report_pending_ = false;
    } else if (event == "manual_started_in_city") {
        manual_separation_.started_in_city(unit_index_);
    } else if (event == "manual_route_completed") {
        manual_separation_.route_completed(task_step_, unit_index_);
    } else if (event == "manual_rest_completed") {
        manual_separation_.rested(inn_rest_completed_ && !inn_payment_pending_);
    } else if (event == "manual_first_back_prepared") {
        manual_separation_.prepare(quests::ManualSeparation::Phase::FirstBack);
    } else if (event == "manual_first_back_completed") {
        manual_separation_.transferred(quests::ManualSeparation::Phase::FirstBack);
    } else if (event == "manual_second_back_prepared") {
        manual_separation_.prepare(quests::ManualSeparation::Phase::SecondBack);
    } else if (event == "manual_second_back_completed") {
        manual_separation_.transferred(quests::ManualSeparation::Phase::SecondBack);
    } else if (event == "manual_leap_prepared") {
        manual_separation_.prepare(quests::ManualSeparation::Phase::Leap);
    } else if (event == "manual_leap_completed") {
        manual_separation_.transferred(quests::ManualSeparation::Phase::Leap);
    } else if (event == "mining_reward_observed") {
        if (!reward_index) throw std::runtime_error("MINING_REWARD_REQUIRED");
        mining_.observe_reward(*reward_index);
    } else if (event == "mining_reward_dismissed") {
        mining_.reward_dismissed();
    } else if (event == "mining_refill_requested") {
        if (!mining_.refill_pending()) {
            if (inn_payment_pending_) throw std::runtime_error("INN_PAYMENT_UNCONFIRMED");
            ++supply_cycle_;
            inn_rest_completed_ = false;
        }
        mining_.require_pickaxes();
    } else if (event == "mining_party_assembled") {
        mining_.party_assembled();
    } else if (event == "mining_refill_completed") {
        if (!inn_rest_completed_ || inn_payment_pending_) throw std::runtime_error("MINING_REST_NOT_CONFIRMED");
        mining_.rest_completed();
    } else if (event == "mining_cycle_completed") {
        mining_.complete_cycle();
    } else if (event == "dark_light_entered") {
        // 暗灯case不是StateDungeon入本，不触发初始恢复、计次或重置路线。
        dark_light_active_ = true;
        need_initial_recover_ = false;
    } else if (event == "dark_light_completed") {
        dark_light_active_ = false;
    } else if (event == "giant_cycle_started") {
        if (giant_unit_ || inn_payment_pending_)
            throw std::runtime_error("GIANT_CYCLE_ALREADY_STARTED_OR_PAYMENT_PENDING");
        if (profile_.at("REST_INTERVEL").get<std::int64_t>() < 0)
            throw std::runtime_error("GIANT_REST_INTERVAL_INVALID");
        const auto now = clock_->now();
        settle_legacy_lap(now);
        ++dungeons_;
        giant_unit_ = unit_index_;
        giant_route_completed_ = false;
    } else if (event == "giant_route_completed") {
        if (!giant_unit_ || *giant_unit_ != unit_index_ || task_step_ != 2)
            throw std::runtime_error("GIANT_ROUTE_NOT_COMPLETED");
        giant_route_completed_ = true;
    } else if (event == "giant_cycle_completed") {
        if (!giant_unit_ || *giant_unit_ != unit_index_ || !giant_route_completed_ ||
            inn_payment_pending_ || giant_rest_due())
            throw std::runtime_error("GIANT_CYCLE_NOT_COMPLETED");
        ++giant_cycles_completed_;
        giant_unit_.reset();
    } else if (event == "trap_cycle_started") {
        if (trap_unit_)
            throw std::runtime_error("TRAP_CYCLE_ALREADY_STARTED");
        const auto now = clock_->now();
        settle_legacy_lap(now);
        // 旧陷阱任务在开始本轮时计数，不要求遭遇；另记完成数，不能把尝试当成功。
        ++dungeons_;
        trap_unit_ = unit_index_;
    } else if (event == "trap_cycle_completed") {
        if (!trap_unit_ || *trap_unit_ != unit_index_ || task_step_ != 7)
            throw std::runtime_error("TRAP_ROUTE_NOT_COMPLETED");
        ++trap_cycles_completed_;
        trap_unit_.reset();
    } else if (event == "karma_observed") {
        if (!karma_writer_)
            throw std::runtime_error("KARMA_PROFILE_NOT_BOUND");
        if (!karma_choice_) {
            karma_choice_ = choose_karma(karma_value_);
            ++karma_sequence_;
        }
    } else if (event == "karma_completed") {
        if (!karma_choice_)
            throw std::runtime_error("KARMA_NOT_OBSERVED");
        karma_effect_ = {{"operation_id", operation}, {"field", "KARMA_ADJUST"},
            {"before", karma_choice_->before}, {"after", karma_choice_->after},
            {"frame_id", frame_id}, {"generation", generation}, {"save_status", "Pending"}};
        karma_value_ = karma_choice_->after;
        karma_choice_.reset();
    } else if (event == "target_completed") {
        if (!expected_step)
            throw std::runtime_error("BUSINESS_TASK_STEP_REQUIRED");
        target_point_completed();
    } else if (event == "dungeon_entered")
        enter_dungeon();
    else if (event == "combat_observed")
        observe_combat();
    else if (event == "combat_special_observed")
        observe_combat(true);
    else if (event == "chest_observed")
        observe_chest();
    else if (event == "chest_character_attempted") {
        if (!pending_chest_)
            throw std::runtime_error("CHEST_NOT_OBSERVED");
        chest_selection_.attempted();
    }
    else if (event == "dungeon_resumed")
        resume_dungeon();
    else if (event == "dungeon_completed")
        dungeon_completed();
    else if (event == "revival_observed") {
        if (!revival_pending_)
            ++revival_sequence_;
        revival_pending_ = true;
    }
    else if (event == "resurrected") {
        if (!revival_pending_)
            throw std::runtime_error("REVIVAL_NOT_OBSERVED");
        resurrected();
    }
    else if (event == "party_reassembled")
        bag_clear_completed();
    else if (event == "party_death_observed") {
        if (!death_prompt_pending_) {
            ++death_prompt_sequence_;
            prepared_.reset();
            strategy_.reload(task_step_);
        }
        death_prompt_pending_ = true;
    }
    else if (event == "party_death_cleared") {
        if (!death_prompt_pending_)
            throw std::runtime_error("PARTY_DEATH_NOT_OBSERVED");
        death_prompt_pending_ = false;
    }
    else if (event == "party_defeat_observed") {
        if (!suicide_requested_)
            ++party_defeat_sequence_;
        suicide_requested_ = true;
    }
    else if (event == "wall_turn_completed" || event == "wall_left_completed" || event == "wall_right_completed") {
        const std::size_t expected = event == "wall_turn_completed" ? 0 : event == "wall_left_completed" ? 1 : 2;
        if (wall_bypass_step_ != expected)
            throw std::runtime_error("WALL_BYPASS_STEP_MISMATCH");
        ++wall_bypass_step_;
    }
    else if (event == "inn_payment_prepared") {
        if (inn_rest_completed_ || inn_payment_pending_)
            throw std::runtime_error("INN_PAYMENT_ALREADY_PREPARED_OR_COMPLETED");
        // 在可能消费的输入之前留下事实。后置观察失败不证明“没付款”，不能自动清掉。
        inn_payment_pending_ = true;
    }
    else if (event == "inn_rest_completed") {
        if (!inn_payment_pending_)
            throw std::runtime_error("INN_PAYMENT_NOT_PREPARED");
        // 换 generation 或换普通段均保留已住宿事实；真正再次入本才开始新补给周期。
        if (!inn_rest_completed_)
            ++inn_rests_;
        inn_rest_completed_ = true;
        inn_payment_pending_ = false;
    }
    else if (event == "healing_requested") {
        if (!healing_required())
            throw std::runtime_error("HEALING_NOT_REQUIRED");
        if (!healing_active_)
            ++healing_sequence_;
        healing_active_ = true;
        healing_pending_ = true;
        // 入本/复活请求在开始尝试时消费；未确认恢复结束时 pending 仍保留。
        need_initial_recover_ = false;
        recover_after_rez_ = false;
    } else if (event == "healing_completed") {
        if (!healing_active_)
            throw std::runtime_error("HEALING_NOT_STARTED");
        healing_active_ = false;
        healing_pending_ = false;
    }
    else if (event == "game_restarted") {
        if (!lifecycle_recovery_active_)
            throw std::runtime_error("GAME_RESTART_NOT_REQUESTED");
        const auto now = clock_->now();
        if (leap_wait_.summary(now).at("active").get<bool>()) {
            leap_wait_.restarted(now);
            handoff_intent_ = nullptr;
        }
        lifecycle_recovery_active_ = false;
    }
    else
        throw std::runtime_error("BUSINESS_EVENT_UNKNOWN");
    confirmations_.emplace(operation, effect);
    last_confirmation_ = {{"operation_id", operation}, {"event", event},
                           {"generation", generation}, {"frame_id", frame_id}};
    if (event == "karma_completed") {
        // 先记录确认事实，再持久化；保存失败不是游戏动作失败，不能再次点击。
        try {
            karma_effect_["profile_revision"] = karma_writer_->save(karma_effect_);
            karma_effect_["save_status"] = "Saved";
        } catch (const std::exception &error) {
            karma_effect_["save_status"] = "Failed";
            karma_effect_["error"] = error.what();
            throw std::runtime_error("PROFILE_SAVE_FAILED");
        }
    }
    return true;
}
J WvdRunState::summarize() const {
    const supply::SupplyFacts facts{dungeons_, met_encounter_, total_seconds_, last_bag_clear_};
    const bool ordinary = !inn_rest_completed_ && supply::ordinary_rest_due(profile_, facts);
    const bool party = supply::decide_rest(profile_, facts).reassemble;
    auto strategy = strategy_.summary();
    // Fordraig仅在非Boss阶段派生Auto；Boss保留原策略及已消耗次数，不写冻结profile。
    strategy["automatic"] = fordraig_.force_automatic() || strategy.at("automatic").get<bool>();
    return {{"kind", "wvd"},
            {"state_revision", "1"},
            {"farm_target_text", profile_.at("FARM_TARGET_TEXT")},
            {"last_lap_seconds", last_lap_seconds_ ? J(*last_lap_seconds_) : J(nullptr)},
            {"karma_value", karma_value_}, {"karma_pending", karma_choice_.has_value()},
            {"karma_ambush", karma_choice_ && karma_choice_->ambush},
            {"karma_sequence", karma_sequence_}, {"karma_effect", karma_effect_},
            {"run_identity", identity_},
            {"handoff_source", handoff_source_}, {"handoff_intent", handoff_intent_},
            {"leap_wait", leap_wait_.summary(clock_->now())},
            {"generation", generation_},
            {"unit_index", unit_index_},
            {"task_step", task_step_},
            {"strategy", std::move(strategy)},
            {"has_prepared_skill", prepared_.has_value()},
            {"prepared_skill_index", prepared_ ? J(prepared_index_) : J(nullptr)},
            {"prepared_portrait", prepared_ ? prepared_portrait_ : ""},
            {"dungeons", dungeons_},
            {"trap_cycles_completed", trap_cycles_completed_},
            {"giant_route_completed", giant_route_completed_},
            {"giant_cycle_active", giant_unit_.has_value()},
            {"giant_cycles_completed", giant_cycles_completed_},
            {"giant_rest_due", giant_rest_due()},
            {"dark_light_active", dark_light_active_},
            {"mining", mining_.summary()},
            {"manual_separation", manual_separation_.summary()},
            {"bounty_reports", bounty_reports_},
            {"fishing", fishing_.summary(clock_->now())},
            {"special_dialogue_pending", special_dialogue_pending_},
            {"special_dialogue_sequence", special_dialogue_sequence_},
            {"special_dialogues_completed", special_dialogues_completed_},
            {"featured_visit", featured_visit_.summary()},
            {"golden_chest", golden_chest_.summary(unit_index_)},
            {"sandman", sandman_.summary(unit_index_)},
            {"gold_income", gold_income_.summary(unit_index_)},
            {"bull_cave", bull_cave_.summary(unit_index_)},
            {"steel_trial", steel_trial_.summary(unit_index_)},
            {"repel_forces", repel_forces_.summary(unit_index_)},
            {"fordraig", fordraig_.summary(unit_index_)},
            {"cave_of_separation", cave_of_separation_.summary(unit_index_)},
            {"bounty_reveals", bounty_reveals_},
            {"sleep", sleep_.summary(unit_index_)},
            {"bounty_cycle", bounty_cycle_.summary(unit_index_, bounty_reports_)},
            {"bounty_report_pending", bounty_report_pending_},
            {"encounter_timed_out", encounter_timed_out()},
            {"combats", combats_},
            {"chests", chests_},
            {"crashes", crashes_},
            {"lifecycle_recovery_sequence", lifecycle_recovery_sequence_},
            {"lifecycle_recovery_active", lifecycle_recovery_active_},
            {"elapsed_seconds", std::chrono::duration<double>(clock_->now() - started_).count()},
            {"combat_seconds", combat_seconds_},
            {"total_seconds", total_seconds_},
            {"chest_seconds", chest_seconds_},
            {"last_bag_clear", last_bag_clear_},
            {"ordinary_rest_due", ordinary},
            {"party_refresh_due", party},
            {"city_supply_due", ordinary || party},
            {"inn_rest_completed", inn_rest_completed_},
            {"inn_payment_pending", inn_payment_pending_},
            {"inn_rests", inn_rests_},
            {"supply_cycle", supply_cycle_},
            {"confirmed_operations", confirmations_.size()},
            {"last_confirmation", last_confirmation_},
            {"combat_timer_active", combat_started_.has_value()},
            {"chest_timer_active", chest_started_.has_value()},
            {"pending_combat", pending_combat_},
            {"pending_chest", pending_chest_},
            {"chest_has_character", chest_selection_.selected().has_value()},
            {"chest_character", chest_selection_.selected() ? J(*chest_selection_.selected()) : J(nullptr)},
            {"chest_available_mask", chest_selection_.available_mask()},
            {"chest_character_attempts", chest_selection_.attempts()},
            {"combat_sequence", combat_sequence_},
            {"chest_sequence", chest_sequence_},
            {"revival_sequence", revival_sequence_},
            {"revival_pending", revival_pending_},
            {"revivals", revivals_},
            {"death_prompt_sequence", death_prompt_sequence_},
            {"death_prompt_pending", death_prompt_pending_},
            {"suicide_requested", suicide_requested_},
            {"party_defeat_sequence", party_defeat_sequence_},
            {"met_encounter", met_encounter_},
            {"need_initial_recover", need_initial_recover_},
            {"recover_after_rez", recover_after_rez_},
            {"healing_required", healing_required()},
            {"healing_active", healing_active_},
            {"healing_sequence", healing_sequence_},
            {"combat_speed", combat_speed_},
            {"zoom_world_map", zoom_world_map_},
            {"bypass_after_restart", wall_bypass_step_ == 3},
            {"wall_bypass_step", wall_bypass_step_}, {"wall_bypass_sequence", wall_bypass_sequence_}};
}
} // namespace wvd::games
