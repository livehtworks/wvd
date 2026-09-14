#include "profile_store.hpp"
#include "legacy_import.hpp"
#include "platform/windows/runtime_files.hpp"
#include "platform/windows/file_digest.hpp"
#include <fstream>
#include <windows.h>

namespace wvd::storage {
using J = nlohmann::json;
namespace {
struct Lock {
    HANDLE file;
    explicit Lock(std::filesystem::path path) {
        path += ".lock";
        file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            throw std::runtime_error("PROFILE_BUSY");
    }
    ~Lock() { CloseHandle(file); }
};
std::string revision(J value) {
    value.erase("revision");
    auto text = value.dump();
    return platform::bytes_sha256(
        {reinterpret_cast<const std::uint8_t *>(text.data()), text.size()});
}
void validate(const J &document) {
    if (document.at("schema") != 1 || !document.at("values").is_object() ||
        document.at("values").size() != 33 || !document.at("legacy_document").is_object() ||
        !document.at("legacy_passthrough").is_object() || !document.at("sources").is_object() ||
        !document.at("selected_section").is_string())
        throw std::runtime_error("PROFILE_SCHEMA_INVALID");
    games::validate_strategy(document.at("values").at("STRATEGY"));
}
} // namespace
ProfileStore::ProfileStore(std::filesystem::path path, J descriptor)
    : path_(std::move(path)), importer_(std::move(descriptor)) {
    if (!path_.is_absolute() || path_.filename() == "config.json")
        throw std::runtime_error("PROFILE_PATH_INVALID");
}
J ProfileStore::create(const games::WvdProfile &profile) {
    J value{{"schema", 1},
            {"values", profile.values},
            {"legacy_document", profile.legacy_document},
            {"legacy_passthrough", profile.legacy_passthrough},
            {"sources", profile.sources},
            {"selected_section", profile.selected_section}};
    validate(value);
    if (importer_.parse({{"GENERAL", value.at("values")}}).values != value.at("values"))
        throw std::runtime_error("PROFILE_FIELDS_INCOMPLETE");
    value["revision"] = revision(value);
    Lock lock(path_);
    platform::atomic_write(path_, value.dump(2), false);
    return value;
}
J ProfileStore::load() const {
    std::ifstream input(path_, std::ios::binary);
    if (!input)
        throw std::runtime_error("PROFILE_READ_FAILED");
    std::string text((std::istreambuf_iterator<char>(input)), {});
    auto value = parse_legacy_json(text);
    validate(value);
    if (importer_.parse({{"GENERAL", value.at("values")}}).values != value.at("values"))
        throw std::runtime_error("PROFILE_FIELDS_INCOMPLETE");
    if (value.at("revision") != revision(value))
        throw std::runtime_error("PROFILE_REVISION_INVALID");
    return value;
}
J ProfileStore::compare_exchange(const std::string &expected, const J &document) {
    Lock lock(path_);
    auto current = load();
    if (current.at("revision") != expected)
        throw std::runtime_error("PROFILE_CONFLICT");
    auto next = document;
    validate(next);
    if (importer_.parse({{"GENERAL", next.at("values")}}).values != next.at("values"))
        throw std::runtime_error("PROFILE_FIELDS_INCOMPLETE");
    next["revision"] = revision(next);
    platform::atomic_write(path_, next.dump(2), true);
    return next;
}
} // namespace wvd::storage
