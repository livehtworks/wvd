#include "locale_assets.hpp"
#include "authoring/semantic_assets.hpp"
#include "games/wvd/vision/template_language.hpp"
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
    // 要塞的旧文件名指的是区域序号，不是建筑楼层；第十区实际为要塞3F。
    {"impregnableFortress", "outskirts.fortress"},
    {"fortressb1f", "outskirts.fortress.zone1"},
    {"fortressb3f", "outskirts.fortress.zone3"},
    {"fortressb7f", "outskirts.fortress.zone7"},
    {"fortressb10f", "outskirts.fortress.zone10"},
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
    {"openworldmap", "worldmap.open.option"},
    {"returnText", "outskirts.submenu.back"},
    {"Stay.png", "inn.stay.option"},
    {"close", "ui.close.cross"}, {"combatClose", "ui.close.cross"},
    {"cursedWheel", "wheel.entry"}, {"cursedWheelTitle", "wheel.title"},
    {"leap", "wheel.leap"}, {"cursedWheel_timeLeap", "wheel.leap"},
    {"Triumph", "wheel.target.triumph"}, {"BeautifulOre", "wheel.target.ore"},
    {"GhostsOfYore", "wheel.target.ghosts"},
    {"cursedwheel_dhi", "wheel.chapter.dhi"},
    {"cursedwheel_impregnableFortress", "wheel.chapter.fortress"},
    {"TradeWaterway", "wheel.chapter.waterway"},
    {"trait", "character.panel"},
    {"recover", "dungeon.recovery.panel"},
    {"combat_skill_detail", "combat.skill.detail"},
    {"spellskill/skillDetail", "combat.skill.detail"},
    {"combat_skill_confirm", "combat.skill.confirm"},
    {"RiseAgain", "party.revival.action"},
    {"returntoTown", "outskirts.return.to.town"},
    {"returntotown", "outskirts.return.to.town"},
    {"COS/COS", "outskirts.separation"}, {"COS/COSB2F", "outskirts.separation.b2f"},
};

