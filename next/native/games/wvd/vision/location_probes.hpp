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
        for (const char *name : {"city.royal.identity", "city.inn.entry", "city.temple.entry",
            "city.blacksmith.entry", "city.ruins.entry", "city.guild.entry",
            "city.ore_merchant.entry", "city.item_shop.entry", "city.edge.entry", "city.any"})
            values.emplace(name, assets.condition(name, ""));
        for (const char *name : {"guild.commissions.page", "guild.bounties.page",
            "chest.open.option", "chest.choose.page"})
            values.emplace(std::string(name) + "|zh-Hant", assets.condition(name, "zh-Hant"));
        for (const char *name : {"chest.open.option", "chest.choose.page"})
            values.emplace(std::string(name) + "|en", assets.condition(name, "en"));
        values.emplace("chest.reward.page", assets.condition("chest.reward.page", ""));
        return values;
    }();
    return recipes.at(std::string(id) + (locale[0] ? std::string("|") + locale : ""));
}
inline nlohmann::json royal_city() { return resource("city.royal.identity"); }
inline nlohmann::json inn_button() { return resource("city.inn.entry"); }
inline nlohmann::json temple_button() { return resource("city.temple.entry"); }
inline nlohmann::json blacksmith_button() { return resource("city.blacksmith.entry"); }
inline nlohmann::json ruins_button() { return resource("city.ruins.entry"); }
inline nlohmann::json guild_button() { return resource("city.guild.entry"); }
inline nlohmann::json ore_merchant_button() { return resource("city.ore_merchant.entry"); }
inline nlohmann::json item_shop_button() { return resource("city.item_shop.entry"); }
inline nlohmann::json edge_of_town_button() { return resource("city.edge.entry"); }
inline nlohmann::json city_screen() { return resource("city.any"); }
}
