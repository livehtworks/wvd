#pragma once

namespace closure {
// Only semantic screens and device delivery are scripted. Factories, state,
// confirmations, strategy rows, public calls and FlowExecutor remain production code.
inline int giant_linkage(const char *profile_path, const char *saved_public_root = nullptr) {
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
    for (const auto &scenario : {std::string("normal"), std::string("revive-chest"), std::string("blocked"),
        std::string("direct-chest"), std::string("restart"), std::string("road-combat"), std::string("interlude-combat")}) {
        Driver driver(graph, profile);
        auto &ports = driver.ports;
        auto &state = ports.business;
        std::string screen = "city";
        bool report_available = false;
        bool target_settled = false;
        bool restarted = false, road_completed = false;
        unsigned target_battles{}, resumes{}, context_attempts{};
        const auto recipe = [&](const char *id, bool value) {
            ports.conditions[library.resource_condition(id, "zh-Hant", authoring::ResourceUse::Observation).dump()] = value;
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
            if (name == "chest") ports.images = {"chestFlag"};
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
            if (mode == "auto_route_moving") return screen == "dungeon";
            if (mode == "auto_route_post") return screen == "battle" || screen == "dungeon" || screen == "harken";
            if (mode == "region_changed") return true;
            if (mode == "navigation_resume_unavailable") return false;
            if (mode == "movement_stopped") return false;
            return std::nullopt;
        };
        ports.after_input = [&](const std::string &path) {
            if (screen == "city") {
                if (path.find("Guild") != std::string::npos || path.find("city-enter-guild") != std::string::npos ||
                    path.find("InspectBountyBoard") != std::string::npos || path.find("Public") != std::string::npos) show("guild");
                else if (path.find("Ruins") != std::string::npos) show("wheel");
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
                if (path.find("QuickLeap") != std::string::npos || path.find("SubmitLeap") != std::string::npos) show("city");
            } else if (screen == "outskirts") show("zone");
            else if (screen == "zone") show(scenario == "road-combat" && !road_completed ? "battle" : "dungeon");
            else if (screen == "dungeon") {
                if (path.find("Route1") != std::string::npos) { report_available = true; show("harken"); }
                else if (path.find("Heal") != std::string::npos) show("healing");
                else show("battle");
            } else if (screen == "healing") {
                if (path.find("Back") != std::string::npos) show("dungeon");
            } else if (screen == "revive") { ++resumes; show("battle"); }
            else if (screen == "chest") show("reward");
            else if (screen == "reward") show(scenario == "interlude-combat" ? "battle" : "dungeon");
            else if (screen == "harken") show("city");
            else if (screen == "inn") show("room");
            else if (screen == "room") show("confirmation");
            else if (screen == "confirmation") show("stayed");
            else if (screen == "stayed") show("city");
        };
        show("city");
        std::unique_ptr<runtime::FlowExecutor> executor = std::make_unique<runtime::FlowExecutor>(driver.program, ports, 35s);
        const auto deadline = std::chrono::steady_clock::now() + 35s;
        std::size_t unit{};
        bool blocked_once{};
        std::string previous_step;
        while (std::chrono::steady_clock::now() < deadline) {
            const auto step = executor->current_step_id();
            if (state.summary().at("target_encounter").at("phase") == 4) target_settled = true;
            if (step != previous_step) { driver.trace.push_back(step); previous_step = step; }
            if (screen == "battle" && step.find("Battle_StartProgress") != std::string::npos) {
                if (scenario == "road-combat" && !state.summary().at("target_encounter").at("active").get<bool>()) {
                    check(state.summary().at("task_step") == 0, "ROAD_COMBAT_MUST_NOT_CONFIRM_TARGET");
                    road_completed = true;
                    show("dungeon");
                } else {
                ++target_battles;
                check(state.summary().at("target_encounter").at("active") == true &&
                    state.summary().at("task_step") == 0, "TARGET_IDENTITY_MUST_EXIST_BEFORE_END");
                context_attempts = state.summary().at("target_encounter").at("attempt").get<unsigned>();
                if (scenario == "restart" && !restarted) {
                    restarted = true;
                    state.enter_segment(contracts::SegmentBoundary::LifecycleRecovery, ++ports.generation, unit);
                    show("dungeon");
                    executor = std::make_unique<runtime::FlowExecutor>(driver.program, ports, 35s);
                    check(state.summary().at("target_encounter").at("active") == true &&
                        state.summary().at("target_encounter").at("attempt") == context_attempts &&
                        state.summary().at("target_encounter").at("resume_authorized") == false,
                        "RESTART_MUST_PRESERVE_ATTEMPT_AND_REQUIRE_REACQUISITION");
                    continue;
                }
                if (scenario == "revive-chest" && target_battles == 1) show("revive");
                else if ((scenario == "revive-chest" && target_battles == 2) || scenario == "direct-chest" ||
                    scenario == "interlude-combat") show("chest");
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
            {"revival_inputs", resumes}, {"target_attempt", context_attempts}}.dump(2);
        if (scenario == "interlude-combat") {
            check(driver.last.code == "target.continuation_identity_unconfirmed" &&
                summary.at("task_step") == 0 && summary.at("target_encounter").at("active") == true &&
                !executor->has_unresolved_input(), "INTERLUDE_COMBAT_MUST_NOT_COMPLETE_OR_REBIND_TARGET");
            std::cout << "giant linkage interlude-combat PASS; target remains incomplete; explicit identity failure\n";
            continue;
        }
        check(driver.last.state == runtime::TickState::Completed && unit == 2 &&
            summary.at("bounty_cycle").at("completed_cycles") == 1 && summary.at("task_step") == 2 &&
            target_settled && !summary.at("target_encounter").at("active").get<bool>() && !executor->has_unresolved_input(),
            "GIANT_LINKAGE:" + scenario + ":" + driver.last.code + ":" + executor->current_step_id());
        check(scenario != "revive-chest" || (target_battles == 2 && resumes == 1 && context_attempts == 1),
            "REVIVAL_MUST_CONTINUE_SAME_TARGET_ATTEMPT");
        std::cout << "giant linkage " << scenario << " PASS; task_step=2; bounty_cycle=1; pending=0\n";
    }
    return 0;
}
}
