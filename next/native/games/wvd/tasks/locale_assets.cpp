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

void replace_templates(J &node, authoring::SemanticAssets &assets) {
    if (node.is_array()) {
        for (auto &child : node) replace_templates(child, assets);
        return;
    }
    if (!node.is_object()) return;
    if (node.value("mode", std::string{}) == "template" && node.contains("image")) {
        const auto image = node.at("image").get<std::string>();
        if (image == "whowillopenit") {
            node = assets.condition("chest.choose.page", "zh-Hant", authoring::ResourceUse::Observation);
            return;
        }
        const auto observation = legacy_observations.find(image);
        if (observation != legacy_observations.end()) {
            node = assets.condition(observation->second, "zh-Hant", authoring::ResourceUse::Observation);
            return;
        }
        const auto found = legacy_assets.find(image);
        if (found != legacy_assets.end()) {
            node = assets.condition(found->second, "zh-Hant", authoring::ResourceUse::Position);
            return;
        }
    }
    for (auto &[key, child] : node.items()) {
        (void)key;
        replace_templates(child, assets);
    }
}
} // namespace

void localize_task_assets(CompiledWorkflow &workflow, const J &catalogue,
                          const std::string &locale) {
    if (locale == "en") return;
    if (locale != "zh-Hant") throw std::runtime_error("TASK_RESOURCE_LOCALE_UNSUPPORTED:" + locale);
    authoring::SemanticAssets assets(catalogue);
    replace_templates(workflow.nodes, assets);
    replace_templates(workflow.event_scopes, assets);
    workflow.authoring["resource_locale"] = locale;
    workflow.authoring["semantic_selections"] = assets.selections();
    workflow.refresh_images();
}
} // namespace wvd::games::tasks
