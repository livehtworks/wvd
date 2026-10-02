#pragma once
#include "resource_locale.hpp"
#include "recognition/request.hpp"

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
        if (document.at("execution").contains("events")) {
            for (auto &[id, rule] : document["execution"]["events"].items()) {
                (void)id;
                if (rule.value("enabled", false)) {
                    try {
                        auto detect = lower_condition(rule.at("detect"), locale);
                        rule["detect"] = std::move(detect);
                    } catch (const ContractError &error) {
                        contract_error("EVENT_DETECT_UNAVAILABLE", id + ":" + locale + ":detect:" + error.what());
                    }
                    if (rule.contains("resume") && rule.at("resume").value("mode", "") == "replan") {
                        try {
                            auto guard = lower_condition(rule.at("resume").at("guard"), locale);
                            rule["resume"]["guard"] = std::move(guard);
                        } catch (const ContractError &error) {
                            contract_error("EVENT_GUARD_UNAVAILABLE", id + ":" + locale + ":guard:" + error.what());
                        }
                    }
                }
            }
        }
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
            if (node.contains("resume")) {
                for (auto &[id, resume] : node["resume"].items()) {
                    (void)id;
                    if (resume.value("mode", "") == "replan") {
                        try {
                            auto guard = lower_condition(resume.at("guard"), locale);
                            resume["guard"] = std::move(guard);
                        } catch (const ContractError &error) {
                            contract_error("EVENT_GUARD_UNAVAILABLE", id + ":" + locale + ":guard:" + error.what());
                        }
                    }
                }
            }
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
        validate_resource_locale(locale);
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
            if (!p.contains("id") || p.size() != (p.contains("method") ? 3 : 2))
                contract_error("SEMANTIC_REFERENCE_INVALID");
            return resolve_id(p.at("id").get<std::string>(), locale, use, active,
                              p.value("method", std::string{}));
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
        if (mode == "ocr")
            recognition::validate_ocr_parameters(recognition::parse_ocr_parameters(p),
                                                 use == ResourceUse::Position);
        return p;
    }
    Json resolve_id(const std::string &id, const std::string &locale, ResourceUse use,
                    std::set<std::string> &active, const std::string &method = {}) {
        if (!public_id(id)) contract_error("SEMANTIC_ID_INVALID", id);
        if (active.size() >= 8 || !active.insert(id).second) contract_error("SEMANTIC_REFERENCE_CYCLE", id);
        struct Pop { std::set<std::string> &s; std::string id; ~Pop() { s.erase(id); } } pop{active, id};
        if (catalogue_.empty() || !catalogue_.at("resources").contains(id))
            contract_error("SEMANTIC_RESOURCE_MISSING", id);
        const auto &entry = catalogue_.at("resources").at(id);
        if (entry.value("validation", std::string{}) == "MISSING_REAL_RECIPE")
            contract_error("SEMANTIC_RECIPE_MISSING", id + ":" + locale);
        const auto role = entry.at("role").get<std::string>();
        if (role != "observation" && role != "position") contract_error("SEMANTIC_ROLE_INVALID", id);
        if (use == ResourceUse::Position && role != "position")
            contract_error("SEMANTIC_OBSERVATION_NOT_POSITIONAL", id);
        if (entry.contains("condition")) {
            if (!method.empty()) contract_error("SEMANTIC_METHOD_UNAVAILABLE", id + ":" + method);
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
        auto recipe = variant.at("condition");
        // 显式选择只分派到素材声明的配方；失败不改算法、不改语言、不降阈值。
        if (!method.empty() && recipe.value("mode", "") != method) {
            if (!variant.contains("alternatives") || !variant.at("alternatives").contains(method))
                contract_error("SEMANTIC_METHOD_UNAVAILABLE", id + ":" + method);
            recipe = variant.at("alternatives").at(method);
            if (recipe.value("mode", "") != method)
                contract_error("SEMANTIC_METHOD_INVALID", id + ":" + method);
        }
        auto result = resolve_tree(recipe, locale, use, active);
        const auto algorithm = result.value("mode", std::string{});
        if (use == ResourceUse::Position && algorithm != "template" && algorithm != "bright_mask" && algorithm != "ocr")
            contract_error("SEMANTIC_POSITION_RECIPE_INVALID", id);
        selections_[method.empty() ? id : id + "@" + method] = {{"locale", selected}, {"role", role}, {"condition", result},
                           {"validation", variant.value("validation", "UNVERIFIED")}};
        return result;
    }
};
} // namespace wvd::authoring
