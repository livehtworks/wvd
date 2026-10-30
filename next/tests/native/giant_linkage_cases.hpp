#pragma once

namespace closure {
// Only semantic screens and device delivery are scripted. Factories, state,
// confirmations, strategy rows, public calls and FlowExecutor remain production code.
inline int giant_linkage(const char *profile_path, const char *saved_public_root = nullptr, bool recovery_audit = false) {
    const auto *output = std::getenv("WVD_CLOSURE_ROOT");
    check(output && *output, "WVD_CLOSURE_ROOT_REQUIRED");
    const auto stored = read(profile_path);
    auto profile = stored.at("values");
    profile["FARM_TARGET"] = "GiantBounty";
    games::WvdQuestCatalog quests(read("packs/wvd/parameters/legacy-quests.json"));
    const auto &task = quests.at("GiantBounty");
    const auto plan = games::tasks::giant_bounty_plan(task);
    J documents = J::object();
    J public_sources = J::object();
    for (const auto &source : read("resources/authoring/public-flows.json")) {
        const auto id = source.at("flow").at("id").get<std::string>();
        if (saved_public_root) {
            const auto path = std::filesystem::path(saved_public_root) / (id + ".json");
            auto saved = read(path.string().c_str());
            check(saved.at("flow").at("id") == id, "GIANT_SAVED_FLOW_IDENTITY_INVALID");
            public_sources[id] = {{"path", path.generic_string()}, {"sha256", platform::file_sha256(path)}};
            documents[id] = std::move(saved);
        } else documents[id] = source;
    }
    const auto catalogue = read("resources/authoring/semantic-assets.json");
    const games::tasks::PublicFlowLibrary library(documents, catalogue);
    const games::tasks::PublicStepScope public_steps([&](const auto &id, const auto &arguments) {
        return library.compile_step(id, arguments, "zh-Hant");
    });
    const auto graph = games::tasks::bounty_cycle(task, profile, {}, library,
        documents.at("guild-open-bounty-page"), "zh-Hant");
    if (recovery_audit) {
        for (const bool hands : {false, true}) {
            games::quests::BountyCycle cycle;
            using Phase = games::quests::BountyCycle::Phase;
            check(!cycle.segment_complete(0), "EMPTY_CYCLE_MUST_NOT_COMPLETE_SEGMENT");
            cycle.start(0, hands, false, 0, 0);
            cycle.prepare_transfer(0, Phase::Leap);
            cycle.transferred(0, Phase::Leap);
            cycle.skip_travel(0);
            check(!cycle.segment_complete(0), "UNREVEALED_SEGMENT_MUST_NOT_COMPLETE");
            cycle.revealed(0, 1);
            check(cycle.segment_complete(0) && !cycle.segment_complete(1), "REVEAL_RECEIPT_UNIT_MISMATCH");
            cycle.route_completed(1, 2);
            check(!cycle.segment_complete(1), "UNRETURNED_SEGMENT_MUST_NOT_COMPLETE");
            cycle.returned(1);
            check(cycle.segment_complete(1) && !cycle.segment_complete(2), "RETURN_RECEIPT_UNIT_MISMATCH");
            if (hands) {
                cycle.route_completed(2, 2);
                cycle.returned(2);
                check(cycle.segment_complete(2) && !cycle.segment_complete(3), "SECOND_RETURN_RECEIPT_UNIT_MISMATCH");
            }
            const auto final_unit = hands ? 3u : 2u;
            cycle.reported(final_unit, hands ? 2 : 1);
            check(!cycle.segment_complete(final_unit), "UNFINISHED_REST_MUST_NOT_COMPLETE");
            cycle.complete(final_unit, false);
            check(cycle.segment_complete(final_unit) && !cycle.segment_complete(final_unit + 1),
                "CYCLE_RECEIPT_MUST_NOT_COMPLETE_NEXT_UNIT");
        }
        Ports ordinary(profile);
        const auto old_id = ordinary.business.confirmation_id("enter", "dungeon_entered");
        ordinary.business.enter_segment(contracts::SegmentBoundary::LifecycleRecovery, 2, 0);
        check(old_id != ordinary.business.confirmation_id("enter", "dungeon_entered"),
            "ORDINARY_DUNGEON_RESTART_POLICY_CHANGED");
        Ports bounty(profile);
        auto &state = bounty.business;
        std::uint64_t frame{};
        for (const auto *event : {"giant_bounty_started", "bounty_leap_prepared", "bounty_leap_completed",
                 "bounty_travel_skipped", "bounty_revealed", "bounty_cycle_revealed"})
            check(state.confirm_event(event, event, 1, ++frame), "BOUNTY_RECOVERY_GUARD_SETUP");
        state.enter_segment(contracts::SegmentBoundary::Continuation, 2, 1);
        const auto enter_id = state.confirmation_id("enter", "dungeon_entered");
        state.confirm_event(enter_id, "dungeon_entered", 2, ++frame);
        const auto point_id = state.confirmation_id("point", "target_completed");
        state.confirm_event(point_id, "target_completed", 2, ++frame, 0);
        state.enter_segment(contracts::SegmentBoundary::LifecycleRecovery, 3, 1);
        check(enter_id == state.confirmation_id("enter", "dungeon_entered") &&
            point_id == state.confirmation_id("point", "target_completed") &&
            !state.confirm_event(enter_id, "dungeon_entered", 3, ++frame) &&
            !state.confirm_event(point_id, "target_completed", 3, ++frame, 0) &&
            state.summary().at("task_step") == 1, "BOUNTY_RECOVERY_REPLAYS_CONFIRMED_POINT");
        state.enter_segment(contracts::SegmentBoundary::Continuation, 4, 2);
        check(enter_id != state.confirmation_id("enter", "dungeon_entered"), "CROSS_UNIT_RECEIPT_REUSED");

        auto report = games::tasks::visit_bounty_board(games::tasks::BountyVisit::Report, library,
            documents.at("guild-open-bounty-page"), "zh-Hant");
        Driver uncertain(report, profile);
        uncertain.ports.business.confirm_event("prepared", "bounty_report_prepared", 1, 1);
        uncertain.ports.conditions[library.resource_condition("guild.bounties.page", "zh-Hant",
            authoring::ResourceUse::Observation).dump()] = true;
        uncertain.finish();
        check(uncertain.last.state == runtime::TickState::Failed &&
            uncertain.last.code == "quest.bounty_report_unconfirmed" && uncertain.ports.inputs.empty() &&
            uncertain.ports.business.summary().at("bounty_report_pending") == true &&
            uncertain.ports.business.summary().at("bounty_reports") == 0,
            "MISSING_REPORT_RECEIPT_MUST_NOT_COUNT_OR_CLICK");
        uncertain.evidence("recovery-report-no-receipt-protected");
        const auto policy = games::tasks::recovery_policy({"closure", "2", "game", "", false}, true);
        const auto ordinary_policy = games::tasks::recovery_policy({"closure", "2", "game", "", false});
        contracts::SessionResult result;
        result.end = contracts::SessionEnd::Failed;
        result.reason = "device.instance_exited";
        result.quiescent = true;
        check(policy(result, uncertain.ports.business, 1).has_value() &&
            !ordinary_policy(result, uncertain.ports.business, 1), "BOUNTY_OWNER_RECOVERY_NOT_SCOPED");
        result.unresolved_inputs.push_back({{"state", "Unknown"}, {"delivery_unknown", true}});
        check(!policy(result, uncertain.ports.business, 1), "UNKNOWN_REPORT_INPUT_MUST_STILL_STOP");
        result.unresolved_inputs.clear();
        result.reason = "navigation.input_no_progress";
        check(!policy(result, uncertain.ports.business, 1), "PENDING_REPORT_MUST_NOT_FORCE_GAME_RESTART");
        result.reason = "device.instance_exited";
        result.end = contracts::SessionEnd::UserStopped;
        check(!policy(result, uncertain.ports.business, 1), "MANUAL_STOP_MUST_NOT_RESUME");
    }
    const std::map<std::string, std::pair<std::string, std::string>> recovery_points{
        {"audit-inspect-guild", {"OpenInspectBoard", "guild"}},
        {"audit-inspect-list", {"CheckOldReports", "board"}},
        {"audit-inspect-harken", {"OpenInspectBoard", "harken"}},
        {"audit-old-report-prepared", {"OldBountyReport_Report", "board"}},
        {"audit-old-report-receipt", {"OldBountyReport_ReportConfirmed", "receipt"}},
        {"audit-old-rest-submitted", {"OldBountyRest_Paid", "stayed"}},
        {"audit-old-rest-paid", {"OldBountyRest_Leave", "stayed"}},
        {"audit-leap-prepared", {"Leap", "city"}},
        {"audit-leap-result", {"Leaped", "city"}},
        {"audit-reveal-menu", {"BountyBoard_Exit", "board"}},
        {"audit-reveal-result", {"Revealed", "city"}},
        {"audit-reveal-recorded", {"Checkpoint", "city"}},
        {"audit-entered", {"FirstTraverse", "dungeon"}},
        {"audit-battle", {"FirstDungeon_Battle_StartProgress", "battle"}},
        {"audit-revival", {"FirstDungeon_Battle_StartProgress", "revive"}},
        {"audit-chest", {"FirstDungeon_Battle_StartProgress", "chest"}},
        {"audit-reward", {"FirstDungeon_Battle_StartProgress", "reward"}},
        {"audit-target-done", {"FirstDungeon_CallRoute1", "dungeon"}},
        {"audit-heal", {"FirstDungeon_Heal_Recover", "healing"}},
        {"audit-harken", {"FirstPoints", "harken"}},
        {"audit-return", {"FirstReturn", "harken"}},
        {"audit-return-battle", {"FirstReturn", "battle"}},
        {"audit-return-chest", {"FirstReturn", "chest"}},
        {"audit-return-revival", {"FirstReturn", "revive"}},
        {"audit-return-recorded", {"Checkpoint", "city"}},
        {"audit-report-prepared", {"BountyReport_Report", "board"}},
        {"audit-report-receipt", {"BountyReport_ReportConfirmed", "receipt"}},
        {"audit-report-recorded", {"BountyReport_CloseReceipt", "receipt"}},
        {"audit-report-closed", {"BountyReport_CheckReports", "board"}},
        {"audit-rest-confirm", {"Inn_PreparePayment", "confirmation"}},
        {"audit-rest-submitted", {"Inn_Paid", "stayed"}},
        {"audit-rest-paid", {"Inn_Leave", "stayed"}},
        {"audit-rest-closed", {"Completed", "city"}},
        {"audit-cycle-recorded", {"Checkpoint", "city"}},
    };
    std::vector<std::string> scenarios{std::string("late-no-route"), std::string("black-destination"),
        std::string("restart-no-route"), std::string("harken-menu-dismissed"), std::string("harken-menu-combat"),
        std::string("normal"), std::string("revive-chest"), std::string("blocked"),
        std::string("direct-chest"), std::string("restart"), std::string("restart-battle"),
        std::string("road-combat"), std::string("interlude-combat"),
        std::string("reconnect-before-leap"), std::string("reconnect-after-leap")};
    if (recovery_audit) {
        scenarios.clear();
        for (const auto &[name, point] : recovery_points) scenarios.push_back(name);
    }
    std::vector<std::string> failures;
    for (const auto &scenario : scenarios) {
      try {
        const bool reconnect = scenario.starts_with("reconnect-");
        Driver driver(reconnect || recovery_audit ? games::recovery::with_boot_recovery(graph, true) : graph, profile);
        auto &ports = driver.ports;
        auto &state = ports.business;
        std::string screen = "city";
        bool report_available = scenario.starts_with("audit-old-");
        bool target_settled = false;
        bool restarted = false, road_completed = false, fault_pending = false, harken_dismissed = false;
        bool destination_pending{}, destination_handed_off{};
        unsigned destination_reads{}, destination_inputs{};
        unsigned reconnects{}, leap_submissions{}, return_battles{};
        unsigned target_battles{}, resumes{}, context_attempts{};
        bool audited_recovery{};
        J recovery_before, recovery_after;
        const auto recipe = [&](const char *id, bool value) {
            const auto condition = library.resource_condition(id,"zh-Hant",authoring::ResourceUse::Observation);
            ports.conditions[condition.dump()] = value;
            // Script real leaves, not only a compound key that disappears when
            // the production serializer flattens an equivalent all/any tree.
            const auto leaves = [&](const auto &self, const J &probe, bool hit) -> void {
                const auto mode = probe.value("mode", "");
                if (mode == "all" || mode == "any" || mode == "not") {
                    std::size_t index{};
                    for (const auto &child : probe.at("conditions")) {
                        const bool child_hit = mode == "not" ? !hit
                            : mode == "any" ? hit && index == 0 : hit;
                        self(self,child,child_hit); ++index;
                    }
                } else ports.conditions[probe.dump()] = hit;
            };
            if (value) leaves(leaves,condition,true);
        };
        const auto show = [&](const std::string &name) {
            screen = name;
            ports.scene(name);
            const bool city = name == "city", board = name == "board" || name == "receipt";
            ports.conditions[games::vision::fortress_city().dump()] = city;
            ports.conditions[games::vision::royal_city().dump()] = false;
            ports.conditions[games::vision::city_screen().dump()] = city;
            ports.conditions[games::vision::inn_button().dump()] = city;
            ports.conditions[games::vision::guild_button().dump()] = city;
            ports.conditions[games::vision::edge_of_town_button().dump()] = city;
            ports.conditions[games::vision::ruins_button().dump()] = city;
            ports.conditions[games::vision::harken_floor_menu().dump()] = name == "harken";
            if (recovery_audit) {
                ports.conditions[games::vision::character_page().dump()] = name == "healing";
                ports.conditions[games::vision::resource("inn.standard.gold.confirmation", "zh-Hant").dump()] = name == "confirmation";
            }
            recipe("guild.menu", name == "guild");
            recipe("guild.commissions.page", name == "commissions");
            recipe("guild.commissions.entry", name == "guild");
            recipe("guild.bounties.entry", name == "commissions");
            recipe("guild.bounties.page", board);
            recipe("guild.list.back", name == "board");
            recipe("guild.leave", name == "guild");
            recipe("guild.report.action", name == "board" && report_available);
            recipe("guild.report.receipt", name == "receipt");
            recipe("guild.report.receipt.close", name == "receipt");
            if (city) ports.images = {"Inn", "guild", "EdgeOfTown", "ruins"};
            if (name == "wheel") ports.images = {"cursedWheel_zh_hant"};
            if (name == "leap") ports.images = {"cursedWheelTitle_zh_hant", "cursedwheel_impregnableFortress_zh_hant", "Triumph_zh_hant", "leap_zh_hant"};
            if (name == "outskirts") ports.images = {"impregnableFortress"};
            if (name == "zone") ports.images = {"fortressb10f"};
            if (name == "dungeon") ports.images = {"dungFlag", "mark_auto", "resume"};
            if (name == "battle") { ports.images.clear(); ports.combat = ports.ready = true; }
            if (name == "revive") ports.images = {"RiseAgain"};
            if (name == "chest") {
                ports.images = {"chestFlag"};
                recipe("chest.open.option", true);
            }
            if (name == "reward") {
                ports.images = {"chest_reward_advance"};
                ports.conditions[games::vision::resource("chest.reward.page").dump()] = true;
            }
            if (name == "healing") ports.images = {"trait", "recover"};
            if (name == "inn") ports.images = {"Stay"};
            if (name == "room") ports.images = {"Economy"};
            if (name == "confirmation") ports.images = {"inn_confirm_zh_hant"};
            if (name == "stayed") { ports.images = {"Stay"}; ports.conditions[games::vision::inn_leave_zh().dump()] = true; }
        };
        ports.semantic_leaf = [&](const J &condition) -> std::optional<bool> {
            const auto mode = condition.value("mode", "");
            if (recovery_audit && (mode == "boot_ready" || mode == "boot_post")) {
                for (const auto &probe : games::vision::boot_probes(mode == "boot_post", "zh-Hant", false))
                    if (ports.evaluate(probe)) return true;
                return false;
            }
            if (mode == "auto_route_moving") return screen == "dungeon";
            if (mode == "auto_route_post") return screen == "battle" || screen == "dungeon" || screen == "harken";
            if (mode == "region_changed" || mode == "combat_progress") return true;
            if (mode == "navigation_resume_unavailable" || mode == "movement_stopped")
                return scenario == "black-destination" && destination_pending && destination_reads >= 2;
            return std::nullopt;
        };
        ports.after_input = [&](const std::string &path) {
            if (screen == "city") {
                if (path.find("Guild") != std::string::npos || path.find("city-enter-guild") != std::string::npos ||
                    path.find("InspectBountyBoard") != std::string::npos || path.find("Public") != std::string::npos) show("guild");
                else if (path.find("Ruins") != std::string::npos) {
                    show("wheel");
                    if (scenario == "reconnect-before-leap" && !reconnects) fault_pending = true;
                }
                else if (path.find("Rest") != std::string::npos || path.find("inn") != std::string::npos ||
                    path.find("Inn") != std::string::npos) show("inn");
                else show("outskirts");
            } else if (screen == "guild") {
                if (path.find("Leave") != std::string::npos) show("city"); else show("commissions");
            } else if (screen == "commissions") show("board");
            else if (screen == "board") {
                if (path.find("ListBack") != std::string::npos) show("guild");
                else if (path.find("Report") != std::string::npos) show("receipt");
            } else if (screen == "receipt") { report_available = false; show("board"); }
            else if (screen == "wheel") show("leap");
            else if (screen == "leap") {
                if (path.find("QuickLeap") != std::string::npos || path.find("SubmitLeap") != std::string::npos) {
                    ++leap_submissions;
                    show("city");
                    if (scenario == "reconnect-after-leap" && !reconnects) fault_pending = true;
                }
            } else if (screen == "outskirts") show("zone");
            else if (screen == "zone") show(scenario == "road-combat" && !road_completed ? "battle" : "dungeon");
            else if (screen == "dungeon") {
                if (path.find("Route1") != std::string::npos) { report_available = true; show("harken"); }
                else if (path.find("ReturnHarken") != std::string::npos) show("harken");
                else if (path.find("Heal") != std::string::npos) show("healing");
                else if ((scenario == "late-no-route" || scenario == "black-destination" ||
                    (scenario == "restart-no-route" && restarted)) && path.find("Route0_Choose") != std::string::npos) {
                    check(++destination_inputs == 1, "LATE_NO_ROUTE_REPLAYED_NAVIGATION");
                    destination_pending = true;
                }
                else show("battle");
            } else if (screen == "healing") {
                if (path.find("Back") != std::string::npos || path.find("CloseCharacter") != std::string::npos) show("dungeon");
            } else if (screen == "revive") { ++resumes; show("battle"); }
            else if (screen == "chest") show("reward");
            else if (screen == "reward") show(scenario == "interlude-combat" ? "battle" : "dungeon");
            else if (screen == "harken") show("city");
            else if (screen == "inn") show("room");
            else if (screen == "room") show("confirmation");
            else if (screen == "confirmation") show("stayed");
            else if (screen == "stayed") show("city");
        };
        ports.before_capture = [&] {
            if (destination_pending && ++destination_reads >= 2 && scenario != "black-destination")
                ports.images.insert("theRouteToTheDestinationCannotBeFound");
            if (!fault_pending) return;
            fault_pending = false;
            throw contracts::ObservationUnavailable({contracts::ReadFaultKind::TransportUnavailable,
                contracts::ReadFaultStage::Capture, "ADB_OFFLINE", "test.bounty.capture", {}, {}});
        };
        ports.observation_recovery = [&](bool) {
            check(++reconnects == 1, "GIANT_TRANSFER_RECONNECT_LOOP");
            show("city");
            return contracts::ObservationRecovery{{}, true, true};
        };
        show("city");
        std::unique_ptr<runtime::FlowExecutor> executor = std::make_unique<runtime::FlowExecutor>(driver.program, ports, 35s);
        const auto deadline = std::chrono::steady_clock::now() + 35s;
        std::size_t unit{};
        bool blocked_once{};
        bool reacquisition_observed{};
        std::string previous_step;
        while (std::chrono::steady_clock::now() < deadline) {
            const auto step = executor->current_step_id();
            if (recovery_audit && !audited_recovery && step == "Task_" + recovery_points.at(scenario).first &&
                (scenario != "audit-report-closed" || state.summary().at("bounty_reports") == 1) &&
                (scenario != "audit-reveal-recorded" || unit == 0) &&
                (scenario != "audit-return-recorded" || unit == 1) &&
                (scenario != "audit-cycle-recorded" || unit == 2)) {
                check(!executor->has_unresolved_input(), "AUDIT_MUST_NOT_DISCARD_PENDING_INPUT");
                audited_recovery = true;
                recovery_before = state.summary();
                contracts::SessionResult failure;
                failure.end = contracts::SessionEnd::Failed;
                failure.reason = "device.instance_exited";
                failure.quiescent = true;
                const auto policy = games::tasks::recovery_policy({"closure", "2", "game", "", false}, true);
                check(policy(failure, state, 1).has_value(), "AUDIT_ORIGINAL_RECOVERY_POLICY_REJECTED");
                state.enter_segment(contracts::SegmentBoundary::LifecycleRecovery, ++ports.generation, unit);
                recovery_after = state.summary();
                show(recovery_points.at(scenario).second);
                executor = std::make_unique<runtime::FlowExecutor>(driver.program, ports, 35s);
                driver.trace.push_back("AUDIT_LIFECYCLE_RECOVERY:" + step);
                continue;
            }
            if (destination_pending && step.find("Route0_DestinationFinished") != std::string::npos) {
                destination_pending = false;
                destination_handed_off = true;
                show("dungeon"); // The toast has disappeared before the parent resumes.
            }
            // The child has confirmed both points. Healing can close the Harken
            // menu before the parent consumes the completed route receipt.
            if (scenario.starts_with("harken-menu-") && step == "FirstPoints" && !harken_dismissed) {
                check(state.summary().at("task_step") == 2, "HARKEN_DISMISSAL_BEFORE_POINT_CONFIRMATION");
                harken_dismissed = true;
                show(scenario == "harken-menu-combat" ? "battle" : "dungeon");
            }
            if (state.summary().at("target_encounter").at("completion_reason") == "unattributed_combat_reacquire") {
                check(state.summary().at("task_step") == 0 &&
                    !state.summary().at("target_encounter").at("resume_authorized").get<bool>(),
                    "REACQUIRE_MUST_NOT_COMPLETE_TARGET_POINT");
                reacquisition_observed = true;
            }
            if (state.summary().at("target_encounter").at("phase") == 4) target_settled = true;
            if (step != previous_step) { driver.trace.push_back(step); previous_step = step; }
            if (screen == "battle" && step.find("Battle_StartProgress") != std::string::npos) {
                if (((scenario == "harken-menu-combat" && harken_dismissed) || scenario.starts_with("audit-return-")) && state.summary().at("task_step") == 2 &&
                    !state.summary().at("target_encounter").at("active").get<bool>()) {
                    ++return_battles;
                    show("dungeon");
                } else if (scenario == "road-combat" && !state.summary().at("target_encounter").at("active").get<bool>()) {
                    check(state.summary().at("task_step") == 0, "ROAD_COMBAT_MUST_NOT_CONFIRM_TARGET");
                    road_completed = true;
                    show("dungeon");
                } else {
                ++target_battles;
                check(state.summary().at("target_encounter").at("active") == true &&
                    state.summary().at("task_step") == 0, "TARGET_IDENTITY_MUST_EXIST_BEFORE_END");
                context_attempts = state.summary().at("target_encounter").at("attempt").get<unsigned>();
                if ((scenario == "restart" || scenario == "restart-battle" || scenario == "restart-no-route") && !restarted) {
                    restarted = true;
                    state.enter_segment(contracts::SegmentBoundary::LifecycleRecovery, ++ports.generation, unit);
                    show(scenario == "restart-battle" ? "battle" : "dungeon");
                    executor = std::make_unique<runtime::FlowExecutor>(driver.program, ports, 35s);
                    check(state.summary().at("target_encounter").at("active") == true &&
                        state.summary().at("target_encounter").at("attempt") == context_attempts &&
                        state.summary().at("target_encounter").at("resume_authorized") == false,
                        "RESTART_MUST_PRESERVE_ATTEMPT_AND_REQUIRE_REACQUISITION");
                    continue;
                }
                if (scenario == "revive-chest" && target_battles == 1) show("revive");
                else if ((scenario == "revive-chest" && target_battles == 2) || scenario == "direct-chest" ||
                    (scenario == "interlude-combat" && target_battles == 1)) show("chest");
                else show("dungeon");
                }
            }
            if (scenario == "blocked" && !blocked_once && step.find("FightTarget0") != std::string::npos) {
                ports.blocker = true; blocked_once = true;
            }
            if (ports.blocker && step.find("TargetDispatch0") != std::string::npos) ports.blocker = false;
            driver.last = executor->tick();
            if (driver.last.state == runtime::TickState::Waiting)
                std::this_thread::sleep_until(std::min(driver.last.wake_at, std::chrono::steady_clock::now() + 30ms));
            if (driver.last.state == runtime::TickState::Completed) {
                if (unit == 2) break;
                state.enter_segment(contracts::SegmentBoundary::Continuation, ++ports.generation, ++unit);
                executor = std::make_unique<runtime::FlowExecutor>(driver.program, ports, 35s);
            } else if (driver.last.state != runtime::TickState::Progress && driver.last.state != runtime::TickState::Waiting) break;
        }
        const auto summary = state.summary();
        std::ofstream(std::filesystem::path(output) / ("giant-linkage-" + scenario + ".json")) << J{
            {"scope", "complete three-unit bounty factory; device and semantic leaves isolated"},
            {"profile_sha256", platform::file_sha256(profile_path)}, {"strategy_rows_preserved", profile.at("STRATEGY")},
            {"public_library", saved_public_root ? "recorded_saved_definitions" : "author_source"},
            {"public_sources", public_sources},
            {"trace", driver.trace}, {"inputs", ports.inputs}, {"business", summary},
            {"progress", executor->progress_snapshot()}, {"terminal_state", int(driver.last.state)},
            {"terminal_code", driver.last.code}, {"target_battle_entries", target_battles},
            {"return_battle_entries", return_battles}, {"harken_menu_dismissed", harken_dismissed},
            {"revival_inputs", resumes}, {"target_attempt", context_attempts}}.dump(2);
        if (recovery_audit) {
            std::ofstream(std::filesystem::path(output) / (scenario + "-recovery.json")) << J{
                {"checkpoint", recovery_points.at(scenario).first}, {"screen", recovery_points.at(scenario).second},
                {"triggered", audited_recovery}, {"before", recovery_before}, {"after", recovery_after},
                {"final", summary}, {"leap_submissions", leap_submissions}}.dump(2);
            check(audited_recovery, "AUDIT_CHECKPOINT_NOT_REACHED:" + scenario);
            check(summary.at("bounty_reports") == (scenario.starts_with("audit-old-") ? 2 : 1),
                "AUDIT_REPORT_COUNT_MUST_MATCH_REAL_RECEIPTS");
            if (scenario == "audit-target-done" || scenario == "audit-heal" || scenario == "audit-harken")
                check(target_battles == 1, "AUDIT_COMPLETED_TARGET_MUST_NOT_BE_REPLAYED");
            if (scenario == "audit-rest-submitted" || scenario == "audit-rest-paid")
                check(summary.at("inn_payment").at("submissions") ==
                    recovery_before.at("inn_payment").at("submissions"), "AUDIT_PAID_INN_MUST_NOT_PAY_AGAIN");
        }
        if (scenario == "interlude-combat" || scenario == "restart-battle")
            check(reacquisition_observed && target_battles >= 3 && context_attempts >= 2 &&
                std::any_of(driver.trace.begin(), driver.trace.end(), [](const auto &id) {
                    return id.find("TargetReacquirePrepared") != std::string::npos;
                }), "UNATTRIBUTED_COMBAT_MUST_REACQUIRE_WITH_NEW_ATTEMPT");
        check(driver.last.state == runtime::TickState::Completed && unit == 2 &&
            summary.at("bounty_cycle").at("completed_cycles") == 1 && summary.at("task_step") == 2 &&
            target_settled && !summary.at("target_encounter").at("active").get<bool>() && !executor->has_unresolved_input(),
            "GIANT_LINKAGE:" + scenario + ":" + driver.last.code + ":" + executor->current_step_id());
        check(scenario != "revive-chest" || (target_battles == 2 && resumes == 1 && context_attempts == 1),
            "REVIVAL_MUST_CONTINUE_SAME_TARGET_ATTEMPT");
        if (scenario.starts_with("harken-menu-"))
            check(harken_dismissed && target_battles == 1 &&
                std::count(driver.trace.begin(), driver.trace.end(), "FirstRouteDone") == 1 &&
                std::count(driver.trace.begin(), driver.trace.end(), "FirstReturned") == 1 &&
                return_battles == (scenario == "harken-menu-combat" ? 1U : 0U),
                "CONFIRMED_ROUTE_MUST_RETURN_WITHOUT_REDOING_TARGET_OR_REQUIRING_OLD_MENU");
        if (reconnect) {
            check(reconnects == 1 && leap_submissions == (scenario == "reconnect-before-leap" ? 1U : 2U) &&
                std::count(driver.trace.begin(), driver.trace.end(), "Task_PrepareLeap") == 1 &&
                std::find(driver.trace.begin(), driver.trace.end(), "Task_ResumeLeapTransfer") != driver.trace.end() &&
                std::find(driver.trace.begin(), driver.trace.end(), "Task_TransferUncertain") == driver.trace.end(),
                "GIANT_RECONNECT_MUST_RESUME_PREPARED_LEAP_WITHOUT_REPREPARING");
        }
        if (scenario == "late-no-route" || scenario == "black-destination" || scenario == "restart-no-route")
            check(destination_handed_off && destination_reads >= 2 && destination_inputs == 1 &&
                target_battles == (scenario == "restart-no-route" ? 1U : 0U) &&
                (scenario != "restart-no-route" || (reacquisition_observed &&
                    std::count(driver.trace.begin(), driver.trace.end(), "FirstDungeon_TargetReacquirePrepared0") == 1)) &&
                std::count(driver.trace.begin(), driver.trace.end(), "FirstDungeon_NavigationTerminated0") == 1 &&
                std::count(driver.trace.begin(), driver.trace.end(), "FirstRouteDone") == 1,
                "LATE_DESTINATION_RESULT_MUST_SURVIVE_TOAST_DISAPPEARANCE");
        std::cout << "giant linkage " << scenario << " PASS; task_step=2; bounty_cycle=1; pending=0\n";
      } catch (const std::exception &error) {
        if (!recovery_audit) throw;
        failures.push_back(scenario + ":" + error.what());
        std::cerr << "giant linkage " << failures.back() << " FAIL\n";
      }
    }
    if (recovery_audit) {
        std::ofstream(std::filesystem::path(output) / "recovery-audit.json") << J{
            {"cases", scenarios}, {"failures", failures}, {"passed", failures.empty()}}.dump(2);
        check(failures.empty(), "GIANT_RECOVERY_AUDIT_FAILED:" + std::to_string(failures.size()));
        return 0;
    }
    for (const bool travel : {false, true}) {
        auto values = profile;
        if (travel) values["FARM_TARGET"] = "Scorpionesses";
        const auto guarded = travel ? games::tasks::bounty_cycle(quests.at("Scorpionesses"), values, {}, library,
            documents.at("guild-open-bounty-page"), "zh-Hant") : graph;
        Driver driver(guarded, values);
        auto &state = driver.ports.business;
        std::uint64_t receipt{};
        for (const auto *event : {travel ? "scorpion_started" : "giant_bounty_started", "bounty_leap_prepared"})
            check(state.confirm_event(event, event, 1, ++receipt), "TRANSFER_GUARD_SEED_REJECTED");
        if (travel) {
            for (const auto *event : {"bounty_leap_completed", "bounty_travel_prepared"})
                check(state.confirm_event(event, event, 1, ++receipt), "TRAVEL_GUARD_SEED_REJECTED");
        } else state.enter_segment(contracts::SegmentBoundary::Continuation, ++driver.ports.generation, 1);
        driver.ports.scene("city");
        driver.finish();
        check(driver.last.state == runtime::TickState::Failed &&
            driver.last.code == "quest.bounty_transfer_unconfirmed" && driver.ports.inputs.empty() &&
            state.summary().at("bounty_cycle").at("transfer_pending") == true,
            "UNRELATED_TRANSFER_OR_UNIT_MISMATCH_MUST_NOT_RETRY_LEAP");
        driver.evidence(travel ? "TRANSFER-cross-city-protected" : "TRANSFER-unit-mismatch-protected");
    }
    return 0;
}
}
