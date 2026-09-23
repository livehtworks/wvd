#pragma once
#include "document_parameters.hpp"

namespace wvd::authoring {
enum class ResourceUse { Observation, Position };

// 编辑/编译期选择语义资源；不截图、不调用 OCR、不在运行中扫描文件目录。
// locale 是游戏素材语言，不复用工作台 LANGUAGE。shared 仅表示同一图片不含语言文本。
class SemanticAssets {
  public:
    explicit SemanticAssets(Json catalogue = Json::object()) : catalogue_(std::move(catalogue)) {
        if (!catalogue_.is_object()) contract_error("SEMANTIC_CATALOG_INVALID");
        if (!catalogue_.empty() && (!catalogue_.is_object() || catalogue_.value("schema", 0) != 1 ||
                                   !catalogue_.contains("resources") || !catalogue_.at("resources").is_object()))
            contract_error("SEMANTIC_CATALOG_INVALID");
    }
    Json condition(const std::string &id, const std::string &locale,
                   ResourceUse use = ResourceUse::Observation) {
        check_locale(locale);
        std::set<std::string> active;
        return resolve_id(id, locale, use, active);
    }
    Json lower(Json document, const std::string &locale) {
        check_locale(locale);
        for (auto &node : document.at("nodes")) {
            auto &p = node.at("parameters");
            const auto type = node.at("type").get<std::string>();
            if (type == "recognition") p["condition"] = lower_condition(p.at("condition"), locale);
            else if (type == "action") {
                p["scene"] = lower_condition(p.at("scene"), locale);
                p["postcondition"] = lower_condition(p.at("postcondition"), locale);
                if (p.value("operation", "") == "click")
                    p["target"] = lower_condition(p.at("target"), locale, ResourceUse::Position);
            } else if (type == "business" && p.value("binding", "") == "confirm")
                p["condition"] = lower_condition(p.at("condition"), locale);
        }
        return document;
    }
    Json resolve(const Json &condition, const std::string &locale,
                 ResourceUse use = ResourceUse::Observation) { return lower_condition(condition, locale, use); }
    Json selections() const { return selections_; }
    const Json &catalogue() const { return catalogue_; }
  private:
    Json catalogue_, selections_ = Json::object();
    static void check_locale(const std::string &locale) {
        if (locale != "" && locale != "en" && locale != "zh-Hant" && locale != "zh-Hans" && locale != "ja")
            contract_error("AUTHOR_RESOURCE_LOCALE_INVALID", locale);
    }
    Json lower_condition(const Json &p, const std::string &locale,
                         ResourceUse use = ResourceUse::Observation) {
        std::set<std::string> active;
        check_locale(locale);
        return resolve_tree(p, locale, use, active);
    }
    Json resolve_tree(const Json &p, const std::string &locale, ResourceUse use,
                      std::set<std::string> &active) {
        if (!p.is_object()) contract_error("SEMANTIC_CONDITION_INVALID");
        const auto mode = p.value("mode", std::string{});
        if (mode == "semantic") {
            if (p.size() != 2 || !p.contains("id")) contract_error("SEMANTIC_REFERENCE_INVALID");
            return resolve_id(p.at("id").get<std::string>(), locale, use, active);
        }
        if (mode == "location") {
            if (p.size() != 2 || !p.contains("id")) contract_error("LOCATION_REFERENCE_INVALID");
            const auto value = scalar(p.at("id"));
            if (!std::holds_alternative<std::int64_t>(value) || std::get<std::int64_t>(value) < 0)
                contract_error("LOCATION_ID_INVALID");
            const auto key = std::to_string(std::get<std::int64_t>(value));
            if (use != ResourceUse::Observation) contract_error("LOCATION_IS_NOT_CLICK_TARGET", key);
            if (!catalogue_.contains("locations") || !catalogue_.at("locations").contains(key))
                contract_error("LOCATION_NOT_REGISTERED", key);
            return resolve_id(catalogue_.at("locations").at(key).at("resource").get<std::string>(),
                              locale, use, active);
        }
        if (mode == "all" || mode == "any" || mode == "not") {
            if (use == ResourceUse::Position) contract_error("SEMANTIC_COMPOSITE_NOT_POSITIONAL");
            auto result = p;
            if (!p.contains("conditions") || !p.at("conditions").is_array())
                contract_error("SEMANTIC_CONDITION_INVALID");
            for (auto &child : result["conditions"]) child = resolve_tree(child, locale, use, active);
            return result;
        }
        return p; // 旧模板/OCR/专用识别原样交给现有作者模型和 PipelineCompiler 校验。
    }
    Json resolve_id(const std::string &id, const std::string &locale, ResourceUse use,
                    std::set<std::string> &active) {
        if (!public_id(id)) contract_error("SEMANTIC_ID_INVALID", id);
        if (active.size() >= 8 || !active.insert(id).second) contract_error("SEMANTIC_REFERENCE_CYCLE", id);
        struct Pop { std::set<std::string> &s; std::string id; ~Pop() { s.erase(id); } } pop{active, id};
        if (catalogue_.empty() || !catalogue_.at("resources").contains(id))
            contract_error("SEMANTIC_RESOURCE_MISSING", id);
        const auto &entry = catalogue_.at("resources").at(id);
        const auto role = entry.at("role").get<std::string>();
        if (role != "observation" && role != "position") contract_error("SEMANTIC_ROLE_INVALID", id);
        if (use == ResourceUse::Position && role != "position")
            contract_error("SEMANTIC_OBSERVATION_NOT_POSITIONAL", id);
        if (entry.contains("condition")) {
            if (entry.contains("variants") || role != "observation") contract_error("SEMANTIC_DEFINITION_INVALID", id);
            auto result = resolve_tree(entry.at("condition"), locale, use, active);
            selections_[id] = {{"role", role}, {"condition", result}};
            return result;
        }
        if (!entry.contains("variants") || !entry.at("variants").is_object())
            contract_error("SEMANTIC_VARIANTS_INVALID", id);
        const auto &variants = entry.at("variants");
        const auto selected = variants.contains(locale) ? locale : variants.contains("shared") ? "shared" : "";
        if (selected.empty()) contract_error("SEMANTIC_LOCALE_UNAVAILABLE", id + ":" + locale);
        const auto &variant = variants.at(selected);
        if (!variant.contains("condition")) contract_error("SEMANTIC_VARIANT_INVALID", id);
        auto result = resolve_tree(variant.at("condition"), locale, use, active);
        const auto algorithm = result.value("mode", std::string{});
        if (use == ResourceUse::Position && algorithm != "template" && algorithm != "bright_mask")
            contract_error("SEMANTIC_POSITION_RECIPE_INVALID", id);
        selections_[id] = {{"locale", selected}, {"role", role}, {"condition", result},
                           {"validation", variant.value("validation", "UNVERIFIED")}};
        return result;
    }
};
} // namespace wvd::authoring