const std::unordered_map<std::string, std::string> legacy_observations{
    {"theRouteToTheDestinationCannotBeFound", "navigation.no_route"},
    {"notenoughsp", "combat.resource.error"}, {"notenoughmp", "combat.resource.error"},
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
    if (node.contains("locale_only") && node.at("locale_only") != locale) return;
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
        if (image == "RiseAgain" && use == authoring::ResourceUse::Observation)
            id = "party.revival.page";
        if (!id.empty()) {
            auto resolved = assets.condition(id, locale, use);
            if (resolved.value("mode", "") == "ocr" || resolved.value("mode", "") == "bright_mask") {
                // 模板阈值和预处理不能跨算法沿用；只有无覆盖的旧引用使用素材默认配方。
                if (!(node.size() == 2 || (node.size() == 3 && node.value("threshold", 0.0) == 0.8)))
                    throw std::runtime_error("NATIVE_LEGACY_METHOD_OVERRIDE_UNSUPPORTED:" + image);
                node = std::move(resolved);
                return;
            }
            if (resolved.value("mode", "") != "template") {
                // 繁中世界地图是关闭与缩放的联合只读证据；定位用途和显式
                // 单模板覆盖都不能广播到两张独立叶子图。
                const bool default_threshold = node.size() == 3 &&
                    node.contains("threshold") && node.at("threshold") == 0.8;
                const bool revival = image == "RiseAgain" && resolved.value("mode", "") == "revival_prompt";
                const bool resource_error = (image == "notenoughsp" || image == "notenoughmp") &&
                    resolved.value("mode", "") == "combat_resource_error";
                const bool worldmap = image == "worldmapflag" && resolved.value("mode", "") == "all";
                if ((!worldmap && !revival && !resource_error) || use != authoring::ResourceUse::Observation ||
                    !(node.size() == 2 || default_threshold))
                    throw std::runtime_error("NATIVE_LEGACY_COMPOSITE_OVERRIDE_UNSUPPORTED:" + image);
                node = std::move(resolved);
                return;
            }
            for (const auto &[key, value] : node.items()) {
                if (key == "mode" || key == "image") continue;
                if ((image == "close" || image == "combatClose") && key == "crop") {
                    if (value != J::array({18, 12, 40, 40}))
                        throw std::runtime_error("NATIVE_LEGACY_CLOSE_CROP_UNSUPPORTED");
                    continue;
                }
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

bool random_maze_probe(const J &condition) {
    if (!condition.is_object()) return false;
    const auto image = condition.value("image", "");
    return condition.value("mode", "") == "default_dialogue" || image == "ambush" ||
        image == "ignore" || image == "sandman_recover" || image.starts_with("dialogueChoices/");
}

J localize_implicit_probe(const J &condition, const std::string &locale) {
    if (locale != "zh-Hant" || condition.value("mode", "") != "template" ||
        !condition.contains("image") || (condition.contains("locale_only") && condition.at("locale_only") != locale))
        return condition;
    static const auto recipes = [] {
        authoring::SemanticAssets assets(J::parse(wvd_semantic_catalogue));
        std::unordered_map<std::string, J> result;
        for (const auto &[image, id] : legacy_assets) result[image] = assets.condition(id, "zh-Hant");
        for (const auto &[image, id] : legacy_observations) result[image] = assets.condition(id, "zh-Hant");
        result["RiseAgain"] = assets.condition("party.revival.page", "zh-Hant");
        result["whowillopenit"] = assets.condition("chest.choose.page", "zh-Hant");
        return result;
    }();
    const auto image = condition.at("image").get<std::string>();
    const auto found = recipes.find(image);
    if (found == recipes.end()) return condition;
    auto resolved = found->second;
    if (resolved.value("mode", "") != "template") return resolved;
    for (const auto &[key, value] : condition.items()) {
        if (key == "mode" || key == "image" || key == "locale_only") continue;
        if ((image == "close" || image == "combatClose") && key == "crop") {
            if (value != J::array({18, 12, 40, 40}))
                throw std::runtime_error("NATIVE_LEGACY_CLOSE_CROP_UNSUPPORTED");
            continue;
        }
        if (key == "threshold" && value == 0.8) continue;
        resolved[key] = value;
    }
    return resolved;
}

void localize_task_assets(CompiledWorkflow &workflow, const J &catalogue,
                          const std::string &locale) {
    authoring::validate_resource_locale(locale);
    workflow.authoring["resource_locale"] = locale;
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

J locale_asset_coverage(const CompiledWorkflow &workflow, const std::string &locale) {
    authoring::validate_resource_locale(locale);
    J missing = J::array(), foreign = J::array(), ocr = J::array();
    const auto &index = vision::template_language_index();
    std::set<std::string> seen;
    const auto provided = workflow.authoring.value("provided_portrait_images", J::array());
    for (auto name : workflow.images) {
        // These PNGs were decoded, dimension-checked and hash-bound by the
        // profile's portrait provider, not inferred from a filename prefix.
        if (std::find(provided.begin(), provided.end(), J(name)) != provided.end()) continue;
        if (name.ends_with(".png")) name.resize(name.size() - 4);
        if (!seen.insert(name).second) continue;
        const auto found = index.find(name);
        if (found == index.end()) missing.push_back(name);
        else if (!found->second.contains("shared") && !found->second.contains(locale))
            foreign.push_back(name);
    }
    const auto inspect = [&](auto &&self, const J &value, const std::string &path) -> void {
        if (value.is_object()) {
            if (value.contains("locale_only") && value.at("locale_only") != locale) return;
            if (value.value("mode", std::string{}) == "ocr" && value.value("language", std::string{}) != locale)
                ocr.push_back({{"path", path}, {"language", value.value("language", std::string{})}});
            for (const auto &[key, child] : value.items()) self(self, child, path + "/" + key);
        } else if (value.is_array()) {
            for (std::size_t i = 0; i < value.size(); ++i) self(self, value[i], path + "/" + std::to_string(i));
        }
    };
    inspect(inspect, workflow.nodes, "nodes");
    inspect(inspect, workflow.event_scopes, "events");
    return {{"locale", locale}, {"complete", !locale.empty() && missing.empty() && foreign.empty() && ocr.empty()},
        {"unclassified", missing}, {"unresolved_foreign", foreign}, {"foreign_ocr", ocr}};
}

void require_locale_asset_coverage(const CompiledWorkflow &workflow, const std::string &locale) {
    authoring::validate_resource_locale(locale);
    // This admission rule closes the requested Chinese migration. English
    // execution keeps its existing resource checks rather than losing features.
    if (locale != "zh-Hant") return;
    const auto coverage = locale_asset_coverage(workflow, locale);
    if (!coverage.at("complete").get<bool>())
        throw std::runtime_error("LOCALE_ASSET_COVERAGE_INCOMPLETE:" + coverage.dump());
}
} // namespace wvd::games::tasks
