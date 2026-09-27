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
        for (const char *name : {"guild.commissions.page", "guild.bounties.page", "guild.bounty.reveal.close",
            "chest.open.option", "chest.choose.page", "inn.stay.option", "inn.standard.gold.confirmation", "purchase.premium.button"})
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
// 菜单重复点击必须重新确认原菜单、局部阻塞和右下提示区域。
// 提示区域变化时只等待；静止不是“没有网络请求”的证明，故只授权无资源副作用的导航。
inline nlohmann::json menu_retry_ready(nlohmann::json menu, const char *phase = "navigation") {
    return {{"mode", "all"}, {"conditions", {std::move(menu),
        nlohmann::json{{"mode", "input_clear"}, {"phase", phase}},
        nlohmann::json{{"mode", "region_quiet"}, {"roi", {650, 1450, 249, 149}}, {"settle_ms", 1000}}}}};
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
