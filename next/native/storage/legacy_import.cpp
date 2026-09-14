#include "legacy_import.hpp"
#include "platform/windows/file_digest.hpp"
#include "platform/windows/runtime_files.hpp"
#include <fstream>
#include <algorithm>
#include <set>

namespace wvd::storage {
using J = nlohmann::json;
namespace {
void require(bool ok, const char *code) {
    if (!ok)
        throw std::runtime_error(code);
}
std::string pointer_token(const std::string &key) {
    auto p = J::json_pointer{};
    p /= key;
    return p.to_string().substr(1);
}
std::string read(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    require(bool(input), "LEGACY_SOURCE_UNREADABLE");
    std::string result((std::istreambuf_iterator<char>(input)), {});
    require(result.size() <= 16 * 1024 * 1024, "LEGACY_SOURCE_TOO_LARGE");
    return result;
}
void validate(const J &value, const J &field) {
    const auto type = field.at("type").get<std::string>();
    bool valid = value.is_null() && field.at("default").is_null();
    valid = valid || (type == "string" && value.is_string()) ||
            (type == "boolean" && value.is_boolean()) ||
            (type == "integer" && value.is_number_integer()) ||
            (type == "array" && value.is_array()) || (type == "object" && value.is_object());
    require(valid, "PROFILE_FIELD_TYPE");
}
} // namespace
J parse_legacy_json(const std::string &text) {
    std::vector<std::set<std::string>> objects;
    return J::parse(text, [&](int, J::parse_event_t event, J &value) {
        if (event == J::parse_event_t::object_start)
            objects.emplace_back();
        else if (event == J::parse_event_t::object_end)
            objects.pop_back();
        else if (event == J::parse_event_t::key)
            require(objects.back().insert(value.get<std::string>()).second, "LEGACY_DUPLICATE_KEY");
        return true;
    });
}
LegacyConfigImporter::LegacyConfigImporter(J descriptor) : descriptor_(std::move(descriptor)) {
    require(descriptor_.at("schema") == 1 && descriptor_.at("fields").is_array() &&
                descriptor_.at("fields").size() == 33,
            "LEGACY_DESCRIPTOR_INVALID");
    std::set<std::string> fields;
    for (const auto &field : descriptor_.at("fields"))
        require(fields.insert(field.at("name")).second, "LEGACY_DESCRIPTOR_DUPLICATE");
}
games::WvdProfile LegacyConfigImporter::parse(const J &source) const {
    require(source.is_object(), "LEGACY_CONFIG_TYPE");
    J general = source.value("GENERAL", J::object());
    require(general.is_object(), "LEGACY_SECTION_TYPE");
    if (general.contains("TASK_SPECIFIC_CONFIG"))
        require(general.at("TASK_SPECIFIC_CONFIG").is_boolean(), "PROFILE_FIELD_TYPE");
    if (general.contains("FARM_TARGET"))
        require(general.at("FARM_TARGET").is_null() || general.at("FARM_TARGET").is_string(),
                "PROFILE_FIELD_TYPE");
    const std::string task = general.value("FARM_TARGET", J(nullptr)).is_string()
                                 ? general.at("FARM_TARGET").get<std::string>()
                                 : "";
    const auto section =
        general.value("TASK_SPECIFIC_CONFIG", false) && !task.empty() && source.contains(task)
            ? task
            : std::string("DEFAULT");
    auto selected = source.value(section, J::object());
    require(selected.is_object(), "LEGACY_SECTION_TYPE");
    J merged = general;
    merged.update(selected);
    games::WvdProfile profile{J::object(), source, J::object(), J::object(), section};
    std::set<std::string> known;
    for (const auto &field : descriptor_.at("fields")) {
        auto name = field.at("name").get<std::string>();
        known.insert(name);
        auto value = merged.value(name, field.at("default"));
        validate(value, field);
        profile.values[name] = value;
        profile.sources[name] = selected.contains(name)  ? section
                                : general.contains(name) ? "GENERAL"
                                                         : "VERIFIED_DEFAULT";
    }
    games::validate_strategy(profile.values.at("STRATEGY"));
    auto points = profile.values.at("TASK_POINT_STRATEGY");
    if (points.contains("overall_strategy"))
        require(points.at("overall_strategy").is_string(), "PROFILE_TASK_POINT_TYPE");
    if (points.contains("task_point")) {
        require(points.at("task_point").is_object(), "PROFILE_TASK_POINT_TYPE");
        for (const auto &[step, strategy] : points.at("task_point").items()) {
            require(!step.empty() &&
                        std::all_of(step.begin(), step.end(),
                                    [](char c) { return c >= '0' && c <= '9'; }) &&
                        strategy.is_string(),
                    "PROFILE_TASK_POINT_TYPE");
        }
    }
    // 原语义树完整保留；额外按 JSON Pointer 分类未知值，后续不能把它们悄悄当成业务参数。
    for (const auto &[section_name, contents] : source.items()) {
        if (!contents.is_object()) {
            profile.legacy_passthrough["/" + pointer_token(section_name)] = contents;
            continue;
        }
        for (const auto &[key, value] : contents.items()) {
            auto pointer = "/" + pointer_token(section_name) + "/" + pointer_token(key);
            if (!known.contains(key))
                profile.legacy_passthrough[pointer] = value;
            if (key == "TASK_POINT_STRATEGY" && value.is_object())
                for (const auto &[k, v] : value.items())
                    if (k != "overall_strategy" && k != "task_point")
                        profile.legacy_passthrough[pointer + "/" + pointer_token(k)] = v;
            if (key == "STRATEGY" && value.is_array())
                for (std::size_t i = 0; i < value.size(); ++i) {
                    const auto &group = value[i];
                    if (!group.is_object())
                        continue;
                    for (const auto &[k, v] : group.items())
                        if (k != "group_name" && k != "skill_settings" &&
                            k != "complete_one_as_all")
                            profile.legacy_passthrough[pointer + "/" + std::to_string(i) + "/" +
                                                       pointer_token(k)] = v;
                    if (group.contains("skill_settings") && group.at("skill_settings").is_array()) {
                        const auto &rows = group.at("skill_settings");
                        for (std::size_t j = 0; j < rows.size(); ++j)
                            if (rows[j].is_object())
                                for (const auto &[k, v] : rows[j].items())
                                    if (k != "role_var" && k != "skill_var" && k != "target_var" &&
                                        k != "freq_var" && k != "skill_lvl")
                                        profile
                                            .legacy_passthrough[pointer + "/" + std::to_string(i) +
                                                                "/skill_settings/" +
                                                                std::to_string(j) + "/" +
                                                                pointer_token(k)] = v;
                    }
                }
        }
    }
    return profile;
}
games::WvdProfile
LegacyConfigImporter::import_copy(const std::filesystem::path &source,
                                  const std::filesystem::path &destination) const {
    require(!std::filesystem::exists(destination), "LEGACY_COPY_DESTINATION_EXISTS");
    const auto before = platform::file_sha256(source);
    auto text = read(source);
    require(platform::bytes_sha256(
                {reinterpret_cast<const std::uint8_t *>(text.data()), text.size()}) == before,
            "LEGACY_SOURCE_CHANGED");
    std::filesystem::create_directories(destination);
    auto snapshot = destination / "legacy-config.json";
    platform::atomic_write(snapshot, text, false);
    auto profile = parse(parse_legacy_json(read(snapshot)));
    require(platform::file_sha256(source) == before, "LEGACY_SOURCE_CHANGED");
    profile.sources["source_sha256"] = before;
    profile.sources["snapshot"] = "legacy-config.json";
    return profile;
}
J LegacyConfigImporter::export_legacy(const games::WvdProfile &profile) const {
    auto original = parse(profile.legacy_document);
    auto result = profile.legacy_document;
    for (const auto &field : descriptor_.at("fields")) {
        auto key = field.at("name").get<std::string>();
        const auto &value = profile.values.at(key);
        validate(value, field);
        if (value == original.values.at(key))
            continue;
        auto section = original.sources.at(key).get<std::string>();
        if (section == "VERIFIED_DEFAULT")
            section = field.at("category") == "GENERAL" ? "GENERAL" : profile.selected_section;
        result[section][key] = value;
    }
    return result;
}
J merge_legacy_quests(J baseline, const nlohmann::ordered_json &mod, J &diagnostics) {
    require(baseline.is_object() && mod.is_object(), "LEGACY_QUEST_TYPE");
    for (const auto &[key, raw] : mod.items()) {
        J info = raw;
        if (!info.is_object() || !info.contains("_TYPE") ||
            (info["_TYPE"] != "dungeon" && info["_TYPE"] != "quest") ||
            (!info.contains("questName") && !info.contains("questName_en_US"))) {
            diagnostics.push_back({{"task_id", key}, {"error", "INVALID_MOD_TASK_SKIPPED"}});
            continue;
        }
        if (!info.contains("questName"))
            info["questName"] = info.at("questName_en_US");
        if (!info.contains("questName_en_US"))
            info["questName_en_US"] = info.at("questName");
        require(info.at("questName").is_string() && info.at("questName_en_US").is_string(),
                "MOD_TASK_NAME_TYPE");
        info["questCategory"] = "自定义";
        info["questCategory_en_US"] = "Custom Requests";
        auto final = key;
        while (baseline.contains(final)) {
            final += "_mod";
            info["questName"] = info.at("questName").get<std::string>() + "_自定义";
            info["questName_en_US"] = info.at("questName_en_US").get<std::string>() + "_mod";
        }
        baseline[final] = std::move(info);
    }
    return baseline;
}
} // namespace wvd::storage
