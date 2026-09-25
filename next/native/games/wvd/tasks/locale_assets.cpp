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
    // 旧图名在轮盘、楼层菜单和地图标题之间复用；任务计划先选专用别名。
    {"outskirts_abyss_zh_hant", "outskirts.abyss"},
    {"outskirts_abyss_b2f_zh_hant", "outskirts.abyss.b2f"},
    {"outskirts_abyss_b4f_zh_hant", "outskirts.abyss.b4f"},
    {"outskirts_abyss_b5f_zh_hant", "outskirts.abyss.b5f"},
    {"map_abyss_b2f_zh_hant", "map.abyss.b2f.title"},
    {"AutoMove", "map.auto_move"},
    {"mapFlag", "dungeon.map.open"},
    {"trait", "character.panel"},
    {"recover", "dungeon.recovery.panel"},
    {"combat_skill_detail", "combat.skill.detail"},
    {"combat_skill_confirm", "combat.skill.confirm"},
    {"returntoTown", "outskirts.return.to.town"},
    {"returntotown", "outskirts.return.to.town"},
    {"COS/COS", "outskirts.separation"}, {"COS/COSB2F", "outskirts.separation.b2f"},
};

const std::unordered_map<std::string, std::string> legacy_observations{
    {"worldmapflag", "worldmap.open"},
    {"flee", "combat.menu.flee"},
    {"spellskill/CombatAutoDisable", "combat.auto.off"},
    {"spellskill/CombatAutoEnable", "combat.auto.on"},
    {"combat_speed_off_zh_hant", "combat.speed.off"},
    {"combat_speed_on_zh_hant", "combat.speed.on"},
};

void resolve_combat_assets_en(J &node) {
    if (node.is_array()) {
        for (auto &child : node) resolve_combat_assets_en(child);
        return;
    }
    if (!node.is_object()) return;
    if (node.value("mode", std::string{}) == "template" && node.contains("image")) {
        const auto image = node.at("image").get<std::string>();
        if (image == "combat_skill_detail") node["image"] = "spellskill/skillDetail";
        if (image == "combat_skill_confirm") node["image"] = "OK";
    }
    for (auto &[key, child] : node.items())
        if (key != "image") resolve_combat_assets_en(child);
}

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
            if (resolved.value("mode", "") != "template") {
                // 繁中世界地图是关闭与缩放的联合只读证据；定位用途和显式
                // 单模板覆盖都不能广播到两张独立叶子图。
                const bool default_threshold = node.size() == 3 &&
                    node.contains("threshold") && node.at("threshold") == 0.8;
                if (image != "worldmapflag" || use != authoring::ResourceUse::Observation ||
                    resolved.value("mode", "") != "all" ||
                    !(node.size() == 2 || default_threshold))
                    throw std::runtime_error("NATIVE_LEGACY_COMPOSITE_OVERRIDE_UNSUPPORTED:" + image);
                node = std::move(resolved);
                return;
            }
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
        // 固定坐标输入复用场景条件作二次确认，并不从该条件提取点击坐标。
        const auto child_use = key == "target_recognition" &&
            node.value("use_target_center", true) ? authoring::ResourceUse::Position : use;
        replace_templates(child, assets, locale, child_use);
    }
}
} // namespace

void localize_task_assets(CompiledWorkflow &workflow, const J &catalogue,
                          const std::string &locale) {
    authoring::validate_resource_locale(locale);
    // 战斗专用别名在英文环境仍解析回原图，避免把中文素材带进英文任务。
    if (locale.empty() || locale == "en") {
        resolve_combat_assets_en(workflow.nodes);
        resolve_combat_assets_en(workflow.event_scopes);
        workflow.refresh_images();
        return;
    }
    authoring::SemanticAssets assets(catalogue);
    replace_templates(workflow.nodes, assets, locale);
    replace_templates(workflow.event_scopes, assets, locale);
    workflow.authoring["resource_locale"] = locale;
    workflow.authoring["semantic_selections"] = assets.selections();
    workflow.refresh_images();
}
} // namespace wvd::games::tasks
