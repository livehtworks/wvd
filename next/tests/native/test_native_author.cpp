#include "games/wvd/tasks/author_workflow.hpp"
#include "games/wvd/tasks/native_program.hpp"
#include "games/wvd/tasks/public_flow_library.hpp"
#include "games/wvd/tasks/bounty_visit.hpp"
#include "games/wvd/tasks/locale_assets.hpp"
#include "games/wvd/tasks/native_publisher.hpp"
#include "games/wvd/navigation/time_leap.hpp"
#include "games/wvd/navigation/map_route.hpp"
#include "games/wvd/combat/encounter.hpp"
#include "games/wvd/combat/strategy.hpp"
#include "games/wvd/supply/inn.hpp"
#include "games/wvd/recovery/boot.hpp"
#include "games/wvd/vision/boot_probes.hpp"
#include "platform/windows/file_digest.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

int main() {
    try {
        using J = nlohmann::json;
        for (const auto &point : {wvd::games::TaskPoint{505, 760},
                                  wvd::games::TaskPoint{506, 821}}) {
            wvd::games::MapTarget target;
            target.target = "position";
            target.hint = wvd::games::MapTarget::Hint::Position;
            target.position = point;
            target.swipes = {wvd::games::TaskSwipe{{100, 1200}, {700, 250}}};
            target.harken_arrival = point[1] == 821;
            const auto map = wvd::games::navigation::reach_map_target(target, "B2FTemple");
            const auto &move = map.nodes.at("AutoMove").at("operation_args");
            const auto recognition = move.at("target_recognition").dump();
            if (!move.at("use_target_center").get<bool>() ||
                move.at("command").contains("x") ||
                recognition.find("AutoMove") == std::string::npos ||
                recognition.find("\"threshold\":0.8") == std::string::npos ||
                recognition.find("\"roi\":[" + std::to_string(point[0] - 116) + "," +
                                 std::to_string(point[1] - 200)) == std::string::npos ||
                move.at("postcondition").dump().find("combat_active") == std::string::npos ||
                map.nodes.contains("HarkenArrived") != target.harken_arrival ||
                (target.harken_arrival && move.at("postcondition").dump().find("harken_floor_return_zh_hant") == std::string::npos))
                throw std::runtime_error("MAP_AUTOMOVE_POPUP_GUARD_INVALID");
        }
        J special_profile{{"LANGUAGE", "zh_CN"}, {"TASK_SPECIFIC_CONFIG", false},
            {"DEFAULT_OVERALL_STRATEGY", "普通方案"},
            {"STRATEGY", J::array({J{{"group_name", "普通方案"}, {"skill_settings", J::array()}},
                                   J{{"group_name", "特殊方案"}, {"skill_settings", J::array()}}})},
            {"TASK_POINT_STRATEGY", {{"special_combat", {{"skull", true}, {"portrait", true},
                {"portrait_image", "combat_scorpion_portrait"}, {"normal_strategy", "普通方案"},
                {"special_strategy", "特殊方案"}}}}}};
        wvd::games::CombatStrategy selected(special_profile);
        selected.reload(0);
        selected.begin_encounter(false);
        if (selected.summary().at("current").at("group_name") != "普通方案")
            throw std::runtime_error("SPECIAL_COMBAT_NORMAL_STRATEGY_INVALID");
        selected.begin_encounter(true);
        if (selected.summary().at("current").at("group_name") != "特殊方案")
            throw std::runtime_error("SPECIAL_COMBAT_BOSS_STRATEGY_INVALID");
        const auto both = wvd::games::combat::fight_encounter(special_profile, {}, 1);
        const auto special_condition = both.nodes.at("Special").at("observation_args").dump();
        if (!both.nodes.contains("WaitingForMenu") ||
            special_condition.find("combat_special_skull") == std::string::npos ||
            special_condition.find("combat_scorpion_portrait") == std::string::npos ||
            special_condition.find("\"mode\":\"any\"") == std::string::npos)
            throw std::runtime_error("SPECIAL_COMBAT_OR_BRANCH_INVALID");
        special_profile["TASK_POINT_STRATEGY"]["special_combat"]["portrait"] = false;
        const auto skull_only = wvd::games::combat::fight_encounter(special_profile, {}, 1);
        if (skull_only.nodes.at("Special").at("observation_args").dump().find("combat_scorpion_portrait") != std::string::npos)
            throw std::runtime_error("SPECIAL_COMBAT_SKULL_ONLY_INVALID");
        special_profile["TASK_POINT_STRATEGY"]["special_combat"]["portrait"] = true;
        special_profile["TASK_POINT_STRATEGY"]["special_combat"]["skull"] = false;
        const auto portrait_only = wvd::games::combat::fight_encounter(special_profile, {}, 1);
        if (portrait_only.nodes.at("Special").at("observation_args").dump().find("combat_special_skull") != std::string::npos)
            throw std::runtime_error("SPECIAL_COMBAT_PORTRAIT_ONLY_INVALID");
        special_profile["TASK_POINT_STRATEGY"]["special_combat"]["portrait"] = false;
        const auto legacy = wvd::games::combat::fight_encounter(special_profile, {}, 1);
        if (legacy.nodes.contains("Special") || !legacy.nodes.contains("Observed"))
            throw std::runtime_error("SPECIAL_COMBAT_DISABLED_CHANGED_ENTRY");
        J document{{"schema", 1},
            {"flow", {{"id", "slot-six"}, {"name", "Slot six"}, {"description", ""}}},
            {"entry", "Slot"},
            {"nodes", J::array({
                J{{"id", "Slot"}, {"type", "slot"}, {"name", "Slot"},
                  {"parameters", {{"name", "extra"}, {"calls", J::array({
                      J{{"flow_id", "shared"}}, J{{"flow_id", "shared"}}})}}},
                  {"repeat_limit", 6}},
                J{{"id", "Done"}, {"type", "end"}, {"name", "Done"},
                  {"parameters", {{"outcome", "success"}}}}})},
            {"edges", J::array({J{{"id", "slot_done"}, {"from", "Slot"},
                {"to", "Done"}, {"outcome", "success"}, {"order", 0}}})},
            {"layout", {{"nodes", J::array({J{{"node_id", "Slot"}, {"x", 40}, {"y", 100}},
                J{{"node_id", "Done"}, {"x", 300}, {"y", 100}}})},
                {"viewport", {{"x", 0}, {"y", 0}, {"zoom", 1}}}}},
            {"execution", {{"time_limit_ms", 30000}}}};
        int resolutions = 0;
        const auto compiled = wvd::games::tasks::compile_author_workflow(
            document, {}, [&](const J &) {
                ++resolutions;
                wvd::games::tasks::PipelineCompiler child("test.shared");
                child.route("Entry", {"Terminal"});
                return wvd::games::tasks::AuthorWorkflowCompilation{child.finish()};
            });
        auto program = wvd::games::tasks::compile_native_program(
            compiled.workflow, compiled.source_paths, "slot-six-native");
        program.validate();
        const auto &root = program.definitions.at(program.root_definition);
        if (resolutions != 2 || root.steps.at("Author_Slot").max_hit != 6 ||
            root.steps.at("Author_Slot_Call0").max_hit != 6 ||
            root.steps.at("Author_Slot_Call1").max_hit != 6 ||
            program.definitions.size() != 3)
            throw std::runtime_error("NATIVE_SLOT_CALL_SCOPE_INVALID");
        const auto &first = std::get<wvd::workflow::Call>(
            root.steps.at("Author_Slot_Call0").data);
        const auto &second = std::get<wvd::workflow::Call>(
            root.steps.at("Author_Slot_Call1").data);
        if (first.definition == second.definition ||
            !program.definitions.contains(first.definition) ||
            !program.definitions.contains(second.definition))
            throw std::runtime_error("NATIVE_SLOT_CALL_SHARED_COUNTERS");
        std::ifstream flows("resources/authoring/public-flows.json");
        std::ifstream resources("resources/authoring/semantic-assets.json");
        if (!flows || !resources) throw std::runtime_error("AUTHOR_RESOURCE_SOURCE_MISSING");
        J documents = J::object(), entries, catalogue;
        flows >> entries;
        resources >> catalogue;
        wvd::authoring::SemanticAssets semantic(catalogue);
        const auto open = wvd::games::vision::chest_open_probes();
        const auto stages = wvd::games::vision::chest_stage_probes();
        const auto boot = wvd::games::vision::boot_probes(false);
        if (open.at(1) != semantic.condition("chest.open.option", "zh-Hant") ||
            stages.back() != semantic.condition("chest.reward.page", "") ||
            boot.at(1) != semantic.condition("guild.commissions.page", "zh-Hant") ||
            boot.at(2) != semantic.condition("guild.bounties.page", "zh-Hant"))
            throw std::runtime_error("NATIVE_SEMANTIC_PROBES_DIVERGED");
        for (const auto &entry : entries)
            documents[entry.at("flow").at("id").get<std::string>()] = entry;
        const auto &wheel = documents.at("wheel-select-target");
        const auto &wheel_nodes = wheel.at("nodes");
        const auto &wheel_edges = wheel.at("edges");
        auto author_node = [&](const std::string &id) -> J {
            for (const auto &node : wheel_nodes)
                if (node.at("id") == id) return node;
            throw std::runtime_error("WHEEL_AUTHOR_NODE_MISSING:" + id);
        };
        auto author_edge = [&](const std::string &from, const std::string &to) {
            for (const auto &edge : wheel_edges)
                if (edge.at("from") == from && edge.at("to") == to) return true;
            return false;
        };
        if (!author_edge("Entry", "QuickSelect") || !author_edge("Entry", "FindChapter") ||
            !author_edge("Scroll0", "FindTarget") || author_edge("Scroll0", "Scroll1") ||
            author_node("NextFromAbyss").at("parameters").at("postcondition").at("id") != "wheel.chapter.waterway" ||
            author_node("NextFromWaterway").at("parameters").at("postcondition").at("id") != "wheel.chapter.fortress" ||
            author_node("NextFromFortress").at("parameters").at("postcondition").at("id") != "wheel.chapter.dhi")
            throw std::runtime_error("WHEEL_AUTHOR_DIRECTION_OR_TARGET_RECHECK_INVALID");
        const auto ore_leap = wvd::games::navigation::time_leap_without_causality(
            "BeautifulOre", "cursedwheel_dhi", true);
        const auto fortress_leap = wvd::games::navigation::time_leap_without_causality(
            "GhostsOfYore", "cursedwheel_impregnableFortress", true);
        if (ore_leap.nodes.contains("Reset0") || ore_leap.nodes.contains("Scroll1") ||
            ore_leap.nodes.at("MoveFrom0").at("operation_args").at("target_recognition").dump().find("cursedWheelTapRight") == std::string::npos ||
            fortress_leap.nodes.at("MoveFrom3").at("operation_args").at("target_recognition").dump().find("cursedWheelTapLeft") == std::string::npos ||
            ore_leap.nodes.at("LeapRoute").at("next").at(0) != "Done" ||
            ore_leap.nodes.at("Scroll0").at("next").at(0) != "FindTarget" ||
            ore_leap.nodes.at("Scroll0").at("max_hit") != 6)
            throw std::runtime_error("WHEEL_NATIVE_DIRECTION_OR_TARGET_RECHECK_INVALID");
        wvd::games::tasks::PublicFlowLibrary library(documents, catalogue);
        const auto &board = documents.at("guild-open-bounty-page");
        auto reveal = wvd::games::tasks::visit_bounty_board(
            wvd::games::tasks::BountyVisit::Reveal, library, board, "zh-Hant");
        auto reveal_program = wvd::games::tasks::compile_native_program(
            reveal, reveal.authoring.value("source_paths", J::object()), "bounty-reveal");
        reveal_program.validate();
        if (!reveal.authoring.contains("public_definitions") ||
            !reveal.authoring.at("public_definitions").contains("guild-open-bounty-page") ||
            !reveal.nodes.contains("CloseReveal") ||
            reveal.nodes.at("CloseReveal").at("operation_args").at("target_recognition")
                .at("parameters").at("image") != "guild_reveal_close_zh_hant")
            throw std::runtime_error("BOUNTY_PUBLIC_DEFINITION_NOT_FROZEN");
        wvd::games::tasks::PipelineCompiler map_graph("test.worldmap");
        map_graph.observe("Entry", wvd::games::tasks::PipelineCompiler::image("worldmapflag"), {"Terminal"});
        auto map_workflow = map_graph.finish();
        wvd::games::tasks::localize_task_assets(map_workflow, catalogue, "zh-Hant");
        const auto map_program = wvd::games::tasks::compile_native_program(
            map_workflow, J::object(), "zh-worldmap");
        map_program.validate();
        bool map_composite = false;
        for (const auto &[id, node] : map_workflow.nodes.items()) {
            (void)id;
            if (node.value("observation", "") == "Registered" &&
                node.value("recognizer", "") == "WvdVision")
                map_composite = node.at("observation_args").value("mode", "") == "all";
        }
        if (!map_composite) throw std::runtime_error("ZH_WORLDMAP_COMPOSITE_MISSING");
        wvd::games::tasks::PipelineCompiler entry_graph("test.zh-abyss-entry");
        entry_graph.observe("Entry", wvd::games::tasks::PipelineCompiler::any({
            wvd::games::tasks::PipelineCompiler::image("outskirts_abyss_zh_hant"),
            wvd::games::tasks::PipelineCompiler::image("outskirts_abyss_b2f_zh_hant"),
            wvd::games::tasks::PipelineCompiler::image("map_abyss_b2f_zh_hant"),
            wvd::games::tasks::PipelineCompiler::image("AutoMove"),
            wvd::games::tasks::PipelineCompiler::image("mapFlag")}), {"Terminal"});
        auto entry_workflow = entry_graph.finish();
        wvd::games::tasks::localize_task_assets(entry_workflow, catalogue, "zh-Hant");
        const auto entry_conditions = entry_workflow.nodes.at("Entry").at("observation_args").dump();
        if (entry_conditions.find("outskirts_abyss_zh_hant") == std::string::npos ||
            entry_conditions.find("outskirts_abyss_b2f_zh_hant") == std::string::npos ||
            entry_conditions.find("map_abyss_b2f_zh_hant") == std::string::npos ||
            entry_conditions.find("map_auto_move_zh_hant") == std::string::npos ||
            entry_conditions.find("dungeon_map_close_zh_hant") == std::string::npos)
            throw std::runtime_error("ZH_ABYSS_ENTRY_ASSETS_MISSING");
        wvd::games::tasks::PipelineCompiler combat_graph("test.combat-locale");
        combat_graph.observe("Entry", wvd::games::tasks::PipelineCompiler::any({
            wvd::games::tasks::PipelineCompiler::image("combat_skill_detail"),
            wvd::games::tasks::PipelineCompiler::image("combat_skill_confirm")}), {"Terminal"});
        auto zh_combat = combat_graph.finish();
        auto en_combat = zh_combat;
        wvd::games::tasks::localize_task_assets(zh_combat, catalogue, "zh-Hant");
        wvd::games::tasks::localize_task_assets(en_combat, catalogue, "en");
        const auto zh_conditions = zh_combat.nodes.at("Entry").at("observation_args").dump();
        const auto en_conditions = en_combat.nodes.at("Entry").at("observation_args").dump();
        if (zh_conditions.find("combat_skill_detail_zh_hant") == std::string::npos ||
            zh_conditions.find("combat_skill_confirm_zh_hant") == std::string::npos ||
            en_conditions.find("spellskill/skillDetail") == std::string::npos ||
            en_conditions.find("\"image\":\"OK\"") == std::string::npos)
            throw std::runtime_error("COMBAT_LOCALE_ASSETS_MISSING");
        wvd::games::tasks::PipelineCompiler healing_graph("test.healing-locale");
        healing_graph.observe("Entry", wvd::games::tasks::PipelineCompiler::any({
            wvd::games::tasks::PipelineCompiler::image("trait"),
            wvd::games::tasks::PipelineCompiler::image("recover")}), {"Terminal"});
        auto zh_healing = healing_graph.finish();
        auto en_healing = zh_healing;
        wvd::games::tasks::localize_task_assets(zh_healing, catalogue, "zh-Hant");
        wvd::games::tasks::localize_task_assets(en_healing, catalogue, "en");
        const auto zh_healing_conditions = zh_healing.nodes.at("Entry").at("observation_args").dump();
        const auto en_healing_conditions = en_healing.nodes.at("Entry").at("observation_args").dump();
        if (zh_healing_conditions.find("character_panel_zh_hant") == std::string::npos ||
            zh_healing_conditions.find("recovery_panel_zh_hant") == std::string::npos ||
            en_healing_conditions.find("\"image\":\"trait\"") == std::string::npos ||
            en_healing_conditions.find("\"image\":\"recover\"") == std::string::npos)
            throw std::runtime_error("HEALING_LOCALE_ASSETS_MISSING");
        const auto common = wvd::games::recovery::clear_common_screens(false);
        const auto common_ready = common.nodes.at("Ready").at("observation_args").dump();
        if (common_ready.find("harken_floor_move_zh_hant") == std::string::npos ||
            common_ready.find("harken_floor_return_zh_hant") == std::string::npos)
            throw std::runtime_error("HARKEN_COMMON_HANDOFF_MISSING");
        const auto original = std::filesystem::absolute("packs/wvd");
        const auto test_root = std::filesystem::absolute(".local") /
            ("test-worldmap-publication-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        const auto source = test_root / "source";
        const auto published_path = test_root / "published";
        std::filesystem::create_directories(source / "image");
        wvd::recognition::Bundle map_bundle{source, "test-worldmap", {}};
        for (const auto &name : map_workflow.images) {
            const auto relative = std::string("image/") + name;
            std::filesystem::create_directories((source / relative).parent_path());
            std::filesystem::copy_file(original / relative, source / relative);
            map_bundle.files.push_back({relative,
                wvd::platform::file_sha256(source / relative)});
        }
        {
            auto publication = wvd::games::tasks::publish_native(
                map_workflow, map_bundle, published_path, J::object());
            if (publication.program.revision.empty() ||
                !publication.identity.at("image_sources").contains("worldmap_close_zh_hant.png") ||
                !publication.identity.at("image_sources").contains("worldmap_zoom_plus_zh_hant.png"))
                throw std::runtime_error("ZH_WORLDMAP_PUBLICATION_INVALID");
        }
        if (std::filesystem::weakly_canonical(test_root.parent_path()) !=
            std::filesystem::weakly_canonical(std::filesystem::absolute(".local")))
            throw std::runtime_error("ZH_WORLDMAP_TEST_PATH_INVALID");
        std::filesystem::remove_all(test_root);
        auto report = wvd::games::tasks::visit_bounty_board(
            wvd::games::tasks::BountyVisit::Report, library, board, "en");
        auto report_program = wvd::games::tasks::compile_native_program(
            report, J::object(), "bounty-report-en");
        report_program.validate();
        if (!report.nodes.contains("Report"))
            throw std::runtime_error("EN_BOUNTY_REPORT_LOST");
        auto zh_report = wvd::games::tasks::visit_bounty_board(
            wvd::games::tasks::BountyVisit::Report, library, board, "zh-Hant");
        auto zh_report_program = wvd::games::tasks::compile_native_program(
            zh_report, J::object(), "bounty-report-zh-Hant");
        zh_report_program.validate();
        if (!zh_report.nodes.contains("CloseReceipt") ||
            !zh_report.nodes.contains("ReportConfirmed") ||
            !zh_report.nodes.contains("NoMoreReports") ||
            zh_report.nodes.at("Report").at("next").back() != "CloseReceipt" ||
            zh_report.nodes.at("ReportConfirmed").at("next").back() != "ReportLoop")
            throw std::runtime_error("ZH_BOUNTY_REPORT_RECEIPT_FLOW_MISSING");
        const auto inn = wvd::games::supply::rest_at_inn(false, true);
        const auto inn_program = wvd::games::tasks::compile_native_program(inn, J::object(), "inn-zh-Hant");
        inn_program.validate();
        if (!inn.nodes.contains("ConfirmZh") || !inn.nodes.contains("Confirm") ||
            inn.nodes.at("ConfirmZh").at("operation_args").at("target_recognition")
                .at("parameters").at("image") != "inn_confirm_zh_hant" ||
            inn.nodes.at("PreparePayment").at("next").at(0) != "SelectConfirm")
            throw std::runtime_error("ZH_INN_PAYMENT_CONFIRM_MISSING");
        std::cout << "native author slot six and independent calls passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
