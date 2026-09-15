#include "storage/legacy_import.hpp"
#include "storage/profile_store.hpp"
#include "storage/pipeline_bundle.hpp"
#include "games/wvd/tasks/quest_catalog.hpp"
#include "games/wvd/tasks/task_plan.hpp"
#include "games/wvd/tasks/dungeon_route.hpp"
#include "games/wvd/tasks/dungeon_iteration.hpp"
#include "games/wvd/tasks/fortress_trap.hpp"
#include "games/wvd/tasks/giant.hpp"
#include "games/wvd/tasks/dark_light.hpp"
#include "games/wvd/tasks/mining.hpp"
#include "games/wvd/tasks/manual_separation.hpp"
#include "games/wvd/tasks/sleep_visits.hpp"
#include "games/wvd/tasks/fishing_supply.hpp"
#include "games/wvd/tasks/featured_request.hpp"
#include "games/wvd/tasks/golden_chest.hpp"
#include "games/wvd/quests/sleep_visits.hpp"
#include "games/wvd/tasks/bounty_cycle.hpp"
#include "games/wvd/navigation/dungeon_entry.hpp"
#include "games/wvd/vision/asset_resolver.hpp"
#include "maafw/buffers.hpp"
#include <fstream>
#include <iostream>
#include <set>

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
            if (config.contains("compile_entries_manifest") || config.contains("compile_routes_manifest") || config.contains("compile_iterations_manifest") || config.contains("compile_specials_manifest")) {
                const bool routes = config.contains("compile_routes_manifest");
                const bool iterations = config.contains("compile_iterations_manifest");
                const bool specials = config.contains("compile_specials_manifest");
                if (int(routes) + int(iterations) + int(specials) + int(config.contains("compile_entries_manifest")) != 1)
                    throw std::runtime_error("M4_COMPILE_SCOPE_AMBIGUOUS");
                const auto manifest = read(maafw::path_from_utf8(config.at(specials ? "compile_specials_manifest" : iterations ? "compile_iterations_manifest" : routes ? "compile_routes_manifest" : "compile_entries_manifest")));
                std::set<std::string> files, images;
                maafw::Bundle manifest_bundle;
                for (const auto &file : manifest.at("files")) {
                    const auto path = file.at("path").get<std::string>();
                    manifest_bundle.files.push_back({path, file.at("sha256")});
                    files.insert(path);
                    if (path.starts_with("image/"))
                        images.insert(path.substr(6));
                }
                const auto &aliases = manifest.at("aliases");
                const auto key = specials ? "compiled_specials" : iterations ? "compiled_iterations" : routes ? "compiled_routes" : "compiled_entries";
                result[key] = J::array();
                if (specials)
                    result["unimplemented_specials"] = J::array();
                if (specials) {
                    result["compiled_featured_requests"] = J::array();
                    for (auto operation : {games::tasks::FeaturedRequest::BullCave, games::tasks::FeaturedRequest::GoldenChest}) {
                        const auto graph = games::tasks::accept_featured_request(operation, profile.values.at("ACTIVE_ROYALSUITE_REST").get<bool>());
                        J missing = J::array();
                        for (const auto &image : graph.images) {
                            const auto selected = games::vision::resolve_image_source(manifest_bundle, aliases, image);
                            if (!files.contains(selected.relative_path)) missing.push_back(image);
                        }
                        result["compiled_featured_requests"].push_back({{"kind", graph.kind}, {"images", graph.images},
                            {"missing_images", missing}, {"scope", "FEATURED_VISIT_ONLY_NOT_FULL_TASK"}, {"executed", false}});
                    }
                }
                for (const auto &task : catalog.tasks()) {
                    if (task.type != (specials ? "quest" : "dungeon"))
                        continue;
                    const bool scorpion = task.id == "Scorpionesses" || task.id == "Scorpionesses_plus_6_hands";
                    const bool bounty = scorpion || task.id == "jier";
                    const bool fishing = task.id == "fishing" || task.id == "fishing2";
                    if (specials && !bounty && !fishing && task.id != "SSC-goldenchest" && task.id != "fortress-B8F_trap" && task.id != "gaintKiller" && task.id != "darkLight" && task.id != "FFXI-Org" && task.id != "manualSepDemon" && task.id != "lovesleep") {
                        result["unimplemented_specials"].push_back(task.id);
                        continue;
                    }
                    const auto plan = games::WvdTaskPlan::parse(task);
                    const auto graph = [&] {
                        if (!specials) {
                            if (iterations) return games::tasks::dungeon_iteration(plan, profile.values, images);
                            if (routes) return games::tasks::traverse_dungeon(plan, profile.values, images);
                            return games::navigation::enter_dungeon(plan);
                        }
                        if (bounty) return games::tasks::bounty_cycle(task, profile.values, images);
                        if (fishing) return games::tasks::fishing_cycle(task, profile.values, images);
                        if (task.id == "SSC-goldenchest") return games::tasks::golden_chest_cycle(task, profile.values, images);
                        if (task.id == "lovesleep") return games::tasks::sleep_visits(task, profile.values);
                        if (task.id == "manualSepDemon") return games::tasks::manual_separation(task, profile.values, images);
                        if (task.id == "FFXI-Org") return games::tasks::mining_iteration(task, profile.values);
                        if (task.id == "darkLight") return games::tasks::dark_light(task, profile.values, images);
                        if (task.id == "gaintKiller") return games::tasks::giant_iteration(task, profile.values, images);
                        return games::tasks::fortress_trap_iteration(task, profile.values, images);
                    }();
                    J missing = J::array();
                    for (const auto &image : graph.images) {
                        const auto selected = games::vision::resolve_image_source(manifest_bundle, aliases, image);
                        if (!files.contains(selected.relative_path))
                            missing.push_back(image);
                    }
                    result[key].push_back({{"task_id", task.id}, {"nodes", graph.nodes},
                        {"images", graph.images}, {"required_actions", graph.required_actions},
                        {"missing_images", missing}, {"scope", specials ? "FINITE_SPECIAL_ITERATION_NOT_FULL_TASK" : iterations ? "NORMAL_FARM_ITERATION_NOT_FULL_TASK" : routes ? "DUNGEON_ROUTE_ONLY_NOT_FULL_TASK" : "ENTRY_ONLY_NOT_FULL_TASK"},
                        {"required_normal_units", bounty ? (task.id == "Scorpionesses_plus_6_hands" ? 4 : 3) : task.id == "lovesleep" ? games::quests::SleepVisits::units : task.id == "manualSepDemon" || task.id == "SSC-goldenchest" ? 2 : 1}, {"executed", false}});
                }
            }
        }
        if (config.contains("prepare_pipeline_bundle")) {
            const auto &request = config.at("prepare_pipeline_bundle");
            maafw::Bundle source{maafw::path_from_utf8(request.at("root")), request.at("revision"), {}};
            for (const auto &file : request.at("files"))
                source.files.push_back({file.at("path"), file.at("sha256")});
            const auto prepared = storage::prepare_pipeline_bundle(source, maafw::path_from_utf8(request.at("destination")));
            J files = J::array();
            for (const auto &file : prepared.files)
                files.push_back({{"path", file.relative_path}, {"sha256", file.sha256}});
            result["prepared_pipeline_bundle"] = {{"revision", prepared.revision}, {"files", files},
                {"scope", "PREPARED_BEFORE_RUN_DEFINITION"}, {"executed", false}};
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
