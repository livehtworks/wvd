#include "games/wvd/combat/strategy.hpp"
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
        whole.consume(*whole.select({{"actor", .95}}), games::SkillOutcome::Succeeded);
        check(whole.automatic(), "GROUP_COMPLETION_NOT_APPLIED");

        // 从正式数据只读复制，全部迁移写入新建可丢弃目录，不碰原配置。
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
