#include "games/wvd/combat/strategy.hpp"
#include "games/wvd/combat/selection_diagnostics.hpp"
#include "games/wvd/state.hpp"
#include "games/wvd/business_condition.hpp"
#include "storage/profile_store.hpp"
#include "platform/windows/file_digest.hpp"
#include <fstream>
#include <iostream>
#include <chrono>
using J = nlohmann::json;
using namespace wvd;
void check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
J read(const std::filesystem::path &path) { std::ifstream in(path); return J::parse(in); }
int main(int argc, char **argv) {
    try {
        check(argc == 2, "PASS_READ_ONLY_PROFILE_PATH");
        const auto descriptor = read("packs/wvd/parameters/legacy-config-fields.json");
        auto profile = storage::LegacyConfigImporter(descriptor).parse({{"GENERAL", J::object()}}).values;
        profile["DEFAULT_OVERALL_STRATEGY"] = "test";
        profile["TASK_SPECIFIC_CONFIG"] = false;
        const J once{{"role_var", "actor"}, {"skill_var", "左上技能"}, {"skill_lvl", 1},
            {"target_var", "左上角色"}, {"freq_var", "用完后移除"}};
        auto repeat = once; repeat["freq_var"] = "重复"; repeat["skill_var"] = "左下技能";
        profile["STRATEGY"] = J::array({{{"group_name", "test"}, {"skill_settings", J::array({once, repeat})}}});
        games::CombatStrategy strategy(profile); strategy.reload(0);
        auto first = *strategy.select({{"actor", .95}});
        check(!strategy.consume(first, games::SkillOutcome::TargetFailed), "FAILED_ACTION_REMOVED");
        check(!strategy.consume(first, games::SkillOutcome::AutoFallback), "UNCONFIRMED_FALLBACK_REMOVED");
        check(strategy.consume(first, games::SkillOutcome::AutoFallbackConfirmed), "FALLBACK_NOT_SETTLED");
        auto retried = *strategy.select({{"actor", .95}});
        check(retried.skill == once && retried.strategy_epoch != first.strategy_epoch,
            "FALLBACK_CONSUMED_UNCAST_SKILL");
        first = retried;
        check(strategy.consume(first, games::SkillOutcome::Succeeded), "ONCE_NOT_CONFIRMED");
        auto next = *strategy.select({{"actor", .95}});
        check(next.skill == repeat, "ONCE_DID_NOT_ADVANCE");
        strategy.consume(next, games::SkillOutcome::Succeeded);
        auto again = *strategy.select({{"actor", .95}});
        check(again.skill == repeat && again.strategy_epoch != next.strategy_epoch, "REPEAT_OR_EPOCH_INVALID");
        bool stale = false;
        try { strategy.consume(next, games::SkillOutcome::Succeeded); }
        catch (const std::exception &e) { stale = std::string(e.what()) == "STALE_STRATEGY_SELECTION"; }
        check(stale, "DUPLICATE_CONFIRMATION_ACCEPTED");
        strategy.consume(again, games::SkillOutcome::AutoFallbackConfirmed);
        check(strategy.select({{"actor", .95}})->skill == repeat, "REPEAT_FALLBACK_REMOVED");
        strategy.reload(0);
        check(strategy.select({{"actor", .95}})->skill == once, "RESET_NOT_RESTORED");
        profile["STRATEGY"][0]["complete_one_as_all"] = true;
        profile["STRATEGY"][0]["skill_settings"] = J::array({repeat});
        games::CombatStrategy whole(profile); whole.reload(0);
        whole.consume(*whole.select({{"actor", .95}}), games::SkillOutcome::AutoFallbackConfirmed);
        check(!whole.automatic(), "FALLBACK_COMPLETED_GROUP");
        whole.consume(*whole.select({{"actor", .95}}), games::SkillOutcome::Succeeded);
        check(whole.automatic(), "GROUP_COMPLETION_NOT_APPLIED");
        // The incident had a 0.97351 portrait hit after its once-only row was consumed.
        auto other = repeat; other["role_var"] = "other";
        profile["STRATEGY"][0]["complete_one_as_all"] = false;
        profile["STRATEGY"][0]["skill_settings"] = J::array({once, other});
        games::CombatStrategy diagnostic_strategy(profile); diagnostic_strategy.reload(0);
        diagnostic_strategy.consume(*diagnostic_strategy.select({{"actor", .97351}}), games::SkillOutcome::Succeeded);
        const J summary{{"strategy", diagnostic_strategy.summary()}, {"has_prepared_skill", false},
            {"prepared_portrait", ""}, {"prepared_skill_index", nullptr}};
        const auto diagnosed = games::combat::selection_diagnostics(summary, {{"actor", .97351}, {"other", .31}});
        check(diagnosed.at("reason") == "recognized_actor_no_remaining_action" &&
              diagnosed.at("portrait") == "actor" && diagnosed.at("portrait_recognized") == true &&
              diagnosed.at("remaining_actor_actions") == 0, "CONSUMED_ACTION_MISREPORTED_AS_VISION_FAILURE");
        check(games::combat::selection_diagnostics(summary, {{"actor", .60}}).at("reason") == "portrait_unrecognized",
              "LOW_SCORE_MISREPORTED_AS_EXHAUSTED_ACTION");
        auto exhausted = summary; exhausted["strategy"] = whole.summary();
        check(games::combat::selection_diagnostics(exhausted, {{"actor", .97}}).at("reason") == "strategy_actions_exhausted",
              "EXHAUSTED_STRATEGY_MISREPORTED_AS_EXPLICIT_AUTO");
        games::WvdRunState actor_state(profile, {"portrait-transition", 1,
            std::make_shared<contracts::SteadyClock>()});
        actor_state.enter_segment(contracts::SegmentBoundary::Initial, 1, 0);
        const auto catalog = profile["STRATEGY"][0]["skill_settings"];
        const J actor_condition{{"mode", "business"}, {"field", "/combat_actor_recognized"},
            {"value", true}, {"comparison", "eq"}};
        actor_state.prepare_skill({{"actor", .6379926}}, catalog);
        check(actor_state.summary().at("combat_actor_recognized") == false &&
              actor_state.summary().at("has_prepared_skill") == false, "TRANSITION_FRAME_AUTHORIZED_FALLBACK");
        check(!games::business_condition(actor_state.summary(), actor_condition), "TRANSITION_FRAME_CONDITION_HIT");
        actor_state.prepare_skill({{"actor", .9735101}}, catalog);
        check(actor_state.summary().at("combat_actor_recognized") == true &&
              actor_state.summary().at("has_prepared_skill") == true, "STABLE_FRAME_NOT_RESELECTED");
        check(games::business_condition(actor_state.summary(), actor_condition), "STABLE_FRAME_CONDITION_MISSED");
        if (std::string(argv[1]) == "--semantics") {
            std::cout << "PASS: once/repeat, fallback preservation, group precedence, epoch invalidation. No profile writes or device input.\n";
            return 0;
        }

        // 从正式数据只读复制，全部迁移写入新建可丢弃目录，不碰原配置。
        if (std::string(argv[1]) == "--fallback-contract") {
            strategy.reload(0);
            const auto selected = *strategy.select({{"actor", .95}});
            check(strategy.consume(selected, games::SkillOutcome::DefendFallbackConfirmed), "DEFEND_FALLBACK_NOT_SETTLED");
            const auto preserved = *strategy.select({{"actor", .95}});
            check(preserved.skill == once && preserved.strategy_epoch != selected.strategy_epoch,
                "DEFEND_FALLBACK_CONSUMED_UNCAST_SKILL");
            games::CombatStrategy group(profile); group.reload(0);
            group.consume(*group.select({{"actor", .95}}), games::SkillOutcome::DefendFallbackConfirmed);
            check(!group.automatic(), "DEFEND_FALLBACK_COMPLETED_GROUP");
            std::cout << "PASS: manual defend confirms action, preserves uncast skill and advances epoch; no profile IO.\n";
            return 0;
        }
        const std::filesystem::path source = argv[1];
        const auto before = platform::file_sha256(source);
        const auto root = std::filesystem::absolute(".local") /
            ("strategy-frequency-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root);
        std::filesystem::copy_file(source, root / "profile.json");
        storage::ProfileStore store(root / "profile.json", descriptor);
        const auto migrated = store.load();
        check(migrated.at("strategy_settings_version") == 2, "MIGRATION_VERSION");
        auto expected = read(source);
        auto normalize = [](J &section) { if (section.is_object() && section.contains("STRATEGY")) games::normalize_strategy(section["STRATEGY"]); };
        for (const auto *key : {"values", "default_values"}) if (expected.contains(key)) normalize(expected[key]);
        for (const auto *key : {"legacy_document", "task_overrides"})
            if (expected.contains(key)) for (auto &section : expected[key]) normalize(section);
        expected["strategy_settings_version"] = 2; expected["revision"] = migrated.at("revision");
        check(migrated == expected, "UNRELATED_CONFIGURATION_CHANGED");
        bool backup = false;
        for (const auto &file : std::filesystem::directory_iterator(root))
            if (file.path().filename().string().starts_with("strategy-settings-v1-"))
                backup |= platform::file_sha256(file.path()) == before;
        check(backup, "MIGRATION_BACKUP_MISSING");
        storage::ProfileStore second(root / "profile.json", descriptor);
        check(second.load() == migrated, "MIGRATION_NOT_IDEMPOTENT");
        auto changed = migrated;
        changed["values"]["STRATEGY"][0]["skill_settings"][0]["freq_var"] = "用完后移除";
        const auto saved = second.compare_exchange(migrated.at("revision"), changed);
        check(second.load() == saved, "SAVE_RELOAD_CHANGED");
        check(platform::file_sha256(source) == before, "SOURCE_PROFILE_CHANGED");
        std::cout << "PASS: once/repeat, next actor selection, failed/unconfirmed outcomes, duplicate rejection, reset, group precedence, full profile migration/backup/CAS/reload. No device input.\n";
        std::cout << "Evidence directory: " << root.string() << '\n';
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
