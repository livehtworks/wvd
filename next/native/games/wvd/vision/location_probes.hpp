#pragma once
#include <json.hpp>
#include "authoring/semantic_assets.hpp"
#include "semantic_catalogue.hpp"
#include <map>
#include <string>

namespace wvd::games::vision {
// C++ 仅声明用途。配方在发布前从语义目录冻结；公共图标不证明具体城市。
inline nlohmann::json resource(const char *id, const char *locale = "") {
    static const auto recipes = [] {
        authoring::SemanticAssets assets(nlohmann::json::parse(wvd_semantic_catalogue));
        std::map<std::string, nlohmann::json> values;
        for (const char *name : {"boot.announcement.page", "city.royal.identity", "city.inn.entry", "city.temple.entry",
            "city.blacksmith.entry", "city.ruins.entry", "city.guild.entry",
            "city.ore_merchant.entry", "city.item_shop.entry", "city.edge.entry", "city.any"})
            values.emplace(name, assets.condition(name, ""));
        for (const char *name : {"guild.commissions.page", "guild.bounties.page", "guild.bounty.reveal.close",
            "chest.open.option", "chest.choose.page", "inn.stay.option", "inn.standard.gold.confirmation", "purchase.premium.button",
            "combat.skill.detail", "combat.skill.confirm", "character.panel", "dungeon.recovery.panel", "dungeon.map.open"})
            values.emplace(std::string(name) + "|zh-Hant", assets.condition(name, "zh-Hant"));
        for (const char *name : {"chest.open.option", "chest.choose.page", "inn.stay.option"})
            values.emplace(std::string(name) + "|en", assets.condition(name, "en"));
        values.emplace("chest.reward.page", assets.condition("chest.reward.page", ""));
        return values;
    }();
    return recipes.at(std::string(id) + (locale[0] ? std::string("|") + locale : ""));
}
inline nlohmann::json royal_city() { return resource("city.royal.identity"); }
inline nlohmann::json inn_button() { return resource("city.inn.entry"); }
inline nlohmann::json inn_menu() {
    return {{"mode", "any"}, {"conditions", {resource("inn.stay.option", "zh-Hant"),
                                             resource("inn.stay.option", "en")}}};
}
// 900x1600旅店菜单的“离开”位于“住宿”锚点下方230像素；只在完整菜单已确认时使用。
// 不靠Android返回键跳过住宿结束动画，也不把房型页的“返回”混作离店。
inline nlohmann::json inn_leave_offset() { return {0, 230}; }
// 重试仍由执行器的间隔、首次提交期限和原页面约束；背景运动不是联网状态。
// 每次重新定位菜单并排除已知覆盖层，不让右下火焰/光效永远阻止补点。
inline nlohmann::json menu_retry_ready(nlohmann::json menu, const char *phase = "navigation") {
    return {{"mode", "all"}, {"conditions", {std::move(menu),
        nlohmann::json{{"mode", "input_clear"}, {"phase", phase}}}}};
}
inline nlohmann::json temple_button() { return resource("city.temple.entry"); }
inline nlohmann::json blacksmith_button() { return resource("city.blacksmith.entry"); }
inline nlohmann::json ruins_button() { return resource("city.ruins.entry"); }
inline nlohmann::json guild_button() { return resource("city.guild.entry"); }
inline nlohmann::json ore_merchant_button() { return resource("city.ore_merchant.entry"); }
inline nlohmann::json item_shop_button() { return resource("city.item_shop.entry"); }
inline nlohmann::json edge_of_town_button() { return resource("city.edge.entry"); }
inline nlohmann::json city_screen() { return resource("city.any"); }
}
