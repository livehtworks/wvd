#include "quest_catalog.hpp"
#include <algorithm>

namespace wvd::games {
WvdQuestCatalog::WvdQuestCatalog(const nlohmann::ordered_json &source) {
    if (!source.is_object())
        throw std::runtime_error("QUEST_CATALOG_TYPE");
    for (const auto &[id, value] : source.items()) {
        if (id.empty() || !value.is_object() || !value.contains("_TYPE") ||
            !value.at("_TYPE").is_string())
            throw std::runtime_error("QUEST_DEFINITION_INVALID");
        auto type = value.at("_TYPE").get<std::string>();
        if (type != "dungeon" && type != "quest")
            throw std::runtime_error("QUEST_TYPE_INVALID");
        for (const auto *field : {"_TARGETINFOLIST", "_EOT", "_RTT"})
            if (value.contains(field) && !value.at(field).is_null() && !value.at(field).is_array())
                throw std::runtime_error("QUEST_SEQUENCE_TYPE");
        tasks_.push_back({id, type, value});
    }
}
const WvdQuestDefinition &WvdQuestCatalog::at(const std::string &id) const {
    auto it =
        std::find_if(tasks_.begin(), tasks_.end(), [&](const auto &task) { return task.id == id; });
    if (it == tasks_.end())
        throw std::runtime_error("QUEST_NOT_FOUND");
    return *it;
}
nlohmann::json WvdQuestCatalog::export_source() const {
    nlohmann::json source = nlohmann::json::object();
    for (const auto &task : tasks_)
        source[task.id] = task.source;
    return source;
}
} // namespace wvd::games
