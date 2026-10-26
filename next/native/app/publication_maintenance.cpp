#include "publication_maintenance.hpp"
#include "service_instance.hpp"
#include "repeat_request_id.hpp"
#include "games/wvd/tasks/native_publisher.hpp"
#include "platform/windows/fixed_directory_move.hpp"
#include "platform/windows/bundle_lease.hpp"
#include "platform/windows/file_digest.hpp"
#include "storage/submission_store.hpp"
#include "storage/run_store.hpp"
#include <algorithm>
#include <fstream>
#include <cctype>

namespace wvd::app {
namespace {
using J = nlohmann::json;
namespace fs = std::filesystem;
using Lease = platform::BundleLease;
const char *sentinel_name = ".publication-maintenance.json";
const char *journal_name = ".publication-maintenance-journal.json";
constexpr std::size_t journal_limit = 16 * 1024 * 1024, transaction_limit = 2048;
void need(bool value, const char *code) { if (!value) throw std::runtime_error(code); }
bool present(const fs::path &path) {
    const auto attributes = GetFileAttributesW(platform::extended_path(path).c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES) return true;
    const auto error = GetLastError();
    need(error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND, "PUBLICATION_MAINTENANCE_STAT_FAILED");
    return false;
}
void identifier(const std::string &value) {
    need(!value.empty() && value.size() <= 128 && std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    }), "PUBLICATION_MAINTENANCE_ID_INVALID");
}
fs::path ordinary(const fs::path &path) {
    auto text = path.native();
    if (text.starts_with(L"\\\\?\\UNC\\")) text = L"\\\\" + text.substr(8);
    else if (text.starts_with(L"\\\\?\\")) text = text.substr(4);
    return fs::absolute(text).lexically_normal();
}
std::string digest(const std::string &value) {
    return platform::bytes_sha256({reinterpret_cast<const std::uint8_t *>(value.data()), value.size()});
}
// Bounded disk reads deny concurrent write/delete and reject reparse files.
J read(const fs::path &path, std::string *raw_sha256 = nullptr) {
    HANDLE handle = CreateFileW(platform::extended_path(path).c_str(), GENERIC_READ,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    need(handle != INVALID_HANDLE_VALUE, "PUBLICATION_MAINTENANCE_READ_FAILED");
    struct Close { HANDLE h; ~Close() { CloseHandle(h); } } close{handle};
    BY_HANDLE_FILE_INFORMATION info{}; LARGE_INTEGER size{};
    need(GetFileInformationByHandle(handle, &info) && GetFileType(handle) == FILE_TYPE_DISK &&
        !(info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) &&
        GetFileSizeEx(handle, &size) && size.QuadPart >= 0 && size.QuadPart <= 16 * 1024 * 1024,
        "PUBLICATION_MAINTENANCE_RECORD_INVALID");
    std::string bytes(static_cast<std::size_t>(size.QuadPart), '\0');
    std::size_t offset{};
    while (offset < bytes.size()) {
        DWORD count{};
        need(ReadFile(handle, bytes.data() + offset, static_cast<DWORD>((std::min)(bytes.size() - offset, std::size_t(65536))), &count, nullptr) && count,
            "PUBLICATION_MAINTENANCE_READ_FAILED");
        offset += count;
    }
    if (raw_sha256) *raw_sha256 = digest(bytes);
    return J::parse(bytes);
}
J snapshot(const fs::path &path, bool legacy) {
    auto lease = Lease::open_publication(path);
    need(!legacy || lease->resource_digest().empty(), "PUBLICATION_ALREADY_SHARED");
    J controls = J::object();
    for (const auto &[name, hash] : lease->manifest())
        if (!lease->publication_resources().contains(name)) controls[name] = hash;
    return {{"directory_id", platform::fixed_directory_identity(path)}, {"revision",lease->revision()},
        {"manifest_sha256",digest(J(lease->manifest()).dump())}, {"resources_sha256",digest(J(lease->publication_resources()).dump())}, {"raw_controls",controls}};
}
void verify(const fs::path &path, const J &expected, bool legacy, bool same_id = true) {
    auto actual = snapshot(path, legacy);
    if (!same_id) { actual.erase("directory_id"); auto comparison = expected; comparison.erase("directory_id");
        need(actual == comparison, "PUBLICATION_MAINTENANCE_CONTENT_CHANGED"); }
    else need(actual == expected, "PUBLICATION_MAINTENANCE_CONTENT_CHANGED");
}
bool no_pending(const J &value) {
    if (value.is_object()) {
        for (const auto &[key, item] : value.items()) {
            if ((key.ends_with("_pending") || key == "pending") && item != false && item != nullptr &&
                item != J::array() && item != J::object()) return false;
            if ((key == "unresolved_inputs" || key == "handoff_parent") && !item.is_null() && !item.empty()) return false;
            if (!no_pending(item)) return false;
        }
    } else if (value.is_array()) { for (const auto &item : value) if (!no_pending(item)) return false; }
    return true;
}
using Repeat = PublicationMaintenance::RepeatEvidence;
std::optional<Repeat> repeat_hint(const J &history) {
    if (!history.contains("repeat")) return {};
    const auto &evidence = history.at("repeat");
    return Repeat{evidence.at("root_request_id").get<std::string>(),evidence.at("cycle").get<std::uint64_t>()};
}
J eligibility(const fs::path &root, const std::string &id, const std::string &revision,
    const std::optional<Repeat> &repeat = {}) {
    need(present(root / "requests") && present(root / "runs"), "PUBLICATION_HISTORY_UNKNOWN");
    storage::SubmissionStore requests(root / "requests"); requests.require_writable();
    const auto status = requests.status();
    need(status.at("scan_complete") == true && status.at("capacity_known") == true &&
        status.at("pending_count") == 0 && status.at("pending_intent_count") == 0 && status.at("unknown_entries") == 0,
        "PUBLICATION_HISTORY_UNKNOWN");
    const auto root_id = repeat ? repeat->root_request_id : id;
    identifier(root_id);
    std::map<std::string,std::uint64_t> expected;
    if (repeat) {
        need(repeat->cycle > 0 && repeat->cycle <= 65536,"PUBLICATION_REPEAT_PREFIX_LIMIT");
        need(repeat_request_id(root_id,repeat->cycle) == id,"PUBLICATION_REPEAT_ID_MISMATCH");
        for (std::uint64_t cycle = 1; cycle <= repeat->cycle; ++cycle) {
            const auto expected_id = repeat_request_id(root_id,cycle);
            if (cycle > 1) need(!present(root / "requests" / (digest(expected_id) + ".json")),"PUBLICATION_REPEAT_RELATION_CONFLICT");
            need(expected.emplace(expected_id,cycle).second,"PUBLICATION_REPEAT_RELATION_CONFLICT");
        }
    }
    std::string record_sha;
    const auto row = read(root / "requests" / (digest(root_id) + ".json"),&record_sha);
    need(row.at("schema") == 1 && row.at("request_id") == root_id && row.at("intent").is_object() &&
        row.at("receipt").is_object() && no_pending(row),"PUBLICATION_REQUEST_NOT_SETTLED");
    const auto &intent = row.at("intent");
    need(intent.contains("request") && intent.at("request").is_object(),"PUBLICATION_REPEAT_HISTORY_UNKNOWN");
    std::uint64_t target{};
    if (repeat) {
        const auto &request = intent.at("request");
        need(intent.at("kind") == "task" && request.at("request_id") == root_id &&
            (request.at("task_id") == "GiantBounty" || request.at("task_id") == "Scorpionesses") &&
            request.at("repeat") == true && request.at("repeat_count").is_number_integer() &&
            row.at("receipt").at("kind") == "start_task" && row.at("receipt").at("request_id") == root_id &&
            row.at("receipt").at("accepted") == true && row.at("receipt").contains("run_id"),"PUBLICATION_REPEAT_ROOT_NOT_ACCEPTED");
        const auto count = request.at("repeat_count").get<std::int64_t>();
        need(count >= 1 && count <= 1000000 && repeat->cycle <= static_cast<std::uint64_t>(count),"PUBLICATION_REPEAT_TARGET_INVALID");
        target = static_cast<std::uint64_t>(count);
    } else need(!intent.at("request").value("repeat",false),"PUBLICATION_REPEAT_HISTORY_UNKNOWN");
    struct SavedRun { J definition,result; std::string definition_sha,result_sha; };
    std::map<std::uint64_t,std::vector<SavedRun>> prefix;
    J runs = J::array(); std::size_t visited{};
    platform::fixed_directory_identity(root / "runs");
    for (const auto &instance : fs::directory_iterator(platform::extended_path(root / "runs"))) {
        platform::fixed_directory_identity(instance.path());
        for (const auto &entry : fs::directory_iterator(instance.path())) {
            need(++visited <= 65536,"PUBLICATION_HISTORY_SCAN_LIMIT");
            platform::fixed_directory_identity(entry.path());
            const auto run_path = root / "runs" / instance.path().filename() / entry.path().filename();
            std::string definition_sha;
            const auto definition = read(run_path / "run.json",&definition_sha);
            need(definition.at("schema") == 1 && definition.at("definition").contains("request_id"),"PUBLICATION_HISTORY_UNKNOWN");
            const auto &frozen = definition.at("definition");
            need(no_pending(frozen),"PUBLICATION_HISTORY_PENDING_REFERENCE");
            const auto request_id = frozen.at("request_id").get<std::string>();
            if (repeat ? !expected.contains(request_id) : request_id != id) continue;
            need(frozen.at("program_revision") == revision && frozen.at("pack_revision") == revision &&
                frozen.at("unique_program_count") == 1,"PUBLICATION_HISTORY_REVISION_MISMATCH");
            const auto result = storage::RunStore::read_summary(run_path);
            need(result.at("state") == "Completed" && result.at("result_saved") == true && result.at("quiescent") == true &&
                result.at("details_complete") == true && result.at("storage_error") == "" && no_pending(result) &&
                result.at("run_id") == definition.at("run_id"),"PUBLICATION_RUN_NOT_SETTLED");
            if (repeat) {
                need(frozen.at("unit_count") == 3 && clean_repeat_cycle(true,result.at("completed_business_units").get<std::uint64_t>(),
                    result.at("details_complete").get<bool>(),result.at("secondary_errors").get<std::vector<std::string>>(),result.at("business")),
                    "PUBLICATION_REPEAT_CYCLE_NOT_CLEAN");
                need(definition.at("instance") == platform::utf8(instance.path().filename()) &&
                    std::to_string(definition.at("run_id").get<std::uint64_t>()) == platform::utf8(entry.path().filename()),
                    "PUBLICATION_REPEAT_RUN_PATH_MISMATCH");
                std::string result_sha;
                need(read(run_path / "result.json",&result_sha) == result,"PUBLICATION_HISTORY_CHANGED");
                prefix[expected.at(request_id)].push_back({definition,result,definition_sha,result_sha});
            } else runs.push_back({{"instance",definition.at("instance")},{"run_id",definition.at("run_id")},
                {"definition_sha256",digest(definition.dump())},{"result_sha256",digest(result.dump())}});
        }
    }
    if (!repeat) {
        need(!runs.empty(),"PUBLICATION_HISTORY_ORPHAN");
        need(row.at("receipt").contains("run_id") && std::any_of(runs.begin(),runs.end(),[&](const auto &run) {
            return run.at("run_id") == row.at("receipt").at("run_id");
        }),"PUBLICATION_REQUEST_RUN_MISMATCH");
        return {{"request_record_sha256",digest(row.dump())},{"runs_sha256",digest(runs.dump())},{"run_count",runs.size()}};
    }
    need(prefix.size() == repeat->cycle,"PUBLICATION_REPEAT_PREFIX_MISSING");
    for (const auto &[cycle,records] : prefix) { (void)cycle; need(records.size() == 1,"PUBLICATION_REPEAT_RELATION_CONFLICT"); }
    const auto &anchor = prefix.at(1).front();
    need(anchor.definition.at("run_id") == row.at("receipt").at("run_id"),"PUBLICATION_REQUEST_RUN_MISMATCH");
    const auto &parent = anchor.definition.at("definition");
    need(parent.at("engine_kind") == "wvd_native" && parent.at("game_id") == "wvd","PUBLICATION_REPEAT_FROZEN_MISMATCH");
    J ordered = J::array();
    for (const auto &[cycle,records] : prefix) {
        const auto &record = records.front(); const auto &frozen = record.definition.at("definition");
        need(record.definition.at("instance") == anchor.definition.at("instance"),"PUBLICATION_REPEAT_INSTANCE_MISMATCH");
        for (const auto *key : {"engine_kind","game_id","device_id","viewport","unit_count","program_schema",
            "unique_program_count","unit_program_owners","program_revision","pack_revision"})
            need(frozen.at(key) == parent.at(key),"PUBLICATION_REPEAT_FROZEN_MISMATCH");
        ordered.push_back({{"cycle",cycle},{"request_id",frozen.at("request_id")},{"instance",record.definition.at("instance")},
            {"run_id",record.definition.at("run_id")},{"definition_sha256",record.definition_sha},{"result_sha256",record.result_sha}});
    }
    return {{"request_record_sha256",digest(row.dump())},{"runs_sha256",digest(ordered.dump())},{"run_count",ordered.size()},
        {"repeat",{{"root_request_id",root_id},{"cycle",repeat->cycle},{"target",target},{"root_record_sha256",record_sha},
            {"anchor_definition_sha256",anchor.definition_sha},{"anchor_result_sha256",anchor.result_sha}}}};
}
bool terminal(const J &journal) { return journal.at("phase") == "committed" || journal.at("phase") == "rolled_back"; }
fs::path trash_namespace_for(const fs::path &root, std::string id) {
    std::replace(id.begin(),id.end(),':','-');
    return root.root_path() / "trash" / "wvd-publication-history" / id;
}
fs::path trash_namespace(const fs::path &root) { return trash_namespace_for(root,platform::fixed_directory_identity(root)); }
J paths_in_namespace(const fs::path &space, const std::string &tx) {
    const auto base = space / tx;
    return {{"transaction_root",platform::utf8(base)},{"original",platform::utf8(base / "original")},
        {"converted",platform::utf8(base / "converted")}};
}
J trash_paths(const fs::path &root, const std::string &tx) { return paths_in_namespace(trash_namespace(root),tx); }
void validate_entry(const std::string &root_id, const fs::path &space, const std::string &tx, const J &journal) {
    identifier(tx); identifier(journal.at("request_id").get<std::string>());
    need(journal.at("schema") == 1 && journal.at("transaction_id") == tx &&
        journal.at("data_root_id") == root_id && journal.at("trash") == paths_in_namespace(space,tx),
        "PUBLICATION_MAINTENANCE_JOURNAL_INVALID");
    const auto phase = journal.at("phase").get<std::string>();
    static const std::vector<std::string> phases{"intent","trash_registered","create_stage_intent","stage_registered","preparing","prepared",
        "move_old_intent","old_moved","move_new_intent","new_moved","committed","rollback_intent",
        "rollback_new_moved","rollback_old_intent","rolled_back"};
    need(std::find(phases.begin(),phases.end(),phase) != phases.end(), "PUBLICATION_MAINTENANCE_PHASE_INVALID");
    if (phase == "committed") need(journal.at("stage_id").is_string() && journal.at("trash_id").is_string(),
        "PUBLICATION_MAINTENANCE_JOURNAL_INVALID");
    need(journal.at("old").at("directory_id").is_string() && journal.at("old").at("revision").is_string() &&
        journal.at("old").at("manifest_sha256").is_string() && journal.at("old").at("resources_sha256").is_string() &&
        journal.at("old").at("raw_controls").is_object() && journal.at("history").at("run_count").get<std::uint64_t>() > 0 &&
        journal.at("history").at("runs_sha256").is_string(), "PUBLICATION_MAINTENANCE_JOURNAL_INVALID");
    if (const auto hint = repeat_hint(journal.at("history"))) {
        identifier(hint->root_request_id);
        const auto &evidence = journal.at("history").at("repeat");
        need(hint->cycle > 0 && hint->cycle <= 65536 && hint->cycle <= evidence.at("target").get<std::uint64_t>() &&
            evidence.at("target").get<std::uint64_t>() <= 1000000 && journal.at("history").at("run_count") == hint->cycle &&
            repeat_request_id(hint->root_request_id,hint->cycle) == journal.at("request_id").get<std::string>(),
            "PUBLICATION_MAINTENANCE_REPEAT_EVIDENCE_INVALID");
        for (const auto *field : {"root_record_sha256","anchor_definition_sha256","anchor_result_sha256"}) {
            const auto hash = evidence.at(field).get<std::string>();
            need(hash.size() == 64 && std::all_of(hash.begin(),hash.end(),[](unsigned char c) {
                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }),"PUBLICATION_MAINTENANCE_REPEAT_EVIDENCE_INVALID");
        }
    }
}
J load_global(const fs::path &root) {
    const auto root_id = platform::fixed_directory_identity(root);
    const auto space = trash_namespace_for(root,root_id);
    const auto sentinel = read(root / sentinel_name);
    need(sentinel.at("schema") == 1 && sentinel.at("data_root_id") == root_id,
        "PUBLICATION_MAINTENANCE_SENTINEL_INVALID");
    const auto global = read(root / journal_name);
    need(global.at("schema") == 1 && global.at("journal_id") == sentinel.at("journal_id") &&
        global.at("data_root_id") == sentinel.at("data_root_id") && global.at("transactions").is_object() &&
        !global.at("transactions").empty() && global.at("registered_count") == global.at("transactions").size() &&
        global.at("transactions").size() <= transaction_limit && global.dump().size() <= journal_limit,
        "PUBLICATION_MAINTENANCE_JOURNAL_INVALID");
    for (const auto &[tx,entry] : global.at("transactions").items()) validate_entry(root_id,space,tx,entry);
    return global;
}
std::uint64_t transaction_storage_bytes(const J &global, const J &sentinel) {
    return 2ULL * (global.dump().size() + 4096) + 2ULL * sentinel.dump().size() + 65536;
}
void save_global(const fs::path &root, const J &global, bool replace) {
    const auto text = global.dump();
    need(text.size() <= journal_limit && global.at("transactions").size() <= transaction_limit,
        "PUBLICATION_MAINTENANCE_JOURNAL_CAPACITY");
    platform::atomic_write(root / journal_name,text,replace);
}
void plain_directories(const fs::path &path) {
    std::vector<fs::path> chain;
    auto current = path;
    while (current != current.parent_path()) { chain.push_back(current); current = current.parent_path(); }
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        if (!present(*it)) need(fs::create_directory(platform::extended_path(*it)), "PUBLICATION_MAINTENANCE_TRASH_CREATE_FAILED");
        platform::fixed_directory_identity(*it);
    }
}
void prepare_namespace(const fs::path &root) {
    const auto base = trash_namespace(root);
    plain_directories(base.parent_path());
    const J owner{{"schema",1},{"data_root_id",platform::fixed_directory_identity(root)}};
    if (!present(base)) {
        need(fs::create_directory(platform::extended_path(base)),"PUBLICATION_MAINTENANCE_TRASH_CREATE_FAILED");
        platform::atomic_write(base / "owner.json",owner.dump(),false);
    }
    platform::fixed_directory_identity(base);
    need(read(base / "owner.json") == owner,"PUBLICATION_MAINTENANCE_TRASH_OWNER_UNKNOWN");
}
struct Transaction {
    fs::path root, original, stage, trash, retained;
    J value;
    PublicationMaintenance::Progress progress;
    Transaction(fs::path path, J journal, PublicationMaintenance::Progress observer)
        : root(std::move(path)), value(std::move(journal)), progress(std::move(observer)) {
        const auto request = value.at("request_id").get<std::string>();
        const auto tx = value.at("transaction_id").get<std::string>();
        original = root / "published" / request;
        stage = root / "published" / (request + ".conversion-stage-" + tx);
        need(value.at("trash") == trash_paths(root,tx),"PUBLICATION_MAINTENANCE_TRASH_PATH_INVALID");
        trash = platform::path_from_utf8(value.at("trash").at("original"));
        retained = platform::path_from_utf8(value.at("trash").at("converted"));
    }
    void event(const std::string &phase) const {
        if (progress) progress({{"phase",phase},{"transaction_id",value.at("transaction_id")}});
    }
    void save(const std::string &phase) {
        value["phase"] = phase;
        auto global = load_global(root);
        const auto tx = value.at("transaction_id").get<std::string>();
        need(global.at("transactions").contains(tx),"PUBLICATION_MAINTENANCE_TRANSACTION_MISSING");
        global["transactions"][tx] = value;
        save_global(root,global,true);
        event(phase);
    }
    bool at(const fs::path &path, const std::string &identity) const {
        if (!present(path)) return false;
        need(platform::fixed_directory_identity(path) == identity, "PUBLICATION_MAINTENANCE_SLOT_OCCUPIED"); return true;
    }
    void old_verified(const fs::path &path) const { verify(path, value.at("old"), true); }
    void new_verified(const fs::path &path) const {
        need(!Lease::open_publication(path)->resource_digest().empty(),"PUBLICATION_MAINTENANCE_NEW_LAYOUT_REQUIRED");
        verify(path, value.at("old"), false, false);
    }
    void ensure_trash() {
        const auto base = trash.parent_path();
        if (!value.contains("trash_id")) {
            need(!present(base),"PUBLICATION_MAINTENANCE_TRASH_ID_UNKNOWN");
            prepare_namespace(root);
            need(fs::create_directory(platform::extended_path(base)),"PUBLICATION_MAINTENANCE_TRASH_CREATE_FAILED");
            event("trash_created_unregistered");
            value["trash_id"] = platform::fixed_directory_identity(base);
            save("trash_registered");
        }
        need(platform::fixed_directory_identity(base) == value.at("trash_id").get<std::string>(),"PUBLICATION_MAINTENANCE_TRASH_ID_MISMATCH");
    }
    void finish() {
        need(value.at("phase") != "rolled_back" && value.at("phase") != "rollback_intent" &&
            value.at("phase") != "rollback_new_moved" && value.at("phase") != "rollback_old_intent",
            "PUBLICATION_MAINTENANCE_ROLLBACK_STARTED");
        const auto old_id = value.at("old").at("directory_id").get<std::string>();
        const bool old_home = present(original) && platform::fixed_directory_identity(original) == old_id;
        const bool old_trash = at(trash, old_id);
        need(old_home != old_trash, "PUBLICATION_MAINTENANCE_OLD_LOCATION_UNKNOWN");
        old_verified(old_home ? original : trash);
        need(eligibility(root, value.at("request_id"), value.at("old").at("revision"),repeat_hint(value.at("history"))) == value.at("history"),
            "PUBLICATION_HISTORY_CHANGED");
        { const auto global = load_global(root);
          const auto sentinel = read(root / sentinel_name);
          auto source = Lease::open_publication(old_home ? original : trash);
          (void)games::tasks::admit_existing_publication(*source,stage,transaction_storage_bytes(global,sentinel)); }
        ensure_trash();
        if (!value.contains("stage_id")) {
            need(!present(stage), "PUBLICATION_MAINTENANCE_STAGE_ID_UNKNOWN");
            need(old_home && (value.at("phase") == "trash_registered" || value.at("phase") == "create_stage_intent"), "PUBLICATION_MAINTENANCE_STAGE_MISSING");
            save("create_stage_intent");
            need(fs::create_directory(platform::extended_path(stage)), "PUBLICATION_MAINTENANCE_STAGE_EXISTS");
            event("stage_created_unregistered");
            value["stage_id"] = platform::fixed_directory_identity(stage);
            save("stage_registered");
        }
        const auto stage_id = value.at("stage_id").get<std::string>();
        const bool staged = at(stage, stage_id);
        const bool activated = !old_home && at(original, stage_id);
        need(staged != activated, "PUBLICATION_MAINTENANCE_NEW_LOCATION_UNKNOWN");
        if (staged && (value.at("phase") == "stage_registered" || value.at("phase") == "preparing")) {
            // A partial metadata copy is retained for rollback; never overwrite it.
            need(fs::is_empty(platform::extended_path(stage)), "PUBLICATION_MAINTENANCE_PARTIAL_STAGE");
            save("preparing");
            { auto source = Lease::open_publication(old_home ? original : trash);
              games::tasks::prepare_existing_publication(*source, stage); }
            save("prepared");
        }
        new_verified(staged ? stage : original);
        if (old_home) {
            need(staged, "PUBLICATION_MAINTENANCE_STAGE_MISSING");
            save("move_old_intent");
            platform::move_fixed_directory(original, trash, old_id);
            save("old_moved");
        }
        // Open the moved original again and verify every byte/closed member,
        // after releasing its former BundleLease and before making new active.
        old_verified(trash);
        event("old_revalidated");
        if (staged) {
            save("move_new_intent");
            platform::move_fixed_directory(stage, original, stage_id);
            save("new_moved");
        }
        new_verified(original); old_verified(trash);
        save("committed");
    }
    void rollback() {
        need(eligibility(root,value.at("request_id"),value.at("old").at("revision"),repeat_hint(value.at("history"))) == value.at("history"),
            "PUBLICATION_HISTORY_CHANGED");
        if (!value.contains("trash_id")) need(!present(trash.parent_path()),"PUBLICATION_MAINTENANCE_TRASH_ID_UNKNOWN");
        else need(platform::fixed_directory_identity(trash.parent_path()) == value.at("trash_id").get<std::string>(),"PUBLICATION_MAINTENANCE_TRASH_ID_MISMATCH");
        const auto old_id = value.at("old").at("directory_id").get<std::string>();
        const bool old_home = present(original) && platform::fixed_directory_identity(original) == old_id;
        const bool old_trash = at(trash, old_id);
        need(old_home != old_trash, "PUBLICATION_MAINTENANCE_OLD_LOCATION_UNKNOWN");
        old_verified(old_home ? original : trash);
        if (!value.contains("stage_id")) {
            need(!present(stage), "PUBLICATION_MAINTENANCE_STAGE_ID_UNKNOWN");
            need(old_home, "PUBLICATION_MAINTENANCE_STAGE_MISSING");
            save("rolled_back"); return;
        }
        const auto stage_id = value.at("stage_id").get<std::string>();
        const bool staged = at(stage, stage_id);
        const bool active = !old_home && at(original, stage_id);
        const bool kept = at(retained, stage_id);
        need(unsigned(staged) + unsigned(active) + unsigned(kept) == 1, "PUBLICATION_MAINTENANCE_NEW_LOCATION_UNKNOWN");
        save("rollback_intent");
        if (!kept) {
            platform::move_fixed_directory(staged ? stage : original, retained, stage_id);
            save("rollback_new_moved");
        }
        if (!old_home) {
            old_verified(trash); save("rollback_old_intent");
            platform::move_fixed_directory(trash, original, old_id);
        }
        old_verified(original);
        save("rolled_back");
    }
};
}
void PublicationMaintenance::require_no_pending(const fs::path &input) {
    const auto root = ordinary(input);
    bool saw_journal{};
    for (const auto &entry : fs::directory_iterator(platform::extended_path(root))) {
        const auto name = platform::utf8(entry.path().filename());
        if (!name.starts_with(".publication-maintenance")) continue;
        need(name == sentinel_name || name == journal_name,"PUBLICATION_MAINTENANCE_UNKNOWN_ARTIFACT");
        if (name == journal_name) saw_journal = true;
    }
    if (!present(root / sentinel_name)) { need(!saw_journal,"PUBLICATION_MAINTENANCE_SENTINEL_MISSING"); return; }
    const auto global = load_global(root);
    for (const auto &[tx,entry] : global.at("transactions").items()) {
        (void)tx; need(terminal(entry),"PUBLICATION_MAINTENANCE_PENDING");
    }
}
J PublicationMaintenance::execute(const fs::path &input, const std::string &command,
    const std::string &id, const Progress &progress, const std::optional<RepeatEvidence> &repeat) {
    const auto root = ordinary(input);
    need(!input.empty() && present(root),"PUBLICATION_MAINTENANCE_EXISTING_ROOT_REQUIRED");
    platform::fixed_directory_identity(root); identifier(id);
    need(command == "plan" || command == "convert" || command == "status" || command == "finish" || command == "rollback",
        "PUBLICATION_MAINTENANCE_COMMAND_INVALID");
    need(!repeat || command == "plan" || command == "convert","PUBLICATION_MAINTENANCE_ARGUMENT_INVALID");
    ServiceInstance exclusive(root);
    if (command == "plan" || command == "convert") {
        require_no_pending(root);
        const auto old = snapshot(root / "published" / id,true);
        const auto history = eligibility(root,id,old.at("revision"),repeat);
        J plan{{"schema",1},{"request_id",id},{"old",old},{"history",history},
            {"data_root_id",platform::fixed_directory_identity(root)},{"trash_namespace",platform::utf8(trash_namespace(root))}};
        if (command == "plan") return plan;
        plan["transaction_id"] = platform::unique_id(); plan["phase"] = "intent";
        const auto tx = plan.at("transaction_id").get<std::string>(); plan["trash"] = trash_paths(root,tx);
        const bool existing = present(root / sentinel_name);
        J global = existing ? load_global(root) : J{{"schema",1},{"journal_id",platform::unique_id()},
            {"data_root_id",plan.at("data_root_id")},{"transactions",J::object()}};
        need(global.at("transactions").size() < transaction_limit,"PUBLICATION_MAINTENANCE_JOURNAL_CAPACITY");
        global["transactions"][tx] = plan;
        global["registered_count"] = global.at("transactions").size();
        const auto bytes = global.dump().size();
        need(bytes <= journal_limit - 4096,"PUBLICATION_MAINTENANCE_JOURNAL_CAPACITY");
        const J sentinel{{"schema",1},{"journal_id",global.at("journal_id")},{"data_root_id",plan.at("data_root_id")}};
        const auto stage = root / "published" / (id + ".conversion-stage-" + tx);
        { auto source = Lease::open_publication(root / "published" / id);
          // Current + temporary global records, sentinel and bounded future IDs.
          (void)games::tasks::admit_existing_publication(*source,stage,transaction_storage_bytes(global,sentinel)); }
        if (!existing) {
            platform::atomic_write(root / sentinel_name,sentinel.dump(),false);
            if (progress) progress({{"phase","sentinel_persisted"},{"transaction_id",tx}});
        }
        save_global(root,global,existing);
        Transaction transaction(root,plan,progress); transaction.event("intent"); transaction.finish(); return transaction.value;
    }
    const auto global = load_global(root);
    need(global.at("transactions").contains(id),"PUBLICATION_MAINTENANCE_TRANSACTION_MISSING");
    const auto journal = global.at("transactions").at(id);
    if (command == "status") return journal;
    Transaction transaction(root,journal,progress);
    if (command == "finish") transaction.finish(); else transaction.rollback();
    return transaction.value;
}
} // namespace wvd::app
