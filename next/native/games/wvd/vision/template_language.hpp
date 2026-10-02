#pragma once
#include "semantic_catalogue.hpp"
#include <json.hpp>
#include <map>
#include <set>
#include <string>

namespace wvd::games::vision {
// 语言属于素材内容，不属于文件名。优先使用语义目录的变体标记；旧模板和
// 跨语言共用的文字/图标由同一目录显式声明，不根据英文文件名或后缀猜测。
inline bool template_language_enabled(const std::string &image, const std::string &locale) {
    using J = nlohmann::json;
    static const auto languages = [] {
        const auto catalogue = J::parse(wvd_semantic_catalogue);
        std::map<std::string, std::set<std::string>> index;
        const auto collect = [&](const auto &self, const J &node, const std::string &language) -> void {
            if (node.is_array()) {
                for (const auto &child : node) self(self, child, language);
            } else if (node.is_object()) {
                if (node.contains("image") && node.at("image").is_string())
                    index[node.at("image").get<std::string>()].insert(language);
                for (const auto &[key, child] : node.items()) self(self, child, language);
            }
        };
        for (const auto &[id, entry] : catalogue.at("resources").items()) {
            if (!entry.contains("variants")) continue;
            for (const auto &[language, variant] : entry.at("variants").items())
                collect(collect, variant.at("condition"), language);
        }
        // 显式内容分类覆盖语义用途分类，例如繁中界面的Auto、纯宝石图标也能跨语言使用。
        for (const auto &[language, images] : catalogue.at("template_languages").items())
            for (const auto &name : images) index[name.get<std::string>()] = {language};
        return index;
    }();
    // 未选择语言沿用既有作者探针语义；正式任务使用冻结的resource_locale。
    if (locale.empty()) return true;
    // 与AssetResolver一致：Stay和Stay.png是同一素材，不能借扩展名绕过语言选择。
    auto name = image;
    if (name.ends_with(".png")) name.resize(name.size() - 4);
    const auto found = languages.find(name);
    return found == languages.end() || found->second.contains("shared") || found->second.contains(locale);
}
} // namespace wvd::games::vision
