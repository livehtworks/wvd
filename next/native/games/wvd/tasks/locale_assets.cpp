#include "locale_assets.hpp"
#include "authoring/semantic_assets.hpp"
#include <stdexcept>
#include <unordered_map>

namespace wvd::games::tasks {
namespace {
using J = nlohmann::json;

// 旧任务数据中的名称与已经采集的繁中选项一一对应。其余素材仍可由作者流程按语义 ID 使用。
const std::unordered_map<std::string, std::string> legacy_assets{
    {"Inn", "city.inn.entry"}, {"guild", "city.guild.entry"},
    {"EdgeOfTown", "city.edge.entry"}, {"ruins", "city.ruins.entry"},
    {"guildRequest", "guild.commissions.entry"},
    {"Bounties", "guild.bounties.entry"},
    {"intoWorldMap", "city.world_map.open"},
    {"chestFlag", "chest.open.option"},
    {"Stay", "inn.stay.option"}, {"Economy", "inn.room.standard"},
    {"royalsuite", "inn.room.royal"},
    {"DOF", "outskirts.fire"}, {"DOFB1F", "outskirts.fire.b1f"},
    {"DOW", "outskirts.wind"}, {"DOWB1F", "outskirts.wind.b1f"},
    {"DOL", "outskirts.light"}, {"DOLB1F", "outskirts.light.b1f"},
    {"DOE", "outskirts.earth"}, {"DOEB1F", "outskirts.earth.b1f"},
    {"DOS", "outskirts.water"}, {"DOSB1F", "outskirts.water.b1f"},
    {"malice_malice", "outskirts.resentment"}, {"malice_B3F", "outskirts.resentment.b3f"},
    {"COS/COS", "outskirts.separation"}, {"COS/COSB2F", "outskirts.separation.b2f"},
};

const std::unordered_map<std::string, std::string> legacy_observations{
    {"worldmapflag", "worldmap.open"},
    {"flee", "combat.menu.flee"},
    {"spellskill/CombatAutoDisable", "combat.auto.off"},
    {"spellskill/CombatAutoEnable", "combat.auto.on"},
};

void replace_templates(J &node, authoring::SemanticAssets &assets, const std::string &locale,
                       authoring::ResourceUse use = authoring::ResourceUse::Observation) {
    if (node.is_array()) {
        for (auto &child : node) replace_templates(child, assets, locale, use);
        return;
    }
    if (!node.is_object()) return;
    if (node.value("mode", std::string{}) == "template" && node.contains("image")) {
        const auto image = node.at("image").get<std::string>();
        std::string id;
        if (image == "whowillopenit") {
            id = "chest.choose.page";
        }
        const auto observation = legacy_observations.find(image);
        if (observation != legacy_observations.end()) id = observation->second;
        const auto found = legacy_assets.find(image);
        if (found != legacy_assets.end()) id = found->second;
        if (!id.empty()) {
            auto resolved = assets.condition(id, locale, use);
            if (resolved.value("mode", "") != "template")
                throw std::runtime_error("NATIVE_LEGACY_COMPOSITE_OVERRIDE_UNSUPPORTED:" + image);
            for (const auto &[key, value] : node.items()) {
                if (key == "mode" || key == "image") continue;
                if (key == "threshold" && value == 0.8 && node.size() == 3) continue;
                if (key != "threshold" && key != "roi" && key != "grayscale" &&
                    key != "preprocess")
                    throw std::runtime_error("NATIVE_LEGACY_OVERRIDE_UNSUPPORTED:" + image + ":" + key);
                resolved[key] = value;
            }
            node = std::move(resolved);
            return;
        }
    }
    for (auto &[key, child] : node.items()) {
        const auto child_use = key == "target_recognition" ? authoring::ResourceUse::Position : use;
        replace_templates(child, assets, locale, child_use);
    }
}
} // namespace

void localize_task_assets(CompiledWorkflow &workflow, const J &catalogue,
                          const std::string &locale) {
    authoring::validate_resource_locale(locale);
    // 未选语言或英文时保留原生任务明确指定的原图；作者显式图片不经此兼容映射。
    if (locale.empty() || locale == "en") return;
    authoring::SemanticAssets assets(catalogue);
    replace_templates(workflow.nodes, assets, locale);
    replace_templates(workflow.event_scopes, assets, locale);
    workflow.authoring["resource_locale"] = locale;
    workflow.authoring["semantic_selections"] = assets.selections();
    workflow.refresh_images();
}
} // namespace wvd::games::tasks
