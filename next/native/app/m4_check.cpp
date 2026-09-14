#include "storage/legacy_import.hpp"
#include "storage/profile_store.hpp"
#include "games/wvd/tasks/quest_catalog.hpp"
#include "games/wvd/tasks/task_plan.hpp"
#include "maafw/buffers.hpp"
#include <fstream>
#include <iostream>

namespace {
using J = nlohmann::json;
J read(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("M4_INPUT_UNREADABLE");
    return wvd::storage::parse_legacy_json({std::istreambuf_iterator<char>(input), {}});
}
} // namespace
int main(int argc, char **argv) {
    using namespace wvd;
    if (argc != 2) {
        std::cerr << "M4_PRIVATE_CONFIG_REQUIRED";
        return 2;
    }
    J result;
    std::filesystem::path output;
    try {
        auto config = read(maafw::path_from_utf8(argv[1]));
        output = maafw::path_from_utf8(config.at("output"));
        if (config.contains("device") || config.contains("binding") ||
            config.value("execute", false))
            throw std::runtime_error("M4_REAL_DEVICE_AND_EXECUTION_NOT_ENABLED");
        storage::LegacyConfigImporter importer(
            read(maafw::path_from_utf8(config.at("descriptor"))));
        auto profile =
            config.contains("legacy_source")
                ? importer.import_copy(maafw::path_from_utf8(config.at("legacy_source")),
                                       maafw::path_from_utf8(config.at("copy_directory")))
                : importer.parse(config.contains("raw_json")
                                     ? storage::parse_legacy_json(config.at("raw_json"))
                                     : config.at("source"));
        if (config.contains("changed_values"))
            profile.values.update(config.at("changed_values"));
        result = {{"schema", 1},
                  {"stage", "M4_PARTIAL_IMPLEMENTATION"},
                  {"values", profile.values},
                  {"sources", profile.sources},
                  {"selected_section", profile.selected_section},
                  {"legacy_passthrough", profile.legacy_passthrough},
                  {"export", importer.export_legacy(profile)},
                  {"real_connections", 0},
                  {"real_inputs", 0},
                  {"execution_available", false}};
        if (config.contains("quests")) {
            auto baseline = read(maafw::path_from_utf8(config.at("quests")));
            J diagnostics = J::array();
            for (const auto &mod : config.value("mods", J::array()))
                baseline = storage::merge_legacy_quests(
                    baseline, nlohmann::ordered_json::parse(mod.get<std::string>()), diagnostics);
            games::WvdQuestCatalog catalog(baseline);
            result["quests"] = catalog.export_source();
            result["mod_diagnostics"] = diagnostics;
            result["task_ids"] = J::array();
            for (const auto &task : catalog.tasks())
                result["task_ids"].push_back(task.id);
            if (config.value("inspect_plans", false)) {
                result["plans"] = J::array();
                for (const auto &task : catalog.tasks())
                    result["plans"].push_back(games::WvdTaskPlan::parse(task).inspect());
            }
        }
        if (config.contains("profile_path")) {
            storage::ProfileStore store(maafw::path_from_utf8(config.at("profile_path")),
                                        read(maafw::path_from_utf8(config.at("descriptor"))));
            auto original = store.create(profile);
            result["profile_created"] = original;
            auto draft = original;
            draft["values"]["KARMA_ADJUST"] = "+1";
            auto saved = store.compare_exchange(original.at("revision"), draft);
            result["profile_saved"] = saved;
            try {
                store.compare_exchange(original.at("revision"), original);
                result["cas_conflict"] = false;
            } catch (const std::runtime_error &e) {
                result["cas_conflict"] = std::string(e.what()) == "PROFILE_CONFLICT";
            }
            result["profile_after_conflict"] = store.load();
            auto incomplete = saved;
            incomplete["values"].erase("KARMA_ADJUST");
            try {
                store.compare_exchange(saved.at("revision"), incomplete);
                result["incomplete_rejected"] = false;
            } catch (const std::exception &) {
                result["incomplete_rejected"] = true;
            }
            result["profile_after_rejected_draft"] = store.load();
        }
        result["outcome"] = "PASS";
    } catch (const std::exception &e) {
        result = {
            {"outcome", "Error"}, {"error", e.what()}, {"real_connections", 0}, {"real_inputs", 0}};
    }
    if (output.empty()) {
        std::cerr << result.dump();
        return 2;
    }
    std::ofstream file(output);
    file << result.dump(2);
    file.close();
    return file ? 0 : 3;
}
