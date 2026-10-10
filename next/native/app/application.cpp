#include "application.hpp"

#include "games/wvd/chest/chest.hpp"
#include "games/wvd/combat/encounter.hpp"
#include "games/wvd/combat/debug.hpp"
#include "games/wvd/combat/enemy_rules.hpp"
#include "games/wvd/combat/turn.hpp"
#include "games/wvd/recovery/boot.hpp"
#include "games/wvd/state.hpp"
#include "games/wvd/tasks/bounty_cycle.hpp"
#include "games/wvd/tasks/bull_cave.hpp"
#include "games/wvd/tasks/cave_of_separation.hpp"
#include "games/wvd/tasks/dark_light.hpp"
#include "games/wvd/tasks/dungeon_iteration.hpp"
#include "games/wvd/tasks/departure.hpp"
#include "games/wvd/navigation/dungeon_entry.hpp"
#include "games/wvd/tasks/fishing_supply.hpp"
#include "games/wvd/tasks/fordraig.hpp"
#include "games/wvd/tasks/fortress_trap.hpp"
#include "games/wvd/tasks/giant.hpp"
#include "games/wvd/tasks/gold_income.hpp"
#include "games/wvd/tasks/golden_chest.hpp"
#include "games/wvd/tasks/handoff_provenance.hpp"
#include "games/wvd/tasks/manual_separation.hpp"
#include "games/wvd/tasks/locale_assets.hpp"
#include "games/wvd/tasks/mining.hpp"
#include "games/wvd/tasks/repel_forces.hpp"
#include "games/wvd/tasks/sandman.hpp"
#include "games/wvd/tasks/sleep_visits.hpp"
#include "games/wvd/tasks/steel_trial.hpp"
#include "games/wvd/tasks/native_publisher.hpp"
#include "games/wvd/native_operations.hpp"
#include "games/wvd/tasks/author_workflow.hpp"
#include "games/wvd/tasks/public_flow_library.hpp"
#include "games/wvd/tasks/public_step_scope.hpp"
#include "games/wvd/tasks/run_builder.hpp"
#include "games/wvd/vision/native_recognizers.hpp"
#include "games/wvd/vision/native_asset_resolver.hpp"
#include "authoring/resource_locale.hpp"
#include "platform/windows/path_utf8.hpp"
#include "platform/windows/bundle_lease.hpp"
#include "platform/windows/file_digest.hpp"
#include "platform/windows/mumu_binding.hpp"
#include "platform/windows/runtime_files.hpp"
#include "platform/windows/preparation_timer.hpp"
#include "storage/legacy_import.hpp"
#include "storage/run_store.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <opencv2/imgcodecs.hpp>
#include <windows.h>

#include <commdlg.h>

namespace wvd::app {
using namespace std::chrono_literals;
namespace {
using J = nlohmann::json;
void require(bool condition, const char *code);
void discard_unstarted_publication(runtime::NativeRunDefinition &definition,
    recognition::Bundle &publication, const std::filesystem::path &expected) {
    require(publication.root == expected && publication.lease, "PREPARATION_PUBLICATION_OWNERSHIP_INVALID");
    publication.lease->verify_members();
    const auto inspect = [](const std::filesystem::path &path) {
        HANDLE handle = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error("PREPARATION_CLEANUP_IDENTITY_FAILED");
        BY_HANDLE_FILE_INFORMATION info{};
        const bool ok = GetFileInformationByHandle(handle, &info);
        CloseHandle(handle);
        if (!ok || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::runtime_error("PREPARATION_CLEANUP_IDENTITY_FAILED");
        return info;
    };
    const auto before = inspect(expected);
    definition.units.clear();
    require(publication.lease.use_count() == 1, "PREPARATION_PUBLICATION_STILL_OWNED");
    publication.lease.reset();
    const auto after = inspect(expected);
    require(before.dwVolumeSerialNumber == after.dwVolumeSerialNumber &&
        before.nFileIndexHigh == after.nFileIndexHigh && before.nFileIndexLow == after.nFileIndexLow,
        "PREPARATION_CLEANUP_IDENTITY_CHANGED");
    std::error_code error;
    std::filesystem::remove_all(expected, error);
    if (error) throw std::runtime_error("PREPARATION_CLEANUP_FAILED:" + error.message());
}
void declare_portrait_assets(games::tasks::CompiledWorkflow &workflow,
                            const std::optional<recognition::Bundle> &validated) {
    auto &images = workflow.authoring["provided_portrait_images"] = J::array();
    if (validated)
        for (const auto &file : validated->files)
            images.push_back(file.relative_path.substr(std::string("image/").size()));
}
std::filesystem::path executable_directory() {
    std::wstring buffer(32768, L'\0');
    const auto count = GetModuleFileNameW(nullptr, buffer.data(),
        static_cast<DWORD>(buffer.size()));
    if (!count || count >= buffer.size())
        throw std::runtime_error("APPLICATION_PATH_UNAVAILABLE");
    buffer.resize(count);
    return std::filesystem::path(buffer).parent_path();
}
void require(bool condition, const char *code) {
    if (!condition)
        throw std::runtime_error(code);
}
std::set<std::string> strategy_names(const J &values) {
    std::set<std::string> names;
    const auto strategies = values.value("STRATEGY", J::array());
    if (strategies.is_array()) {
        for (const auto &group : strategies) {
            const auto name = group.at("group_name").get<std::string>();
            require(!name.empty() && names.insert(name).second, "STRATEGY_NAME_DUPLICATE_OR_EMPTY");
        }
    } else if (strategies.is_object()) {
        for (const auto &[name, group] : strategies.items()) {
            (void)group;
            require(!name.empty() && names.insert(name).second, "STRATEGY_NAME_DUPLICATE_OR_EMPTY");
        }
    } else throw std::runtime_error("STRATEGY_DATA_INVALID");
    return names;
}
void strategy_references(J &scope, const std::function<void(J &)> &visit) {
    if (!scope.is_object()) return;
    if (scope.contains("DEFAULT_OVERALL_STRATEGY")) visit(scope["DEFAULT_OVERALL_STRATEGY"]);
    if (!scope.contains("TASK_POINT_STRATEGY") || !scope["TASK_POINT_STRATEGY"].is_object()) return;
    auto &bindings = scope["TASK_POINT_STRATEGY"];
    if (bindings.contains("special_combat") && bindings["special_combat"].is_object()) {
        auto &special = bindings["special_combat"];
        if (special.contains("normal_strategy")) visit(special["normal_strategy"]);
        if (special.contains("special_strategy")) visit(special["special_strategy"]);
        if (special.contains("rules") && special["rules"].is_array())
            for (auto &rule : special["rules"]) if (rule.is_object() && rule.contains("strategy")) visit(rule["strategy"]);
    }
    if (bindings.contains("overall_strategy")) visit(bindings["overall_strategy"]);
    if (!bindings.contains("task_point")) return;
    auto &points = bindings["task_point"];
    if (points.is_object()) { for (auto &value : points) visit(value); }
    else if (points.is_array()) {
        for (auto &point : points) if (point.is_object() && point.contains("strategy")) visit(point["strategy"]);
    }
}
void all_profile_references(J &document, const std::function<void(J &)> &visit) {
    if (document.contains("values")) strategy_references(document["values"], visit);
    if (document.contains("default_values")) strategy_references(document["default_values"], visit);
    if (document.contains("task_overrides"))
        for (auto &scope : document["task_overrides"]) strategy_references(scope, visit);
}
J profile_save_sections() {
    return {
        {"task", {"FARM_TARGET", "FARM_TARGET_TEXT", "TASK_SPECIFIC_CONFIG"}},
        {"common", {"WHO_WILL_OPEN_IT", "QUICK_DISARM_CHEST", "SKIP_COMBAT_RECOVER",
                    "SKIP_CHEST_RECOVER", "RECOVER_WHEN_BEGINNING", "ACTIVE_REST",
                    "REST_INTERVEL", "KARMA_ADJUST", "RE_ASSEMBLE_PARTY",
                    "DEFAULT_OVERALL_STRATEGY", "TASK_POINT_STRATEGY"}},
        {"combat", {"STRATEGY", "RELOAD_STRATEGY_WHEN"}},
        {"advanced", {"EMU_PATH", "EMU_INDEX", "ADB_ADRESS", "AUTO_START_CLASH",
                      "LANGUAGE", "WEBSITE_ORG_TIME", "AM_REFRESH_TIME", "ACTIVE_BEG_MONEY",
                      "ACTIVE_ROYALSUITE_REST", "ACTIVE_TRIUMPH", "ACTIVE_BEAUTIFUL_ORE",
                      "ACTIVE_CSC", "BYPASS_THE_WALL", "MAX_TRY_LIMIT", "MAX_CRASH_LIMIT"}}
    };
}
std::string optional_profile_text(const J &object, const char *key) {
    const auto it = object.find(key);
    if (it == object.end() || it->is_null()) return {};
    require(it->is_string(), "PROFILE_TEXT_TYPE_INVALID");
    return it->get<std::string>();
}
std::string checked_request_id(const J &request) {
    require(request.is_object(), "REQUEST_OBJECT_REQUIRED");
    auto id = request.value("request_id", platform::unique_id());
    // GUID may include braces in the platform helper; canonicalize only generated IDs.
    if (!request.contains("request_id")) {
        id.erase(std::remove(id.begin(), id.end(), '{'), id.end());
        id.erase(std::remove(id.begin(), id.end(), '}'), id.end());
    }
    require(!id.empty() && id.size() <= 128 &&
        id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") == std::string::npos,
        "REQUEST_ID_INVALID");
    return id;
}
std::string target_path(const api::Request &request) {
    auto value = std::string(request.target());
    return value.substr(0, value.find('?'));
}
J parse_body(const api::Request &request) {
    require(request.body().size() <= 16 * 1024 * 1024, "REQUEST_TOO_LARGE");
    return request.body().empty() ? J::object() : storage::parse_legacy_json(request.body());
}
std::vector<std::uint8_t> decode_base64(std::string_view text) {
    static const auto table = [] {
        std::array<int, 256> value{};
        value.fill(-1);
        constexpr std::string_view alphabet =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (std::size_t index = 0; index < alphabet.size(); ++index)
            value[static_cast<unsigned char>(alphabet[index])] = static_cast<int>(index);
        return value;
    }();
    require(!text.empty() && text.size() <= 12 * 1024 * 1024 && text.size() % 4 == 0,
            "PROBE_IMAGE_ENCODING_INVALID");
    std::vector<std::uint8_t> result;
    result.reserve(text.size() / 4 * 3);
    std::uint32_t accumulator = 0;
    int bits = -8;
    bool padding = false;
    std::size_t padding_count = 0, symbols = 0;
    for (const auto byte : text) {
        if (byte == '=') {
            padding = true;
            require(++padding_count <= 2, "PROBE_IMAGE_ENCODING_INVALID");
            continue;
        }
        require(!padding && table[static_cast<unsigned char>(byte)] >= 0,
                "PROBE_IMAGE_ENCODING_INVALID");
        ++symbols;
        accumulator = (accumulator << 6) + static_cast<std::uint32_t>(table[static_cast<unsigned char>(byte)]);
        bits += 6;
        if (bits >= 0) {
            result.push_back(static_cast<std::uint8_t>((accumulator >> bits) & 0xff));
            bits -= 8;
        }
    }
    require((padding_count == 0 && symbols % 4 == 0) ||
                (padding_count == 1 && symbols % 4 == 3 && (accumulator & 3u) == 0) ||
                (padding_count == 2 && symbols % 4 == 2 && (accumulator & 15u) == 0),
            "PROBE_IMAGE_ENCODING_INVALID");
    require(!result.empty() && result.size() <= 8 * 1024 * 1024,
            "PROBE_IMAGE_BYTES_INVALID");
    return result;
}
api::DynamicReply json_reply(const J &value, api::http::status status = api::http::status::ok) {
    return {status, value.dump(), "application/json; charset=utf-8"};
}
api::DynamicReply error_reply(const std::exception &error) {
    return json_reply({{"error_code", error.what()}, {"message", error.what()}},
                      api::http::status::bad_request);
}
bool terminal(contracts::RunState state) {
    return state == contracts::RunState::Idle || state == contracts::RunState::Completed ||
           state == contracts::RunState::UserStopped || state == contracts::RunState::Failed ||
           state == contracts::RunState::Interrupted;
}
contracts::ActionKind action_kind(const std::string &name) {
    if (name == "Click")
        return contracts::ActionKind::Click;
    if (name == "ClickKey")
        return contracts::ActionKind::ClickKey;
    if (name == "Swipe")
        return contracts::ActionKind::Swipe;
    throw std::runtime_error("WORKFLOW_ACTION_UNSUPPORTED");
}
std::filesystem::path manager_from_path(const std::filesystem::path &input) {
    if (input.filename() == "MuMuManager.exe")
        return input;
    for (auto parent = input.parent_path(); !parent.empty(); parent = parent.parent_path()) {
        auto candidate = parent / "nx_main" / "MuMuManager.exe";
        if (std::filesystem::is_regular_file(candidate))
            return candidate;
        if (parent == parent.parent_path())
            break;
    }
    throw std::runtime_error("MUMU_MANAGER_NOT_FOUND");
}
J observation_json(const contracts::Observation &value) {
    const auto outcome = value.outcome == contracts::RecognitionOutcome::Hit
                             ? "Hit"
                             : value.outcome == contracts::RecognitionOutcome::NoHit ? "NoHit" : "Error";
    J matches = J::array();
    for (const auto &match : value.matches)
        matches.push_back({{"box", {match.box.x, match.box.y, match.box.width, match.box.height}},
                           {"score", match.score}, {"text", match.text}});
    double score{};
    for (const auto &match : value.matches)
        score = std::max(score, match.score);
    return {{"outcome", outcome}, {"score", score}, {"action_eligible", value.action_eligible},
            {"center", value.center ? J::array({value.center->x, value.center->y}) : J(nullptr)},
            {"box", value.box ? J::array({value.box->x, value.box->y, value.box->width, value.box->height}) : J(nullptr)},
            {"matches", matches}, {"error_code", value.error_code},
            {"error_stage", value.error_stage}, {"evidence", value.evidence},
            {"timing_ms", value.timing_ms}, {"frame_id", value.basis.frame_id}};
}

J author_document_from_ui(const J &ui) {
    if (ui.contains("schema") && ui.contains("flow"))
        return ui;
    require(ui.is_object() && ui.contains("id") && ui.contains("name") &&
                ui.contains("nodes") && ui.contains("edges"),
            "WORKFLOW_DOCUMENT_INVALID");
    J document{{"schema", 1},
               {"flow", {{"id", ui.at("id")},
                          {"name", ui.at("name")},
                          {"description", ui.value("description", "")}}},
               {"entry", ui.value("entry_node_id", "")},
               {"nodes", J::array()}, {"edges", J::array()},
               {"layout", {{"nodes", J::array()},
                            {"viewport", ui.value("viewport", J{{"x", 0}, {"y", 0}, {"zoom", 1}})}}},
               {"execution", {{"time_limit_ms", ui.value("time_limit_ms", 60000)}}}};
    if (ui.contains("revision"))
        document["revision"] = ui.at("revision");
    if (ui.contains("interface")) document["interface"] = ui.at("interface");
    if (ui.contains("resource_locale")) document["execution"]["resource_locale"] = ui.at("resource_locale");
    if (ui.contains("events")) document["execution"]["events"] = ui.at("events");
    if (ui.contains("checks")) document["execution"]["checks"] = ui.at("checks");
    for (const auto &source : ui.at("nodes")) {
        const auto &data = source.at("data");
        J parameters = data.value("parameters", J::object());
        J node{{"id", source.at("id")},
               {"type", data.at("node_type")},
               {"name", data.value("label", source.at("id").get<std::string>())},
               {"parameters", std::move(parameters)}};
        if (source.contains("repeat_limit"))
            node["repeat_limit"] = source.at("repeat_limit");
        if (source.contains("event_overrides")) node["event_overrides"] = source.at("event_overrides");
        if (source.contains("resume")) node["resume"] = source.at("resume");
        document["nodes"].push_back(std::move(node));
        const auto position = source.value("position", J{{"x", 0}, {"y", 0}});
        document["layout"]["nodes"].push_back(
            {{"node_id", source.at("id")}, {"x", position.at("x")}, {"y", position.at("y")}});
    }
    for (const auto &source : ui.at("edges")) {
        const auto data = source.value("data", J::object());
        document["edges"].push_back(
            {{"id", source.at("id")}, {"from", source.at("source")},
             {"to", source.at("target")},
             {"outcome", source.value("sourceHandle", J("success")).is_string() &&
                 source.value("sourceHandle", J("success")).get<std::string>().starts_with("handoff:")
                 ? source.at("sourceHandle").get<std::string>()
                 : data.value("kind", "sequence") == "failure" ? "failure" : "success"},
             {"order", data.value("order", 0)}});
    }
    return document;
}

J ui_document_from_author(const J &document) {
    J ui{{"id", document.at("flow").at("id")},
         {"name", document.at("flow").at("name")},
         {"description", document.at("flow").at("description")},
         {"entry_node_id", document.at("entry")}, {"nodes", J::array()}, {"edges", J::array()},
         {"viewport", document.at("layout").at("viewport")},
         {"time_limit_ms", document.at("execution").at("time_limit_ms")}, {"runnable", true}};
    if (document.contains("revision"))
        ui["revision"] = document.at("revision");
    ui["interface"] = document.value("interface", J::object());
    ui["resource_locale"] = document.at("execution").value("resource_locale", std::string{});
    if (document.at("execution").contains("events")) ui["events"] = document.at("execution").at("events");
    if (document.at("execution").contains("checks")) ui["checks"] = document.at("execution").at("checks");
    std::map<std::string, J> positions;
    for (const auto &position : document.at("layout").at("nodes"))
        positions[position.at("node_id").get<std::string>()] =
            {{"x", position.at("x")}, {"y", position.at("y")}};
    for (const auto &source : document.at("nodes")) {
        const auto id = source.at("id").get<std::string>();
        J node{{"id", id}, {"type", "editor"}, {"position", positions.at(id)},
               {"data", {{"label", source.at("name")}, {"node_type", source.at("type")},
                          {"parameters", source.at("parameters")}}}};
        if (source.contains("repeat_limit"))
            node["repeat_limit"] = source.at("repeat_limit");
        if (source.contains("event_overrides")) node["event_overrides"] = source.at("event_overrides");
        if (source.contains("resume")) node["resume"] = source.at("resume");
        ui["nodes"].push_back(std::move(node));
    }
    std::map<std::pair<std::string, std::string>, int> outcome_counts;
    for (const auto &edge : document.at("edges"))
        ++outcome_counts[{edge.at("from").get<std::string>(), edge.at("outcome").get<std::string>()}];
    for (const auto &source : document.at("edges")) {
        const auto from = source.at("from").get<std::string>();
        const auto outcome = source.at("outcome").get<std::string>();
        const auto kind = outcome.starts_with("handoff:") ? "handoff" : outcome == "failure" ? "failure"
                          : outcome_counts[{from, outcome}] > 1 ? "candidate" : "sequence";
        ui["edges"].push_back(
            {{"id", source.at("id")}, {"source", from}, {"target", source.at("to")},
             {"sourceHandle", outcome},
             {"data", {{"kind", kind}, {"order", source.at("order")}}}});
    }
    return ui;
}

J selected_node_document(J document, const std::string &selected) {
    const auto found = std::find_if(document.at("nodes").begin(), document.at("nodes").end(),
                                    [&](const J &node) { return node.at("id") == selected; });
    require(found != document.at("nodes").end() && found->at("type") != "end",
            "WORKFLOW_DEBUG_NODE_INVALID");
    J node = *found;
    const std::string success = selected == "debug_success" ? "debug_success_1" : "debug_success";
    const std::string failure = selected == "debug_failure" ? "debug_failure_1" : "debug_failure";
    document["entry"] = selected;
    document["nodes"] = J::array({node,
        J{{"id", success}, {"type", "end"}, {"name", "调试成功"},
          {"parameters", {{"outcome", "success"}}}}});
    document["edges"] = J::array({
        J{{"id", "debug_success_edge"}, {"from", selected}, {"to", success},
          {"outcome", "success"}, {"order", 0}}});
    document["layout"]["nodes"] = J::array({
        J{{"node_id", selected}, {"x", 0}, {"y", 0}},
        J{{"node_id", success}, {"x", 300}, {"y", -80}}});
    // 等待节点没有失败语义，不能为它伪造编译器明确禁止的失败出口。
    // 其他节点保留失败终点，便于调试结果准确区分 NoHit/拒绝与成功。
    if (node.at("type") != "wait") {
        document["nodes"].push_back(
            J{{"id", failure}, {"type", "end"}, {"name", "调试失败"},
              {"parameters", {{"outcome", "failure"}, {"reason", "selected_node_failed"}}}});
        document["edges"].push_back(
            J{{"id", "debug_failure_edge"}, {"from", selected}, {"to", failure},
              {"outcome", "failure"}, {"order", 0}});
        document["layout"]["nodes"].push_back(
            J{{"node_id", failure}, {"x", 300}, {"y", 80}});
    }
    document.erase("revision");
    return document;
}

std::filesystem::path freeze_manifest_members(const std::filesystem::path &pack_root,
                                              const std::filesystem::path &data_root,
                                              const J &manifest) {
    const auto identity = J{{"revision", manifest.at("revision")},
                            {"files", manifest.at("files")}}.dump();
    const auto digest = platform::bytes_sha256(
        {reinterpret_cast<const std::uint8_t *>(identity.data()), identity.size()});
    const auto cache_root = platform::extended_path(data_root / "asset-cache");
    const auto destination = cache_root / digest;
    if (std::filesystem::is_directory(destination))
        return destination;

    const auto staging = cache_root / (digest + ".tmp-" + platform::unique_id());
    std::filesystem::create_directories(staging);
    try {
        for (const auto &file : manifest.at("files")) {
            const auto relative = platform::BundleLease::checked_relative(
                file.at("path").get<std::string>());
            const auto source = pack_root / relative;
            const auto target = staging / relative;
            require(std::filesystem::is_regular_file(source), "APPLICATION_ASSET_MISSING");
            std::filesystem::create_directories(target.parent_path());
            require(std::filesystem::copy_file(source, target,
                                                std::filesystem::copy_options::none),
                    "APPLICATION_ASSET_COPY_FAILED");
            require(platform::file_sha256(target) == file.at("sha256").get<std::string>(),
                    "APPLICATION_ASSET_HASH_MISMATCH");
        }
        std::filesystem::create_directories(cache_root);
        std::filesystem::rename(staging, destination);
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove_all(staging, ignored);
        throw;
    }
    return destination;
}
} // namespace

Application::J Application::load_json(const std::filesystem::path &path) const {
    std::ifstream input(path, std::ios::binary);
    require(bool(input), "APPLICATION_DATA_MISSING");
    std::string text((std::istreambuf_iterator<char>(input)), {});
    return storage::parse_legacy_json(text);
}

Application::Application(ApplicationPaths paths,
                         std::shared_ptr<devices::DeviceConnection> connection)
    : paths_(std::move(paths)), backend_(std::move(connection)) {
    require(paths_.data_root.is_absolute() && paths_.pack_root.is_absolute() &&
                paths_.quest_catalog.is_absolute(),
            "APPLICATION_PATH_INVALID");
    std::filesystem::create_directories(paths_.data_root);
    descriptor_ = load_json(paths_.pack_root / "parameters/legacy-config-fields.json");
    manifest_ = load_json(paths_.pack_root / "manifest.json");
    aliases_ = manifest_.value("aliases", J::object());
    author_bundle_.revision = manifest_.at("revision");
    author_bundle_.snapshot_parent = paths_.data_root / "active-snapshots";
    for (const auto &file : manifest_.at("files")) {
        const auto relative = file.at("path").get<std::string>();
        author_bundle_.files.push_back({relative, file.at("sha256")});
        if (relative.starts_with("image/"))
            available_images_.insert(relative.substr(6));
    }
    // 发布目录还包含 manifest 和界面描述，它们不属于运行资源成员。
    // 将清单成员冻结到独立快照，既保持严格完整性校验，也避免把元数据误判成注入文件。
    author_bundle_.root = freeze_manifest_members(paths_.pack_root, paths_.data_root, manifest_);
    auto quests = load_json(paths_.quest_catalog);
    catalog_ = std::make_unique<games::WvdQuestCatalog>(quests);
    const auto profile_path = paths_.data_root / "profile.json";
    profile_store_ = std::make_unique<storage::ProfileStore>(profile_path, descriptor_);
    workflow_store_ = std::make_unique<storage::WorkflowRepository>(paths_.data_root / "workflows");
    submission_store_ = std::make_unique<storage::SubmissionStore>(paths_.data_root / "requests");
    // 首次引入公共定义；已有同 ID 的用户编辑版本绝不覆盖。公共定义仍存于同一 WorkflowRepository。
    const auto semantic_path = author_bundle_.root / platform::BundleLease::checked_relative("parameters/semantic-assets.json");
    if (std::filesystem::is_regular_file(semantic_path)) semantic_catalogue_ = load_json(semantic_path);
    const auto library_path = author_bundle_.root / platform::BundleLease::checked_relative("parameters/public-flows.json");
    if (std::filesystem::is_regular_file(library_path)) {
        std::set<std::string> existing;
        for (const auto &entry : workflow_store_->list()) existing.insert(entry.at("id").get<std::string>());
        const auto library = load_json(library_path);
        require(library.is_array(), "FLOW_SEED_INVALID");
        for (const auto &document : library) {
            const auto id = document.at("flow").at("id").get<std::string>();
            builtin_documents_[id] = document;
            if (!existing.contains(id)) {
                const auto created = workflow_store_->create(document);
                workflow_store_->register_builtin(document, created.at("revision"));
                existing.insert(id);
            }
        }
    }

    if (!std::filesystem::exists(profile_path)) {
        storage::LegacyConfigImporter importer(descriptor_);
        games::WvdProfile initial;
        if (!paths_.legacy_config.empty() && std::filesystem::is_regular_file(paths_.legacy_config)) {
            const auto copy = paths_.data_root / "legacy-import";
            if (!std::filesystem::exists(copy))
                initial = importer.import_copy(paths_.legacy_config, copy);
            else
                initial = importer.parse(load_json(copy / "legacy-config.json"));
        } else
            initial = importer.parse({{"GENERAL", J::object()}});
        profile_store_->create(initial);
    }
    coordinator_ = std::make_unique<runtime::NativeRunCoordinator>(paths_.data_root / "runs", 1024,
        paths_.service_instance_id);
    operation_ = {{"state", "idle"}, {"name", nullptr}, {"error", nullptr}};
}

Application::~Application() { stop(); }

bool Application::run_active() const {
    return coordinator_ && !coordinator_->wait_for(std::chrono::milliseconds{0});
}

Application::J Application::profile() const {
    const auto stored = profile_store_->load();
    const auto &values = stored.at("values");
    const auto task = optional_profile_text(values, "FARM_TARGET");
    const bool task_specific = values.value("TASK_SPECIFIC_CONFIG", false);
    return {{"profile", values}, {"logging", storage::LoggingPolicy::from_profile(stored).json()},
            {"revision", stored.at("revision")},
            {"effective_source", task_specific ? "任务覆盖" : "默认配置"},
            {"task_override_active", task_specific && !task.empty() &&
                 stored.value("task_overrides", J::object()).contains(task)}};
}

Application::J Application::profile_for_task(const std::string &task_id) const {
    (void)catalog_->at(task_id);
    const auto stored = profile_store_->load();
    auto values = effective_profile_values(task_id, stored);
    const bool overridden = values.value("TASK_SPECIFIC_CONFIG", false);
    return {{"profile", values}, {"logging", storage::LoggingPolicy::from_profile(stored).json()},
            {"revision", stored.at("revision")},
            {"effective_source", overridden ? "任务覆盖" : "默认配置"},
            {"task_override_active", overridden}};
}

Application::J Application::effective_profile_values(const std::string &task_id) const {
    return effective_profile_values(task_id, profile_store_->load());
}
Application::J Application::effective_profile_values(const std::string &task_id, const J &stored) const {
    const auto &task = catalog_->at(task_id);
    auto values = stored.value("default_values", stored.at("values"));
    const auto overrides = stored.value("task_overrides", J::object());
    const bool overridden = stored.at("values").value("TASK_SPECIFIC_CONFIG", false) &&
                            overrides.contains(task_id);
    if (overridden) {
        const auto &task_values = overrides.at(task_id);
        for (const auto &field : descriptor_.at("fields")) {
            const auto name = field.at("name").get<std::string>();
            if (field.at("category").get<std::string>() == "TEMPLATE" &&
                task_values.contains(name))
                values[name] = task_values.at(name);
        }
    }
    values["FARM_TARGET"] = task_id;
    values["FARM_TARGET_TEXT"] = task.source.value("questName", task_id);
    values["TASK_SPECIFIC_CONFIG"] = overridden;
    return values;
}

Application::J Application::catalog() const {
    J tasks = J::array();
    std::set<std::string> categories;
    for (const auto &task : catalog_->tasks())
    {
        const auto category = task.source.value("questCategory", "未分类");
        categories.insert(category);
        J points = J::array();
        if (task.source.contains("_TARGETINFOLIST") && task.source.at("_TARGETINFOLIST").is_array()) {
            std::size_t index{};
            for (const auto &point : task.source.at("_TARGETINFOLIST")) {
                const auto label = point.is_array() && !point.empty() && point[0].is_string()
                                       ? point[0].get<std::string>() : "任务点";
                points.push_back({{"value", std::to_string(index)},
                                  {"label", std::to_string(++index) + ". " + label}});
            }
        }
        tasks.push_back({{"id", task.id}, {"type", task.type},
                         {"name", task.source.value("questName", task.id)},
                         {"category", category},
                         {"description", task.source.value("_TIPS", "")},
                         {"task_points", std::move(points)}});
    }
    J category_options = J::array();
    for (const auto &category : categories)
        category_options.push_back({{"value", category}, {"label", category}});
    const auto options = [](std::initializer_list<J> rows) { return J(rows); };
    J templates = J::array();
    std::set<std::string> role_names;
    for (const auto &image : available_images_)
    {
        templates.push_back({{"value", image}, {"label", image}});
        constexpr std::string_view prefix = "spellskill/char/";
        if (image.starts_with(prefix) && image.ends_with(".png")) {
            auto role = image.substr(prefix.size(), image.size() - prefix.size() - 4);
            if (role.ends_with("_sp")) role.resize(role.size() - 3);
            if (role.ends_with("_alt")) role.resize(role.size() - 4);
            role_names.insert(std::move(role));
        }
    }
    J roles = J::array();
    for (const auto &role : role_names)
        roles.push_back({{"value", role}, {"label", role}});
    return {{"tasks", tasks}, {"task_categories", category_options},
            {"fields", descriptor_.at("fields")},
            {"profile_save_sections", profile_save_sections()},
            {"node_types", options({
                J{{"type", "recognition"}, {"label", "画面识别"}, {"category", "视觉"},
                  {"defaults", {{"condition", {{"mode", "combat_active"}}}}}},
                J{{"type", "action"}, {"label", "受控点击"}, {"category", "动作"},
                  {"defaults", {{"operation", "fixed_click"},
                    {"scene", {{"mode", "combat_active"}}},
                    {"postcondition", {{"mode", "combat_active"}}}, {"position", {450, 800}}}}},
                J{{"type", "wait"}, {"label", "等待"}, {"category", "控制"},
                  {"defaults", {{"duration_ms", 500}}}},
                J{{"type", "business"}, {"label", "战斗子流程"}, {"category", "业务"},
                  {"defaults", {{"binding", "combat"}}}},
                J{{"type", "end"}, {"label", "成功结束"}, {"category", "控制"},
                  {"defaults", {{"outcome", "success"}}}}
            })},
            {"templates", templates}, {"recognizers", options({
                J{{"value", "combat_active"}, {"label", "战斗中"}},
                J{{"value", "skill_level"}, {"label", "技能等级"}},
                J{{"value", "prepared_actor"}, {"label", "本次行动角色"}},
                J{{"value", "skill_target"}, {"label", "本次技能目标"}},
                J{{"value", "input_clear"}, {"label", "局部输入可操作"}},
                J{{"value", "pause"}, {"label", "Pause"}},
                J{{"value", "target_marker"}, {"label", "目标标记"}},
                J{{"value", "next_low_confidence"}, {"label", "NEXT 低阈值"}}
            })},
            {"business_nodes", options({
                J{{"value", "combat"}, {"label", "战斗"}},
                J{{"value", "chest"}, {"label", "开箱"}},
                J{{"value", "confirm"}, {"label", "业务确认"}}
            })},
            {"semantic_resources", [&] {
                J result = J::array();
                const auto resources = semantic_catalogue_.value("resources", J::object());
                for (const auto &[id, entry] : resources.items()) {
                    J locales = J::array();
                    J methods = J::object();
                    const auto variants = entry.value("variants", J::object());
                    for (const auto &[locale, variant] : variants.items()) {
                        locales.push_back(locale);
                        const auto algorithm = variant.at("condition").value("mode", "");
                        J available = J::array({algorithm});
                        const auto alternatives = variant.value("alternatives", J::object());
                        for (const auto &[method, recipe] : alternatives.items()) {
                            (void)recipe;
                            if (method != algorithm) available.push_back(method);
                        }
                        methods[locale] = {{"default", algorithm}, {"available", available}};
                    }
                    result.push_back({{"value", id}, {"label", entry.value("label", id)},
                        {"category", entry.value("category", "未分类")}, {"role", entry.value("role", "observation")},
                        {"locales", locales}, {"methods", methods}});
                }
                return result;
            }()},
            {"roles", roles},
            {"skills", options({J{{"value", "左上技能"}, {"label", "左上技能"}},
                                  J{{"value", "右上技能"}, {"label", "右上技能"}},
                                  J{{"value", "左下技能"}, {"label", "左下技能"}},
                                  J{{"value", "右下技能"}, {"label", "右下技能"}},
                                  J{{"value", "防御"}, {"label", "防御"}}})},
            {"skill_levels", options({J{{"value", 1}, {"label", "1"}},
                                       J{{"value", 2}, {"label", "2"}},
                                       J{{"value", 3}, {"label", "3"}},
                                       J{{"value", 4}, {"label", "4"}},
                                       J{{"value", 5}, {"label", "5"}},
                                       J{{"value", 6}, {"label", "6"}},
                                       J{{"value", 7}, {"label", "7"}}})},
            {"skill_targets", options({J{{"value", "左上角色"}, {"label", "左上角色"}},
                                         J{{"value", "中上角色"}, {"label", "中上角色"}},
                                         J{{"value", "右上角色"}, {"label", "右上角色"}},
                                         J{{"value", "左下角色"}, {"label", "左下角色"}},
                                         J{{"value", "中下角色"}, {"label", "中下角色"}},
                                         J{{"value", "右下角色"}, {"label", "右下角色"}}})},
            {"skill_frequencies", options({J{{"value", "用完后移除"}, {"label", "用完后移除"}},
                J{{"value", "重复"}, {"label", "重复该动作"}}})},
            {"chest_openers", options({J{{"value", 0}, {"label", "随机"}},
                                        J{{"value", 1}, {"label", "左上"}},
                                        J{{"value", 2}, {"label", "中上"}},
                                        J{{"value", 3}, {"label", "右上"}},
                                        J{{"value", 4}, {"label", "左下"}},
                                        J{{"value", 5}, {"label", "中下"}},
                                        J{{"value", 6}, {"label", "右下"}}})},
            {"karma_directions", options({J{{"value", "+0"}, {"label", "不调整"}},
                                           J{{"value", "+1"}, {"label", "善 +1"}},
                                           J{{"value", "-1"}, {"label", "恶 -1"}}})},
            {"strategy_reload_timings", options({J{{"value", "不需要"}, {"label", "不需要"}},
                                                   J{{"value", "每场战斗前"}, {"label", "每场战斗前"}},
                                                   J{{"value", "每次副本开始"}, {"label", "每次副本开始"}}})}};
}

Application::J Application::save_profile(const J &request) {
    require(request.is_object(), "PROFILE_REQUEST_INVALID");
    const auto expected = request.at("revision").get<std::string>();
    auto document = profile_store_->load();
    auto requested_values = request.value("operation", "") == "clear_task_override" ? document.at("values")
        : request.contains("profile") ? request.at("profile") : request.at("document").at("values");
    if (request.contains("scope")) {
        require(request.at("scope").is_string(), "PROFILE_SCOPE_INVALID");
        const auto scope = request.at("scope").get<std::string>();
        const auto sections = profile_save_sections();
        require(sections.contains(scope) && !request.contains("operation"), "PROFILE_SCOPE_INVALID");
        const auto &fields = sections.at(scope);
        require(requested_values.is_object() && requested_values.size() == fields.size(), "PROFILE_SCOPE_FIELDS_INVALID");
        auto merged = document.at("values");
        if (scope == "task") {
            const auto task = optional_profile_text(requested_values, "FARM_TARGET");
            if (!task.empty() && task != optional_profile_text(merged, "FARM_TARGET"))
                merged = effective_profile_values(task, document);
        }
        // 局部保存由服务端合并到CAS基线；浏览器不能夹带其它区域的草稿。
        for (const auto &field : fields) {
            const auto name = field.get<std::string>();
            require(requested_values.contains(name), "PROFILE_SCOPE_FIELDS_INVALID");
            merged[name] = requested_values.at(name);
        }
        require(scope == "advanced" || !request.contains("logging"), "PROFILE_SCOPE_LOGGING_INVALID");
        require(scope == "combat" || !request.contains("strategy_renames"), "PROFILE_SCOPE_RENAME_INVALID");
        if (scope == "common") {
            const auto names = strategy_names(document.at("values"));
            std::set<std::string> existing;
            strategy_references(document["values"], [&](J &reference) {
                if (reference.is_string()) existing.insert(reference.get<std::string>());
            });
            strategy_references(merged, [&](J &reference) {
                if (!reference.is_string()) return;
                const auto name = reference.get<std::string>();
                require(name.empty() || name == "全自动战斗" || name == "自定义任务点策略" ||
                    names.contains(name) || existing.contains(name), "PROFILE_STRATEGY_NOT_SAVED");
            });
        }
        requested_values = std::move(merged);
    }
    if (request.contains("logging"))
        document["logging"] = storage::LoggingPolicy::parse(request.at("logging")).json();
    const auto old_names = strategy_names(document.at("values"));
    if (request.contains("strategy_renames")) {
        const auto &renames = request.at("strategy_renames");
        require(renames.is_object() && renames.size() <= 128, "STRATEGY_RENAME_INVALID");
        const auto desired_names = strategy_names(requested_values);
        std::set<std::string> targets;
        for (const auto &[old_name, new_name] : renames.items()) {
            require(old_names.contains(old_name) && new_name.is_string() &&
                    desired_names.contains(new_name.get<std::string>()) &&
                    targets.insert(new_name.get<std::string>()).second, "STRATEGY_RENAME_INVALID");
        }
        // 同时重绑定，不全局替换字符串。隐藏任务覆盖也必须更新，未知字段原样保留。
        all_profile_references(document, [&](J &reference) {
            if (reference.is_string() && renames.contains(reference.get<std::string>()))
                reference = renames.at(reference.get<std::string>());
        });
        if (request.value("scope", "") == "combat")
            strategy_references(requested_values, [&](J &reference) {
                if (reference.is_string() && renames.contains(reference.get<std::string>()))
                    reference = renames.at(reference.get<std::string>());
            });
    }
    if (request.value("operation", "") == "clear_task_override") {
        const auto task = request.at("task_id").get<std::string>();
        if (document.contains("task_overrides"))
            document["task_overrides"].erase(task);
        auto values = document.value("default_values", document.at("values"));
        values["FARM_TARGET"] = task;
        values["FARM_TARGET_TEXT"] = catalog_->at(task).source.value("questName", task);
        values["TASK_SPECIFIC_CONFIG"] = false;
        document["values"] = std::move(values);
    } else {
        const auto &values = requested_values;
        require(values.is_object(), "PROFILE_VALUES_INVALID");
        auto defaults = document.value("default_values", document.at("values"));
        const auto task = optional_profile_text(values, "FARM_TARGET");
        const bool task_specific = values.value("TASK_SPECIFIC_CONFIG", false) && !task.empty();
        if (task_specific) {
            J task_values = document.value("task_overrides", J::object()).value(task, J::object());
            for (const auto &field : descriptor_.at("fields")) {
                const auto name = field.at("name").get<std::string>();
                if (field.at("category").get<std::string>() == "TEMPLATE")
                    task_values[name] = values.at(name);
                else
                    defaults[name] = values.at(name);
            }
            defaults["TASK_SPECIFIC_CONFIG"] = false;
            document["task_overrides"][task] = task_values;
            auto effective = defaults;
            for (const auto &[name, value] : task_values.items())
                effective[name] = value;
            effective["FARM_TARGET"] = task;
            effective["FARM_TARGET_TEXT"] = values.at("FARM_TARGET_TEXT");
            effective["TASK_SPECIFIC_CONFIG"] = true;
            document["values"] = std::move(effective);
        } else {
            const bool was_task_specific = document.at("values").value(
                "TASK_SPECIFIC_CONFIG", false);
            if (was_task_specific) {
                // 从任务覆盖切回默认时，合并视图中的模板字段仍是任务值，不能反写污染默认。
                for (const auto &field : descriptor_.at("fields")) {
                    const auto name = field.at("name").get<std::string>();
                    if (field.at("category").get<std::string>() != "TEMPLATE")
                        defaults[name] = values.at(name);
                }
            } else {
                // 原本就在编辑默认配置，模板字段也是用户本次明确修改的默认值。
                defaults = values;
            }
            defaults["TASK_SPECIFIC_CONFIG"] = false;
            document["values"] = defaults;
        }
        document["default_values"] = std::move(defaults);
    }
    const auto new_names = strategy_names(document.at("values"));
    (void)portrait_bundle(document.at("values"), false);
    const auto &points = document.at("values").at("TASK_POINT_STRATEGY");
    if (points.contains("special_combat")) {
        const auto &special = points.at("special_combat");
        require(special.is_object(), "SPECIAL_COMBAT_INVALID");
        const bool enabled = special.value("skull", false) || special.value("portrait", false);
        if (enabled) {
            for (const auto *field : {"normal_strategy", "special_strategy"})
                if (std::string(field) != "special_strategy" || special.value("skull", false) ||
                    games::combat::enemy_rules(document.at("values")).empty())
                require(special.contains(field) && special.at(field).is_string() &&
                    new_names.contains(special.at(field).get<std::string>()),
                    "SPECIAL_COMBAT_STRATEGY_INVALID");
            if (special.value("portrait", false) && games::combat::enemy_rules(document.at("values")).empty()) {
                require(special.contains("portrait_image") && special.at("portrait_image").is_string() &&
                    available_images_.contains(special.at("portrait_image").get<std::string>() + ".png"),
                    "SPECIAL_COMBAT_PORTRAIT_MISSING");
            }
        }
    }
    all_profile_references(document, [&](J &reference) {
        if (reference.is_string()) {
            const auto name = reference.get<std::string>();
            require(!old_names.contains(name) || new_names.contains(name), "STRATEGY_STILL_REFERENCED");
        }
    });
    const auto saved = profile_store_->compare_exchange(expected, document);
    const auto &values = saved.at("values");
    const auto task = optional_profile_text(values, "FARM_TARGET");
    const bool task_specific = values.value("TASK_SPECIFIC_CONFIG", false);
    return {{"profile", values}, {"logging", storage::LoggingPolicy::from_profile(saved).json()},
            {"revision", saved.at("revision")},
            {"effective_source", task_specific ? "任务覆盖" : "默认配置"},
            {"task_override_active", task_specific && !task.empty() &&
                 saved.value("task_overrides", J::object()).contains(task)}};
}

void Application::start_device_job(std::string name, std::function<void()> job) {
    std::lock_guard command(command_mutex_);
    {
        std::lock_guard lock(mutex_);
        require(!stopping_, "APPLICATION_STOPPING");
        require(!run_active(), "RUN_ACTIVE");
        require(!task_session_active_, "TASK_SESSION_ACTIVE");
        require(!handoff_status_.is_object() ||
            (handoff_status_.value("state", "") != "watching" &&
             handoff_status_.value("state", "") != "starting"),
            "TASK_HANDOFF_IN_PROGRESS");
        require(operation_.value("state", "idle") != "running", "DEVICE_OPERATION_BUSY");
    }
    if (device_worker_.joinable()) device_worker_.join();
    cancel_operation_ = false;
    worker_publication_failed_ = false;
    {
        std::lock_guard lock(mutex_);
        operation_ = {{"state", "running"}, {"name", name}, {"error", nullptr}};
    }
    try {
        device_worker_ = std::jthread([this, name = std::move(name), job = std::move(job)]() noexcept {
          try {
            std::string failure;
            try {
                if (stopping_ || cancel_operation_) throw std::runtime_error("PREPARATION_CANCELLED");
                job();
            } catch (const std::exception &error) { failure = error.what(); }
              catch (...) { failure = "APPLICATION_OPERATION_EXCEPTION"; }
            J receipt, intent;
            {
                std::lock_guard finished(mutex_);
                if (name == "start_task" || name == "start_workflow" || name == "start_combat_debug") {
                    receipt = submission_; intent = submission_intent_;
                    receipt["state"] = failure.empty() ? "submitted" :
                        failure == "PREPARATION_CANCELLED" ? "cancelled" : "failed";
                    receipt["error"] = failure.empty() ? J(nullptr) : J(failure);
                }
            }
            // Keep admission busy until the durable receipt exists, but never
            // hold the status/stop mutex while performing storage I/O.
            if (!receipt.is_null()) submission_store_->save(receipt.at("request_id"), intent, receipt, true);
            {
                std::lock_guard finished(mutex_);
                auto preparation = operation_.value("preparation", J::object());
                operation_ = {{"state", failure.empty() ? "completed" : "failed"},
                              {"name", name}, {"error", failure.empty() ? J(nullptr) : J(failure)}};
                if (!preparation.empty()) operation_["preparation"] = std::move(preparation);
                if (!receipt.is_null()) submission_ = std::move(receipt);
            }
          } catch (...) {
            worker_publication_failed_ = true;
            cancel_operation_ = true;
            try { coordinator_->request_stop(); } catch (...) {}
            OutputDebugStringW(L"WVD application worker publication failed; stop requested\n");
          }
        });
    } catch (...) {
        std::lock_guard lock(mutex_);
        operation_ = {{"state", "failed"}, {"name", "worker"}, {"error", "WORKER_START_FAILED"}};
        throw;
    }
}

void Application::require_storage_space() const {
    std::uintmax_t available;
    try {
        available = space_query_(paths_.data_root).available;
    } catch (...) {
        throw std::runtime_error("RUN_STORAGE_SPACE_QUERY_FAILED");
    }
    require(available != static_cast<std::uintmax_t>(-1), "RUN_STORAGE_SPACE_QUERY_FAILED");
    // Round ceiling: diagnostics (176 * 8 MiB), recent images (128 MiB),
    // logs/timing/events (144 MiB), plus 256 MiB terminal/input reserve.
    require(available >= 2ULL * 1024 * 1024 * 1024, "RUN_STORAGE_SPACE_LOW");
}

Application::J Application::queue_run(const std::string &kind, const J &request,
                                      const J &identity, std::function<J()> prepare, std::uint64_t stop_epoch) {
    std::lock_guard command(command_mutex_);
    const auto id = checked_request_id(request);
    if (auto prior = submission_store_->replay(id, identity)) return *prior;
    require_storage_space();
    {
        std::lock_guard lock(mutex_);
        // 已登记请求先返回幂等回执；容量仅阻止新意图，不能阻止读取旧结果。
        require(!stopping_, "APPLICATION_STOPPING");
        require(stop_epoch_.load() == stop_epoch, "PREPARATION_CANCELLED");
        require(!run_active(), "RUN_ACTIVE");
        require(operation_.value("state", "idle") != "running", "DEVICE_OPERATION_BUSY");
        require(!task_session_active_, "TASK_SESSION_ACTIVE");
        submission_ = {{"request_id", id}, {"kind", kind}, {"state", "preparing"},
                       {"error", nullptr}, {"accepted", true}};
        submission_intent_ = identity;
    }
    bool registered = false;
    try {
        submission_store_->save(id, identity, submission_, false);
        registered = true;
        start_device_job(kind, [this, id, stop_epoch, prepare = std::move(prepare)] {
            require(stop_epoch_.load() == stop_epoch, "PREPARATION_CANCELLED");
            const auto result = prepare();
            J receipt, intent;
            { std::lock_guard lock(mutex_);
              submission_["run_id"] = result.at("run_id");
              receipt = submission_; intent = submission_intent_; }
            submission_store_->save(id, intent, receipt, true);
        });
    } catch (const std::exception &error) {
        J receipt;
        { std::lock_guard lock(mutex_);
          submission_["state"] = "failed"; submission_["error"] = error.what(); receipt = submission_; }
        if (registered) submission_store_->save(id, identity, receipt, true);
        throw;
    }
    return {{"accepted", true}, {"request_id", id}, {"submission_state", "preparing"}};
}

std::optional<recognition::Bundle> Application::portrait_bundle(const J &values, bool persist) const {
    std::map<std::string, std::vector<std::uint8_t>> images;
    for (const auto &rule : games::combat::enemy_rules(values)) {
        if (rule.png.empty()) {
            require(available_images_.contains(rule.image + ".png"), "ENEMY_PORTRAIT_MISSING");
            continue;
        }
        auto bytes = decode_base64(rule.png);
        constexpr std::array<std::uint8_t, 8> signature{137, 80, 78, 71, 13, 10, 26, 10};
        require(bytes.size() >= 24 && bytes.size() <= 262144 &&
            std::equal(signature.begin(), signature.end(), bytes.begin()) &&
            bytes[12] == 'I' && bytes[13] == 'H' && bytes[14] == 'D' && bytes[15] == 'R',
            "ENEMY_PORTRAIT_PNG_REQUIRED");
        // Bound the decoded allocation before handing compressed user input to OpenCV.
        const auto dimension = [&](std::size_t offset) {
            return (std::uint32_t(bytes[offset]) << 24) | (std::uint32_t(bytes[offset + 1]) << 16) |
                (std::uint32_t(bytes[offset + 2]) << 8) | std::uint32_t(bytes[offset + 3]);
        };
        require(dimension(16) >= 16 && dimension(16) <= 160 && dimension(20) >= 16 && dimension(20) <= 220,
            "ENEMY_PORTRAIT_SIZE_INVALID");
        const auto pixels = cv::imdecode(bytes, cv::IMREAD_COLOR);
        require(!pixels.empty() && pixels.cols >= 16 && pixels.rows >= 16 &&
            pixels.cols <= 160 && pixels.rows <= 220, "ENEMY_PORTRAIT_SIZE_INVALID");
        const auto hash = platform::bytes_sha256(bytes);
        require(rule.image == "custom/monster_" + hash, "ENEMY_PORTRAIT_ID_MISMATCH");
        images.emplace("image/" + rule.image + ".png", std::move(bytes));
    }
    if (images.empty()) return std::nullopt;
    J identity = J::object();
    recognition::Bundle result;
    for (const auto &[path, bytes] : images) {
        const auto hash = platform::bytes_sha256(bytes);
        result.files.push_back({path, hash}); identity[path] = hash;
    }
    const auto material = identity.dump();
    result.revision = platform::bytes_sha256({reinterpret_cast<const std::uint8_t *>(material.data()), material.size()});
    result.root = paths_.data_root / "asset-cache" / ("portraits-" + result.revision);
    result.snapshot_parent = paths_.data_root / "active-snapshots";
    if (!persist) return result;
    if (!std::filesystem::exists(result.root)) {
        const auto staging = result.root.parent_path() / ("portraits-tmp-" + platform::unique_id());
        std::filesystem::create_directories(staging);
        try {
            for (const auto &[path, bytes] : images) {
                const auto target = staging / platform::BundleLease::checked_relative(path);
                std::filesystem::create_directories(target.parent_path());
                std::ofstream out(target, std::ios::binary);
                out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                out.close(); require(bool(out), "ENEMY_PORTRAIT_WRITE_FAILED");
            }
            std::filesystem::rename(staging, result.root);
        } catch (...) {
            std::error_code ignored; std::filesystem::remove_all(staging, ignored); throw;
        }
    }
    platform::BundleLease::Manifest members;
    for (const auto &file : result.files) members.emplace(file.relative_path, file.sha256);
    result.lease = std::make_shared<platform::BundleLease>(result.root, result.revision, members);
    result.lease->verify_members();
    return result;
}

Application::J Application::start_combat_debug(const J &request) {
    std::lock_guard command(command_mutex_);
    const auto stop_epoch = stop_epoch_.load();
    const J intent{{"kind", "combat_debug"}, {"request", request}};
    if (auto prior = submission_store_->replay(checked_request_id(request), intent)) return *prior;
    const auto stored = profile_store_->load();
    require(request.at("profile_revision") == stored.at("revision"), "PROFILE_REVISION_MISMATCH");
    const auto name = request.at("strategy_name").get<std::string>();
    require(strategy_names(stored.at("values")).contains(name), "COMBAT_DEBUG_STRATEGY_MISSING");
    auto frozen = request;
    frozen["request_id"] = checked_request_id(request);
    frozen["resource_locale"] = authoring::effective_resource_locale(request, J::object());
    frozen["mode"] = "combat_debug";
    frozen["revision"] = stored.at("revision");
    auto debug_stored = stored;
    auto &values = debug_stored["values"];
    values["TASK_SPECIFIC_CONFIG"] = false;
    values["DEFAULT_OVERALL_STRATEGY"] = name;
    values["TASK_POINT_STRATEGY"]["special_combat"] = J::object();
    const auto document = games::combat::debug_document(stored.at("revision"));
    // Only saved public dependencies need repository CAS; the debug root is deliberately transient.
    auto library = workflow_store_->snapshot_closure(
        workflow_store_->read(games::tasks::native_public_steps.front()), games::tasks::native_public_steps);
    library[document.at("flow").at("id").get<std::string>()] = document;
    return queue_run("start_combat_debug", frozen,
        intent,
        [this, frozen, stored, debug_stored, document, library] {
            auto prepared = compile_workflow_graph(frozen, debug_stored, document, library);
            std::shared_ptr<devices::DeviceConnection> backend;
            { std::lock_guard lock(mutex_); backend = backend_; }
            if (!backend) {
                const auto &values = stored.at("values");
                const auto binding = platform::create_mumu_binding(manager_from_path(
                    platform::path_from_utf8(values.at("EMU_PATH"))), values.at("EMU_INDEX"), values.at("ADB_ADRESS"));
                require(binding.at("initial_manager").value("is_android_started", false), "COMBAT_DEBUG_EMULATOR_NOT_RUNNING");
                backend = ensure_connected_for_run(stored);
            }
            require(backend->connect(), "DEVICE_RECONNECT_FAILED");
            auto frame = backend->capture_preview();
            require(frame.foreground_application == "jp.co.drecom.wizardry.daphne" &&
                frame.size == contracts::Size{900, 1600}, "COMBAT_DEBUG_NOT_IN_BATTLE");
            if (frame.encoded.empty()) {
                require(frame.raw_bgr && frame.raw_bgr->size() == 900u * 1600u * 3u, "COMBAT_DEBUG_FRAME_INVALID");
                const cv::Mat pixels(1600, 900, CV_8UC3, const_cast<std::uint8_t *>(frame.raw_bgr->data()));
                require(cv::imencode(".png", pixels, frame.encoded), "COMBAT_DEBUG_FRAME_INVALID");
            }
            { std::lock_guard lock(mutex_);
              frame_png_ = std::move(frame.encoded); frame_captured_at_ = frame.captured_at;
              frame_info_ = {{"width", frame.size.width}, {"height", frame.size.height},
                  {"device_id", frame.device_id}, {"viewport", frame.viewport_id}, {"backend", frame.backend},
                  {"connection_generation", frame.connection_generation}, {"foreground_application", frame.foreground_application}}; }
            const auto observed = recognition_probe({{"recognition", {{"mode", "combat_active"}}},
                {"resource_locale", frozen.at("resource_locale")}});
            require(observed.at("outcome") == "Hit", "COMBAT_DEBUG_NOT_IN_BATTLE");
            return prepare_workflow("combat-debug", frozen, debug_stored, document, backend, library, std::move(prepared));
        }, stop_epoch);
}

Application::J Application::start_task(const J &request) {
    std::lock_guard command(command_mutex_);
    const auto stop_epoch = stop_epoch_.load();
    const J intent{{"kind", "task"}, {"request", request}};
    if (auto prior = submission_store_->replay(checked_request_id(request), intent)) return *prior;
    auto frozen = request;
    frozen["request_id"] = checked_request_id(request);
    const auto stored = profile_store_->load();
    frozen["resource_locale"] = authoring::effective_resource_locale(frozen, J::object());
    require(!request.contains("repeat") || request.at("repeat").is_boolean(), "REPEAT_INVALID");
    if (request.contains("repeat_count")) {
        require(request.value("repeat", false) && request.at("repeat_count").is_number_integer(), "REPEAT_COUNT_INVALID");
        const auto count = request.at("repeat_count").get<std::int64_t>();
        require(count >= 1 && count <= 1000000, "REPEAT_COUNT_INVALID");
    }
    // 两条单目标悬赏共用三段业务结算与清理契约。
    if (request.value("repeat", false))
        require(request.value("task_id", stored.at("values").value("FARM_TARGET", "")) == "Scorpionesses" ||
                request.value("task_id", stored.at("values").value("FARM_TARGET", "")) == "GiantBounty",
                "REPEAT_TASK_UNSUPPORTED");
    if (request.contains("profile_revision"))
        require(request.at("profile_revision") == stored.at("revision"), "PROFILE_REVISION_MISMATCH");
    return queue_run("start_task", frozen,
        intent,
        [this, frozen, stored] {
            const auto &selected = stored.at("values").at("FARM_TARGET");
            const auto task_id = frozen.value("task_id", selected.is_string()
                ? selected.get<std::string>() : std::string{});
            const auto source_values = effective_profile_values(task_id, stored);
            auto prepared = compile_task_graph(frozen, catalog_->at(task_id), source_values,
                                               preparation_observer(stored));
            const auto portraits = portrait_bundle(source_values);
            for (const auto &image : prepared.images) {
                const auto selected_image = games::vision::resolve_image_source(
                    author_bundle_, aliases_, image, portraits ? &*portraits : nullptr);
                if (!std::any_of(selected_image.bundle->files.begin(), selected_image.bundle->files.end(),
                    [&](const auto &file) { return file.relative_path == selected_image.relative_path; }))
                    throw std::runtime_error("NATIVE_IMAGE_MISSING:" + selected_image.relative_path);
            }
            const auto backend = ensure_connected_for_run(stored);
            auto started = std::make_shared<std::atomic<int>>(0);
            // Register the sole join/batch owner before coordinator submission.
            // Cancellation cannot strand a run between start and watcher creation.
            watch_task_session(frozen, stored, source_values, backend, frozen.at("request_id"), started);
            try {
                auto result = prepare_task(frozen, stored, backend, source_values, nullptr, std::move(prepared));
                started->store(1);
                return result;
            } catch (...) { started->store(2); throw; }
        }, stop_epoch);
}
Application::J Application::start_workflow(const std::string &flow_id, const J &request) {
    std::lock_guard command(command_mutex_);
    const auto stop_epoch = stop_epoch_.load();
    const J intent{{"kind", "workflow"}, {"flow_id", flow_id}, {"request", request}};
    if (auto prior = submission_store_->replay(checked_request_id(request), intent)) return *prior;
    require(request.value("mode", "workflow") == "workflow" || request.value("mode", "workflow") == "selected_node", "WORKFLOW_MODE_INVALID");
    auto frozen = request;
    frozen["request_id"] = checked_request_id(request);
    const auto stored = profile_store_->load();
    if (request.contains("profile_revision"))
        require(request.at("profile_revision") == stored.at("revision"), "PROFILE_REVISION_MISMATCH");
    const auto document = workflow_store_->read(flow_id);
    require(frozen.value("revision", std::string{}) == document.at("revision").get<std::string>(),
            "WORKFLOW_REVISION_MISMATCH");
    frozen["resource_locale"] = authoring::effective_resource_locale(frozen, document.at("execution"));
    const auto library = workflow_store_->snapshot_closure(document, games::tasks::native_public_steps);
    return queue_run("start_workflow", frozen,
        intent,
        [this, flow_id, frozen, stored, document, library] {
            // 不齐全的语言素材/循环引用在连接和启动模拟器之前暴露。
            auto prepared = compile_workflow_graph(frozen, stored, document, library);
            require(!stopping_ && !cancel_operation_, "PREPARATION_CANCELLED");
            const auto backend = ensure_connected_for_run(stored);
            return prepare_workflow(flow_id, frozen, stored, document, backend, library, std::move(prepared));
        }, stop_epoch);
}

Application::J Application::connect_device(const J &request) {
    std::lock_guard command(command_mutex_);
    require(request.is_object(), "DEVICE_REQUEST_INVALID");
    {
        std::lock_guard lock(mutex_);
        require(!backend_, "DEVICE_ALREADY_CONNECTED");
    }
    start_device_job("connect", [this, request] { connect_selected_device(request); });
    return device_status();
}

void Application::connect_selected_device(const J &request) {
    require(request.is_object(), "DEVICE_REQUEST_INVALID");
    require(!stopping_ && !cancel_operation_, "PREPARATION_CANCELLED");
    const auto path = platform::path_from_utf8(request.contains("emulator_path")
                                                ? request.at("emulator_path") : request.at("path"));
    const auto manager = manager_from_path(path);
    const auto index = request.value("emulator_index", request.value("index", 0));
    const auto serial = request.value("adb_address", request.value("serial", std::string{}));
    require(!serial.empty(), "ADB_ADDRESS_REQUIRED");
    const bool vpn = request.value("vpn_required", request.value("auto_start_clash", false));
    const auto bin = executable_directory();
    const auto capture_host = bin / "wvd-capture-host.exe";
    const auto scrcpy_server = bin / "scrcpy-server-v3.3.4";
    require(std::filesystem::is_regular_file(capture_host) &&
            std::filesystem::is_regular_file(scrcpy_server),
            "NATIVE_DEVICE_RUNTIME_MISSING");
    // 在可能启动选定实例之前取得唯一控制权；不启动其他实例、不执行 restart。
    auto lease = std::make_unique<platform::DeviceLease>(serial);
    auto binding = platform::create_mumu_binding(manager, index, serial);
    if (!binding.at("initial_manager").value("is_android_started", false)) {
        require(!stopping_ && !cancel_operation_, "PREPARATION_CANCELLED");
        platform::launch_selected_instance(path, index);
        const auto deadline = std::chrono::steady_clock::now() + 120s;
        do {
            // 准备期停止不必等一整秒才被本层察觉；不伪称可以撤回已启动的进程。
            for (int i = 0; i < 20 && !stopping_ && !cancel_operation_; ++i)
                std::this_thread::sleep_for(50ms);
            require(!stopping_ && !cancel_operation_, "PREPARATION_CANCELLED");
            binding = platform::create_mumu_binding(manager, index, serial);
            if (binding.at("initial_manager").value("is_android_started", false)) break;
        } while (std::chrono::steady_clock::now() < deadline);
        require(binding.at("initial_manager").value("is_android_started", false), "MUMU_START_TIMEOUT");
    }
    binding["launcher"] = platform::utf8(path);
    binding["application_id"] = "jp.co.drecom.wizardry.daphne";
    binding["vpn_required"] = vpn;
    auto next = std::make_shared<devices::DeviceSession>(
        std::move(binding), capture_host, scrcpy_server);
    require(!stopping_ && !cancel_operation_, "PREPARATION_CANCELLED");
    require(next->connect(), "DEVICE_CONNECT_FAILED");
    require(!stopping_ && !cancel_operation_, "PREPARATION_CANCELLED");
    auto frame = next->capture_preview();
    std::lock_guard connected(mutex_);
    require(!stopping_ && !cancel_operation_, "PREPARATION_CANCELLED");
    require(!backend_, "DEVICE_ALREADY_CONNECTED");
    backend_ = std::move(next);
    preview_lease_ = std::move(lease);
    frame_png_ = std::move(frame.encoded);
    frame_captured_at_ = frame.captured_at;
    frame_info_ = {{"width", frame.size.width}, {"height", frame.size.height},
                   {"device_id", frame.device_id}, {"viewport", frame.viewport_id},
                   {"foreground_application", frame.foreground_application},
                   {"backend", frame.backend}, {"connection_generation", frame.connection_generation},
                   {"display_rotation", frame.display_rotation}};
}

std::shared_ptr<devices::DeviceConnection> Application::ensure_connected_for_run(const J &stored) {
    require(!stopping_ && !cancel_operation_, "PREPARATION_CANCELLED");
    std::shared_ptr<devices::DeviceConnection> backend;
    { std::lock_guard lock(mutex_); backend = backend_; }
    if (backend && backend->offline()) return backend;
    const auto &values = stored.at("values"); // 与提交身份相同的冻结版本，不在准备中重读编辑中的配置。
    const auto path = platform::path_from_utf8(values.at("EMU_PATH"));
    const auto manager = manager_from_path(path);
    const auto index = values.at("EMU_INDEX").get<int>();
    const auto serial = values.at("ADB_ADRESS").get<std::string>();
    if (backend) {
        require(backend->matches_selection(manager, index, serial), "DEVICE_BINDING_CHANGED_RECONNECT_REQUIRED");
        require(coordinator_->snapshot().quiescent && coordinator_->collect_finished_worker(),
                "DEVICE_PREPARATION_NOT_QUIESCENT");
        auto *port = backend->lifecycle_port();
        require(port != nullptr, "DEVICE_START_OBSERVATION_UNAVAILABLE");
        const auto target = backend->lifecycle_target();
        const auto observation = [&] {
            backend->observation_window(std::chrono::steady_clock::now() + 10s, {});
            try {
                auto result = port->observe_lifecycle();
                // Preparation can outlive this read. Do not carry its deadline
                // into publication, the initial lifecycle plan, or previews.
                backend->observation_window({}, {});
                return result;
            } catch (...) {
                backend->observation_window({}, {});
                throw;
            }
        }();
        require(!stopping_ && !cancel_operation_, "PREPARATION_CANCELLED");
        const auto now = std::chrono::steady_clock::now();
        require(observation && observation->target.device_id == target.device_id &&
            observation->target.instance_id == target.instance_id &&
            observation->target.application_id == target.application_id &&
            observation->target.vpn_application_id == target.vpn_application_id &&
            observation->target.vpn_required == target.vpn_required &&
            observation->observed_at <= now && now - observation->observed_at <= 2s &&
            (!observation->application_foreground || observation->application_running) &&
            (!observation->connected || (observation->instance_running && observation->connection_generation)) &&
            (!observation->instance_exited || (!observation->instance_running && !observation->connected)),
            "DEVICE_START_OBSERVATION_INVALID");
        if (!observation->instance_exited) {
            require(observation->instance_running && observation->connected,
                    "DEVICE_START_OBSERVATION_UNCONFIRMED");
            return backend;
        }
        // Only explicit manager exit evidence authorizes disposal/reconnection.
        // ADB offline or a failed observation never enters this branch.
        disconnect_selected_device(backend);
        backend.reset();
        require(!stopping_ && !cancel_operation_, "PREPARATION_CANCELLED");
    }
    // 在现有准备作业中同步调用同一连接实现，不再嵌套第二个作业或第二个控制器。
    device_connector_({{"emulator_path", platform::utf8(path)}, {"emulator_index", index},
                             {"adb_address", serial}, {"auto_start_clash", values.at("AUTO_START_CLASH")}});
    require(!stopping_ && !cancel_operation_, "PREPARATION_CANCELLED");
    std::lock_guard lock(mutex_);
    require(bool(backend_), "DEVICE_CONNECT_FAILED");
    return backend_;
}

Application::J Application::select_emulator_path() const {
    std::wstring selected(32768, L'\0');
    const auto current = optional_profile_text(profile().at("profile"), "EMU_PATH");
    if (!current.empty()) {
        const auto wide = platform::path_from_utf8(current).wstring();
        std::copy_n(wide.c_str(), std::min(wide.size(), selected.size() - 1), selected.data());
    }
    constexpr wchar_t filter[] = L"MuMu 启动程序 (MuMuNxDevice.exe)\0MuMuNxDevice.exe\0"
                                 L"可执行文件 (*.exe)\0*.exe\0\0";
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = selected.data();
    dialog.nMaxFile = static_cast<DWORD>(selected.size());
    dialog.lpstrTitle = L"选择 MuMu 安卓设备启动程序";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog)) {
        const auto error = CommDlgExtendedError();
        require(error == 0, "EMULATOR_FILE_DIALOG_FAILED");
        return {{"cancelled", true}, {"path", nullptr}};
    }
    selected.resize(std::wcslen(selected.c_str()));
    const auto path = std::filesystem::path(selected);
    require(path.filename() == "MuMuNxDevice.exe" && std::filesystem::is_regular_file(path),
            "MUMU_LAUNCHER_REQUIRED");
    return {{"cancelled", false}, {"path", platform::utf8(path)}};
}

Application::J Application::disconnect_device() {
    start_device_job("disconnect", [this] {
        std::shared_ptr<devices::DeviceConnection> old;
        {
            std::lock_guard lock(mutex_);
            old = backend_;
        }
        disconnect_selected_device(old);
    });
    return device_status();
}
void Application::disconnect_selected_device(const std::shared_ptr<devices::DeviceConnection> &old) {
    if (old) old->disconnect(); // Failure retains the old owner and lease.
    std::lock_guard lock(mutex_);
    require(backend_ == old, "DEVICE_OWNER_CHANGED");
    backend_.reset();
    preview_lease_.reset();
    frame_png_.clear();
    frame_info_ = nullptr;
    frame_captured_at_.reset();
}

Application::J Application::capture_device() {
    start_device_job("capture", [this] {
        std::shared_ptr<devices::DeviceConnection> backend;
        {
            std::lock_guard lock(mutex_);
            backend = backend_;
            if (backend && !preview_lease_)
                preview_lease_ = std::make_unique<platform::DeviceLease>(
                    backend->lifecycle_target().device_id);
        }
        require(bool(backend), "DEVICE_NOT_CONNECTED");
        require(backend->connect(), "DEVICE_RECONNECT_FAILED");
        auto frame = backend->capture_preview();
        std::lock_guard captured(mutex_);
        frame_png_ = std::move(frame.encoded);
        frame_captured_at_ = frame.captured_at;
        frame_info_ = {{"width", frame.size.width}, {"height", frame.size.height},
                       {"device_id", frame.device_id}, {"viewport", frame.viewport_id},
                       {"foreground_application", frame.foreground_application},
                       {"backend", frame.backend},
                       {"connection_generation", frame.connection_generation}};
    });
    return device_status();
}

Application::J Application::device_status() const {
    std::lock_guard lock(mutex_);
    const auto busy = operation_.value("state", "idle") == "running";
    auto frame = frame_info_;
    if (frame.is_object() && frame_captured_at_)
        frame["age_ms"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - *frame_captured_at_)
                              .count();
    const auto operation_failed = operation_.value("state", "idle") == "failed";
    const auto operation_error = operation_failed ? operation_.value("error", "DEVICE_OPERATION_FAILED")
                                                  : std::string{};
    return {{"state", busy ? operation_.value("name", "working") : bool(backend_) ? "connected" : "disconnected"},
            {"connected", bool(backend_)}, {"busy", busy}, {"operation", operation_},
            {"error_code", operation_failed ? J(operation_error) : J(nullptr)},
            {"message", operation_failed ? J(operation_error) : J(nullptr)},
            {"frame", std::move(frame)}, {"frame_available", !frame_png_.empty()},
            {"display_name", backend_ ? backend_->lifecycle_target().device_id : "未连接"},
            {"screenshot_url", frame_png_.empty() ? J(nullptr) : J("/api/v1/device/frame")},
            {"diagnostics", backend_ ? backend_->diagnostics() : J(nullptr)}};
}

Application::J Application::run_status() const {
    const auto view = coordinator_->read_view();
    const auto &snapshot = view.snapshot;
    auto value = storage::snapshot_json(snapshot);
    value["server_instance_id"] = view.instance_id;
    value["request_id"] = view.request_id;
    value["events"] = view.journal ? view.journal->latest_node() : J{{"events", J::array()}};
    const auto &event_page = value.at("events");
    const auto directory = view.store ? view.store->directory() : std::filesystem::path{};
    value["run_directory"] = directory.empty() ? J(nullptr) : J(platform::utf8(directory));
    value["result"] = contracts::name(snapshot.state);
    value["error_code"] = snapshot.reason.empty() ? J(nullptr) : J(snapshot.reason);
    value["message"] = snapshot.reason.empty() ? J(nullptr) : J(snapshot.reason);
    value["worker_publication_failed"] = worker_publication_failed_.load();
    const auto diagnostic_summary = view.store ? view.store->diagnostic_summary() : J::object();
    J diagnostics = J::array();
    for (const auto &entry : diagnostic_summary.value("entries", J::array())) {
        auto item = entry;
        item["label"] = entry.value("reason", entry.value("stage", "诊断"));
        if (entry.value("status", "") == "saved" && entry.contains("id"))
            item["image_url"] = "/api/v1/diagnostics/" + view.instance_id + "/" +
                                std::to_string(snapshot.run_id) + "/" +
                                std::to_string(entry.at("generation").get<std::uint64_t>()) + "/" +
                                std::to_string(entry.at("id").get<std::uint64_t>()) + ".png";
        if (entry.contains("frame") && entry.at("frame").is_object())
            item["frame_age_ms"] = entry.at("frame").value("age_at_submit_ms", 0);
        diagnostics.push_back(std::move(item));
    }
    value["diagnostics"] = std::move(diagnostics);
    {
        std::lock_guard lock(mutex_);
        value["coherent"] = snapshot.run_id == active_run_id_;
        if (snapshot.run_id != active_run_id_) return value;
        // Status polling only needs the visible nodes. Borrow the immutable
        // indexes under their existing lock instead of copying the whole graph.
        const auto &mapping = active_pipeline_to_node_;
        const auto &source_paths = active_source_paths_;
        value["workflow_id"] = active_workflow_id_.empty() ? J(nullptr) : J(active_workflow_id_);
        value["workflow_revision"] = active_workflow_revision_.empty()
                                         ? J(nullptr) : J(active_workflow_revision_);
        value["submission"] = submission_;
        value["busy"] = run_active() || task_session_active_ || operation_.value("state", "idle") == "running";
        value["repeat"] = repeat_status_;
        value["task_name"] = active_task_name_.empty() ? J(nullptr) : J(active_task_name_);
        value["elapsed_seconds"] = snapshot.quiescent && snapshot.business.is_object()
            ? snapshot.business.value("elapsed_seconds", 0.0) : active_started_
            ? std::chrono::duration_cast<std::chrono::seconds>(
                  std::chrono::steady_clock::now() - *active_started_).count()
            : 0;
        value["statistics"] = {
            {"已完成业务段", snapshot.completed_business_units},
            {"动作尝试", snapshot.inputs.attempted},
            {"动作执行", snapshot.inputs.backend_called},
            {"动作拒绝", snapshot.inputs.rejected}};
        value["handoff"] = handoff_status_;
        if (event_page.contains("events")) {
            const auto &events = event_page.at("events");
            for (auto it = events.rbegin(); it != events.rend(); ++it) {
                if (!it->contains("node_id") || !it->at("node_id").is_string())
                    continue;
                const auto pipeline = it->at("node_id").get<std::string>();
                constexpr std::string_view observer_prefix = "__wvd_observe__";
                constexpr std::string_view await_suffix = "@await";
                const bool observing = pipeline.starts_with(observer_prefix) ||
                    pipeline.ends_with(await_suffix);
                const auto visible = pipeline.starts_with(observer_prefix)
                    ? pipeline.substr(observer_prefix.size())
                    : pipeline.ends_with(await_suffix)
                        ? pipeline.substr(0, pipeline.size() - await_suffix.size()) : pipeline;
                // 派生观察节点映射回作者的输入节点；仍显示真实阶段，不能让等待看起来没执行。
                value["step_name"] = visible + (observing ? "（等待页面结果）" : "");
                value["execution_stage"] = observing ? "transition_observation" : "pipeline";
                if (it->contains("source_path")) value["node_path"] = it->at("source_path");
                else if (source_paths.contains(visible)) value["node_path"] = source_paths.at(visible);
                if (const auto found = mapping.find(visible); found != mapping.end()) {
                    value["current_node_id"] = found->second;
                    if (snapshot.state == contracts::RunState::Failed)
                        value["failed_node_id"] = found->second;
                    break;
                }
                if (mapping.empty()) break; // 旧任务没有作者节点映射，也必须显示真实执行步骤。
            }
        }
        if (value.contains("active_event") && value.at("active_event").is_object()) {
            const auto source = value.at("active_event").value("source_node", std::string{});
            value["suspended_step"] = {
                {"pipeline_node", source},
                {"node_id", mapping.contains(source) ? J(mapping.at(source)) : J(nullptr)},
                {"node_path", source_paths.value(source, J::array())}
            };
        }
    }
    if (value.contains("execution") && value.at("execution").is_object()) {
        const auto &execution = value.at("execution");
        value["call_stack"] = execution.value("call_stack", J::array());
        if (snapshot.state == contracts::RunState::Running ||
            snapshot.state == contracts::RunState::Recovering ||
            snapshot.state == contracts::RunState::StopRequested) {
            value["suspended_step"] = execution.value("suspended_step", J(nullptr));
            if (!execution.value("step_id", "").empty()) value["step_name"] = execution.at("step_id");
            auto path = J::parse(execution.value("source_path", ""), nullptr, false);
            if (path.is_array()) value["node_path"] = std::move(path);
        }
    }
    return value;
}

platform::PreparationObserver Application::preparation_observer(const J &stored) {
    const auto logging = storage::LoggingPolicy::from_profile(stored);
    if (!logging.performance || !logging.accepts(storage::LogLevel::Info)) return {};
    return [this](const char *phase, const char *state, const J &metrics) {
        std::lock_guard lock(mutex_);
        auto &entry = operation_["preparation"][phase];
        entry = metrics;
        entry["state"] = state;
        operation_["preparation"]["current_phase"] = phase;
    };
}

games::tasks::CompiledWorkflow Application::compile_task_graph(const J &request,
    const games::WvdQuestDefinition &task, const J &values,
    const platform::PreparationObserver &observer) const {
    platform::PreparationTimer timer("compile_task_graph", observer);
    platform::PreparationTimer library_timer("public_library", observer);
    require(!stopping_ && !cancel_operation_, "PREPARATION_CANCELLED");
    const auto board = workflow_store_->read("guild-open-bounty-page");
    const auto locale = authoring::effective_resource_locale(request, J::object());
    const auto closure = workflow_store_->snapshot_closure(board, games::tasks::native_public_steps);
    const games::tasks::PublicFlowLibrary library(closure, semantic_catalogue_);
    J preparation{{"public_library", library_timer.sample()}};
    const games::tasks::PublicStepScope steps([&](const std::string &id, const J &arguments) {
        return library.compile_step(id, arguments, locale);
    });
    platform::PreparationTimer graph_timer("build_task_workflow", observer);
    auto workflow = games::tasks::build_task_workflow(task, values, available_images_,
        [this, &board, &library, &locale](const games::WvdQuestDefinition &selected, const J &profile,
                         const std::set<std::string> &images) {
            const auto status = workflow_store_->inspect_builtin(
                builtin_documents_.at("guild-open-bounty-page")).at("status").get<std::string>();
            require(status != "update_available" && status != "source_unknown",
                "BOUNTY_PUBLIC_BOARD_REQUIRES_REVIEW");
            return games::tasks::bounty_cycle(selected, profile, images, library, board,
                locale);
        });
    preparation["build_task_workflow"] = graph_timer.sample();
    platform::PreparationTimer boot_timer("boot_recovery", observer);
    workflow = games::recovery::with_boot_recovery(workflow, true);
    preparation["boot_recovery"] = boot_timer.sample();
    require(workflow.nodes.contains("Boot_Entry"), "PRODUCTION_BOOT_ENTRY_MISSING");
    platform::PreparationTimer assets_timer("locale_assets", observer);
    games::tasks::localize_task_assets(workflow, semantic_catalogue_,
        authoring::effective_resource_locale(request, J::object()));
    declare_portrait_assets(workflow, portrait_bundle(values, false));
    games::tasks::require_locale_asset_coverage(workflow, locale);
    require(!stopping_ && !cancel_operation_, "PREPARATION_CANCELLED");
    preparation["locale_assets"] = assets_timer.sample();
    workflow.preparation = std::move(preparation);
    workflow.preparation["compile_task_graph"] = timer.sample();
    workflow.preparation["nodes"] = workflow.nodes.size();
    return workflow;
}

runtime::NativeRunDefinition Application::assemble_task(const J &request, const J &stored,
    const devices::LifecycleTarget &lifecycle, std::optional<J> frozen_values,
    bool continuation, std::optional<games::tasks::CompiledWorkflow> prepared,
    const platform::PreparationObserver &diagnostic) {
    const auto observer = diagnostic ? diagnostic : preparation_observer(stored);
    if (stopping_ || cancel_operation_) throw std::runtime_error("PREPARATION_CANCELLED");
    const auto &stored_values = stored.at("values");
    const auto task_id = request.value("task_id", stored_values.at("FARM_TARGET").is_string()
        ? stored_values.at("FARM_TARGET").get<std::string>() : std::string{});
    require(!task_id.empty(), "TASK_NOT_SELECTED");
    const auto values = frozen_values ? *frozen_values : effective_profile_values(task_id, stored);
    const auto resource_locale = authoring::effective_resource_locale(request, J::object());
    require(values.at("FARM_TARGET") == task_id && values.size() == 33,
            "TASK_FROZEN_PROFILE_INVALID");
    const auto &task = catalog_->at(task_id);
    J handoff_source = nullptr;
    if (!continuation && task_id != "7000G" && values.at("ACTIVE_BEG_MONEY").get<bool>()) {
        const auto &target = catalog_->at("7000G");
        handoff_source = {{"schema", 1}, {"request_id", checked_request_id(request)},
            {"task", {{"id", task.id}, {"type", task.type}, {"source", task.source}}},
            {"target_task", {{"id", target.id}, {"type", target.type},
                             {"source", target.source}}},
            {"profile_digest", games::tasks::digest_handoff_json(values)},
            {"profile_sources", stored.at("sources")},
            {"selected_section", stored.at("selected_section")},
            {"legacy_document_digest", games::tasks::digest_handoff_json(stored.at("legacy_document"))},
            {"legacy_passthrough_digest", games::tasks::digest_handoff_json(stored.at("legacy_passthrough"))}};
        handoff_source["digest"] = games::tasks::digest_handoff_json(handoff_source);
    }
    auto workflow = prepared ? std::move(*prepared) : compile_task_graph(request, task, values, observer);
    games::tasks::require_locale_asset_coverage(workflow, resource_locale);
    const auto request_id = checked_request_id(request);
    const auto destination = paths_.data_root / "published" / request_id;
    const auto portraits = portrait_bundle(values);
    auto publication = games::tasks::publish_native(workflow, author_bundle_,
        destination, aliases_, J::object(), portraits ? &*portraits : nullptr,
        [this] { require(!stopping_ && !cancel_operation_, "PREPARATION_CANCELLED"); }, observer);
    const auto &root = publication.program.definitions.at(publication.program.root_definition);
    require(root.steps.contains(workflow.checkpoint), "NATIVE_CHECKPOINT_MISSING");
    const auto checkpoint_source = root.steps.at(workflow.checkpoint).source_path;
    runtime::NativeUnit unit{std::make_shared<const wvd::workflow::FlowProgram>(std::move(publication.program)),
        std::move(publication.bundle), games::vision::native_handlers(aliases_, resource_locale,
            workflow.dialogue_policy, workflow.random_maze_events),
        checkpoint_source, workflow.time_limit};
    const auto count = games::tasks::task_unit_count(task_id, values);
    runtime::NativeRunDefinition definition;
    definition.request_id = request_id;
    definition.match_budget = match_budget_;
    definition.logging = storage::LoggingPolicy::from_profile(stored);
    if (definition.logging.performance && definition.logging.accepts(storage::LogLevel::Info)) {
        definition.preparation = std::move(workflow.preparation);
        definition.preparation.update(publication.preparation);
    }
    definition.units.assign(count, unit);
    definition.policy = {lifecycle.device_id, "wvd", "jp.co.drecom.wizardry.daphne",
        unit.bundle.revision, "900x1600", {900, 1600},
        {contracts::ActionKind::Click, contracts::ActionKind::ClickKey, contracts::ActionKind::Swipe},
        {}, {"wvd"}, 0ms};
    for (const auto &action : workflow.required_actions) definition.policy.permissions.insert(action_kind(action));
    if (!continuation)
        definition.startup = devices::LifecyclePlan{lifecycle,
            lifecycle.vpn_required
                ? std::vector{devices::LifecycleOperation::EnsureVpn,
                              devices::LifecycleOperation::StartApplication}
                : std::vector{devices::LifecycleOperation::StartApplication}, 1};
    else if (lifecycle.vpn_required)
        definition.startup = devices::LifecyclePlan{lifecycle,
            {devices::LifecycleOperation::EnsureVpn}, 1};
    definition.total_time_limit = std::min(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::hours{24}),
        workflow.time_limit * static_cast<std::int64_t>(count) + std::chrono::hours{1} +
            (values.at("ACTIVE_BEG_MONEY").get<bool>() ? 0ms :
                std::chrono::milliseconds{7300000}));
    definition.create_state = [values, handoff_source](const contracts::StateCreationContext &creation) {
        return std::make_unique<games::WvdRunState>(values, creation, nullptr, handoff_source);
    };
    definition.operations = [](contracts::BusinessRunState &base,
        auto event, auto checkpoint) {
        return games::wvd_operation_factory(dynamic_cast<games::WvdRunState &>(base),
            std::move(event), std::move(checkpoint));
    };
    definition.recovery = games::tasks::recovery_policy(lifecycle);
    if (handoff_source.is_object()) {
        definition.handoff_ready = [source = handoff_source](
            const contracts::SessionResult &last, const contracts::BusinessRunState &state) {
            return games::tasks::handoff_ready(last, state, source);
        };
    }
    return definition;
}
contracts::RunSnapshot Application::start_prepared_run(runtime::NativeRunDefinition definition,
    const std::shared_ptr<devices::DeviceConnection> &backend) {
    // Called under command_mutex_: both task and authoring entry points share
    // the final cancellation/ownership gate and unpublished-directory rollback.
    const auto request_id = definition.request_id;
    recognition::Bundle unpublished{definition.units.front().bundle.root, "unstarted", {}};
    unpublished.lease = definition.units.front().bundle.lease;
    auto preparation = std::move(definition.preparation);
    platform::PreparationTimer start_timer;
    contracts::RunSnapshot snapshot;
    try {
        require(!stopping_ && !cancel_operation_, "PREPARATION_CANCELLED");
        { std::lock_guard lock(mutex_); preview_lease_.reset(); }
        snapshot = coordinator_->start(std::move(definition), backend);
    } catch (...) {
        const auto current = coordinator_->request_snapshot(request_id);
        if (!current || current->quiescent)
            discard_unstarted_publication(definition, unpublished, paths_.data_root / "published" / request_id);
        throw;
    }
    unpublished.lease.reset();
    if (!preparation.empty()) {
        preparation["coordinator_start"] = start_timer.sample();
        coordinator_->record_preparation(preparation);
    }
    return snapshot;
}
Application::J Application::prepare_task(const J &request, const J &stored,
    std::shared_ptr<devices::DeviceConnection> backend,
    std::optional<J> frozen_values, J handoff_parent,
    std::optional<games::tasks::CompiledWorkflow> prepared) {
    require_storage_space();
    require(bool(backend), "DEVICE_NOT_CONNECTED");
    backend->set_vpn_required(stored.at("values").at("AUTO_START_CLASH").get<bool>());
    auto definition = assemble_task(request, stored, backend->lifecycle_target(),
        std::move(frozen_values), handoff_parent.is_object(), std::move(prepared));
    definition.handoff_parent = std::move(handoff_parent);
    const auto &stored_values = stored.at("values");
    const auto selected = stored_values.at("FARM_TARGET").is_string()
        ? stored_values.at("FARM_TARGET").get<std::string>() : std::string{};
    const auto task_id = request.value("task_id", selected);
    const auto &task = catalog_->at(task_id);
    const auto request_id = definition.request_id;
    std::lock_guard command(command_mutex_);
    const auto snapshot = start_prepared_run(std::move(definition), backend);
    {
        std::lock_guard lock(mutex_);
        active_workflow_id_ = "task:" + task_id;
        active_run_id_ = snapshot.run_id;
        active_workflow_revision_ = stored.at("revision").get<std::string>();
        active_task_name_ = task.source.value("questName", task_id);
        active_started_ = std::chrono::steady_clock::now();
        active_pipeline_to_node_.clear();
        active_source_paths_ = J::object();
    }
    auto result = storage::snapshot_json(snapshot);
    result.update({{"accepted", true}, {"task_id", task_id},
                   {"task_name", task.source.value("questName", task_id)}, {"request_id", request_id}});
    return result;
}

void Application::watch_task_session(const J &request, const J &stored, J source_values,
    std::shared_ptr<devices::DeviceConnection> backend, std::string request_id,
    std::shared_ptr<std::atomic<int>> start_signal) {
    if (handoff_worker_.joinable()) handoff_worker_.join();
    require(!stopping_ && !cancel_operation_, "PREPARATION_CANCELLED");
    {
        std::lock_guard lock(mutex_);
        handoff_status_ = {{"state", "watching"}, {"source_request_id", request_id}};
        repeat_status_ = {{"active", request.value("repeat", false)},
            {"state", request.value("repeat", false) ? "running" : "disabled"},
            {"completed_cycles", 0}, {"target_cycles", request.value("repeat_count", J(nullptr))},
            {"reason", ""}, {"request_id", request_id}};
        task_session_active_ = true;
    }
    try {
    handoff_worker_ = std::jthread([this, request = J(request), stored = J(stored), source_values = std::move(source_values),
        backend = std::move(backend), request_id = std::move(request_id), start_signal = std::move(start_signal)](std::stop_token stop) mutable noexcept {
      try {
        // The preparing worker must resolve submission ownership even on cancel.
        while (start_signal->load() == 0) std::this_thread::sleep_for(1ms);
        const bool owns_batch = start_signal->load() == 1 || coordinator_->owns_request(request_id);
        const auto update = [this](const std::string &state, const J &detail) {
            std::lock_guard lock(mutex_);
            handoff_status_ = {{"state", state}, {"detail", detail}};
        };
        // 所有正常/异常退出都统一释放会话准入，不能留下界面无法停止的轮间窗口。
        const auto work = [&] {
        try {
          std::uint64_t completed = 0;
          std::uintmax_t batch_bytes = 0;
          constexpr std::uintmax_t batch_limit = 16ULL * 1024 * 1024 * 1024;
          const auto session_request_id = request_id;
          while (!stop.stop_requested() && !stopping_ && !cancel_operation_) {
            while (!stop.stop_requested() && !stopping_ && !cancel_operation_) {
                if (coordinator_->wait_for(250ms)) break;
                if (coordinator_->wait_for_worker(0ms) && !coordinator_->snapshot().quiescent) {
                    update("blocked", "SOURCE_CLEANUP_PENDING");
                    std::lock_guard lock(mutex_);
                    repeat_status_["state"] = "failed";
                    repeat_status_["reason"] = coordinator_->snapshot().reason;
                    return;
                }
            }
            if (stop.stop_requested() || stopping_ || cancel_operation_) return;
            require(coordinator_->collect_finished_worker(), "HANDOFF_SOURCE_WORKER_NOT_FINISHED");
            const auto snapshot = coordinator_->request_snapshot(request_id);
            require(snapshot && snapshot->quiescent && snapshot->result_saved &&
                snapshot->storage_error.empty(), "HANDOFF_SOURCE_NOT_COMMITTED");
            // Account retained run facts and resource references, not just one
            // file's limit. Never delete history to make a new round admissible.
            for (const auto &directory : {coordinator_->run_directory(), paths_.data_root / "published" / request_id}) {
                std::size_t entries{};
                if (!std::filesystem::exists(directory)) continue;
                for (const auto &entry : std::filesystem::recursive_directory_iterator(directory)) {
                    require(++entries <= 65536 &&
                        !(GetFileAttributesW(entry.path().c_str()) & FILE_ATTRIBUTE_REPARSE_POINT), "BATCH_STORAGE_INVENTORY_INVALID");
                    if (entry.is_regular_file()) {
                        const auto bytes = entry.file_size();
                        require(bytes <= batch_limit && batch_bytes <= batch_limit - bytes, "BATCH_STORAGE_LIMIT");
                        batch_bytes += bytes;
                    }
                }
            }
            {
                std::lock_guard lock(mutex_);
                repeat_status_["retained_reference_bytes"] = batch_bytes;
                repeat_status_["batch_storage_limit_bytes"] = batch_limit;
            }
            if (snapshot->outcome_category != "handoff_ready") {
                if (request.value("repeat", false)) {
                    const auto &business = snapshot->business;
                    const auto cycle = business.value("bounty_cycle", J::object());
                    if (snapshot->state != contracts::RunState::Completed)
                        throw std::runtime_error(snapshot->reason.empty() ? "REPEAT_CYCLE_NOT_COMPLETED" : snapshot->reason);
                    // 点击前上下文失效会被记录为拒绝，但没有发送输入，执行器可重新观察。
                    // 是否续轮由最终业务/清理回执决定，不能因这种已恢复的拒绝再终止成功轮。
                    require(snapshot->state == contracts::RunState::Completed &&
                        snapshot->completed_business_units == 3 &&
                        std::all_of(snapshot->secondary_errors.begin(), snapshot->secondary_errors.end(),
                            [](const auto &error) { return error == "DIAGNOSTIC_IMAGES_INCOMPLETE"; }) && snapshot->details_complete &&
                        cycle.value("completed_cycles", 0) == 1 && cycle.value("reports_remaining", -1) == 0 &&
                        !business.value("bounty_report_pending", true) &&
                        !business.value("inn_payment_pending", true), "REPEAT_CYCLE_NOT_CLEAN");
                    ++completed;
                    bool batch_complete=false;
                    {
                        std::lock_guard lock(mutex_);
                        repeat_status_["completed_cycles"] = completed;
                        repeat_status_["state"] = "waiting";
                        // 一轮只有三段结算及静止回执全部通过才计数，失败尝试不消费目标轮数。
                        if (request.contains("repeat_count") && completed >= request.at("repeat_count").get<std::uint64_t>()) {
                            batch_complete=true;
                        }
                    }
                    if(measurement_.armed()) {
                        const auto view=coordinator_->read_view();
                        const auto memory=platform::sample_memory();
                        measurement_.joined_boundary({{"server_instance_id",view.instance_id},
                            {"process_id",memory.process_id},{"process_created_100ns",memory.process_created_100ns},
                            {"run_id",snapshot->run_id},{"round",completed},{"generation",snapshot->generation},
                            {"phase","worker_joined"},{"release_scope","worker"},
                            {"batch_request_id",session_request_id},{"run_directory",platform::utf8(view.store->directory())},
                            {"input_clean",snapshot->unresolved_inputs.empty()},
                            {"cleanup_complete",snapshot->quiescent && view.worker_joined},
                            {"heap_maintenance_complete",view.heap_maintenance_complete},
                            {"heap_maintenance_succeeded",view.heap_maintenance_succeeded}},
                            [&]{return stop.stop_requested() || stopping_ || cancel_operation_;});
                    }
                    if(batch_complete) {
                        std::lock_guard lock(mutex_);repeat_status_["state"]="completed";return;
                    }
                    // 轮间等待可取消，不持有命令锁；停止在下一次 prepare_task 的提交锁内再次核验。
                    for (int i = 0; i < 100 && !stop.stop_requested() && !stopping_ && !cancel_operation_; ++i)
                        std::this_thread::sleep_for(100ms);
                    if (stop.stop_requested() || stopping_ || cancel_operation_) return;
                    require(batch_bytes <= batch_limit - 2ULL * 1024 * 1024 * 1024,
                        "BATCH_STORAGE_RESERVE_LOW");
                    auto next_request = request;
                    request_id = "repeat-" + games::tasks::digest_handoff_json(
                        {{"session", session_request_id}, {"cycle", completed + 1}});
                    next_request["request_id"] = request_id;
                    {
                        std::lock_guard lock(mutex_);
                        repeat_status_["state"] = "starting";
                    }
                    prepare_task(next_request, stored, backend, source_values);
                    {
                        std::lock_guard lock(mutex_);
                        repeat_status_["state"] = "running";
                    }
                    continue;
                }
                update("not_requested", snapshot->reason);
                return;
            }
            require(snapshot->secondary_errors.empty() && snapshot->details_complete,
                "HANDOFF_SOURCE_DIAGNOSTICS_INCOMPLETE");
            const auto &facts = snapshot->business;
            const auto &intent = facts.at("handoff_intent");
            require(intent.at("kind") == "turn_to_7000G" &&
                intent.at("run_identity") == facts.at("run_identity") &&
                intent.at("generation") == snapshot->generation &&
                intent.at("unknown_samples").get<std::uint64_t>() >= 5 &&
                !games::tasks::handoff_has_unconfirmed_effect(facts),
                "HANDOFF_INTENT_INVALID");
            const auto source = facts.at("handoff_source");
            games::tasks::validate_handoff_source(source, source_values);
            require(source.at("request_id") == request_id, "HANDOFF_SOURCE_REQUEST_CHANGED");
            const auto directory = coordinator_->run_directory();
            for (const auto &file : {directory / "result.json", directory / "run.json"})
                require(std::filesystem::is_regular_file(file) &&
                    std::filesystem::file_size(file) <= 32 * 1024 * 1024,
                    "HANDOFF_RESULT_FILE_INVALID");
            const auto saved = load_json(directory / "result.json");
            const auto run = load_json(directory / "run.json");
            require(saved.at("business") == facts && saved.at("run_id") == snapshot->run_id &&
                saved.at("quiescent") == true && saved.at("result_saved") == true &&
                run.at("definition").at("request_id") == request_id &&
                run.at("run_id") == snapshot->run_id,
                "HANDOFF_RESULT_SOURCE_MISMATCH");
            auto next_values = source_values;
            next_values["FARM_TARGET"] = "7000G";
            const auto next_id = "handoff-" + games::tasks::digest_handoff_json(
                {{"source", source.at("digest")}, {"intent", intent.at("intent_id")}});
            const J lineage{{"source_request_id", request_id},
                {"source_run_id", snapshot->run_id}, {"source_digest", source.at("digest")},
                {"intent", intent},
                {"result_sha256", platform::file_sha256(directory / "result.json")}};
            update("starting", lineage);
            if (stop.stop_requested() || stopping_ || cancel_operation_) return;
            const auto started = prepare_task({{"task_id", "7000G"}, {"request_id", next_id}},
                stored, backend, next_values, lineage);
            update("started", {{"run_id", started.at("run_id")},
                {"request_id", next_id}, {"source_run_id", snapshot->run_id}});
            // 既有转金币交接完整保留，但它不是蝎女一轮成功，不能交接后自动重刷蝎女。
            {
                std::lock_guard lock(mutex_);
                if (request.value("repeat", false)) {
                    repeat_status_["state"] = "stopped";
                    repeat_status_["reason"] = "TASK_HANDED_OFF";
                }
            }
            return;
          }
        } catch (const std::exception &error) {
            update("blocked", error.what());
            std::lock_guard lock(mutex_);
            if (request.value("repeat", false)) {
                repeat_status_["state"] = cancel_operation_ || stopping_ ? "stopped" : "failed";
                repeat_status_["reason"] = error.what();
            }
        } catch (...) {
            update("blocked", "HANDOFF_UNEXPECTED_EXCEPTION");
            std::lock_guard lock(mutex_);
            if (request.value("repeat", false)) {
                repeat_status_["state"] = "failed";
                repeat_status_["reason"] = "TASK_SESSION_UNEXPECTED_EXCEPTION";
            }
        }
        };
        if (start_signal->load() == 1) work();
        // Cancellation ends scheduling, not ownership. The input worker may
        // still be saving its terminal result; keep this watcher alive until
        // it can join and record the release boundary (including failed cleanup).
        if (owns_batch && (start_signal->load() == 2 || stop.stop_requested() || stopping_ || cancel_operation_)) {
            coordinator_->request_stop();
            while (!coordinator_->wait_for_worker(250ms)) {}
            coordinator_->collect_finished_worker();
        }
        const bool repeating = request.value("repeat", false);
        request = J();
        stored = J();
        source_values = J();
        backend.reset();
        if (owns_batch) coordinator_->record_batch_release();
        std::lock_guard lock(mutex_);
        repeat_status_["active"] = false;
        if (repeating && repeat_status_.at("state") != "failed" &&
            repeat_status_.at("state") != "stopped" && repeat_status_.at("state") != "completed") repeat_status_["state"] = "stopped";
        if (handoff_status_.value("state", "") == "watching")
            handoff_status_["state"] = "not_requested";
        task_session_active_ = false;
      } catch (...) {
        worker_publication_failed_ = true;
        cancel_operation_ = true;
        try {
            coordinator_->request_stop();
            while (!coordinator_->wait_for_worker(250ms)) {}
            if (coordinator_->collect_finished_worker()) task_session_active_ = false;
        } catch (...) {}
        OutputDebugStringW(L"WVD task watcher publication failed; cleanup ownership retained\n");
      }
    });
    } catch (...) {
        std::lock_guard lock(mutex_);
        task_session_active_ = false;
        repeat_status_["active"] = false;
        repeat_status_["state"] = "failed";
        repeat_status_["reason"] = "TASK_SESSION_WORKER_START_FAILED";
        handoff_status_["state"] = "blocked";
        throw;
    }
}

Application::J Application::list_workflows() const {
    auto entries = workflow_store_->list();
    for (auto &entry : entries) {
        const auto id = entry.at("id").get<std::string>();
        if (!builtin_documents_.contains(id)) continue;
        const auto status = workflow_store_->inspect_builtin(builtin_documents_.at(id));
        entry["builtin_status"] = status.at("status");
        entry["builtin_revision"] = status.at("builtin_revision");
    }
    return {{"workflows", std::move(entries)}};
}

Application::J Application::inspect_builtin(const std::string &flow_id) const {
    require(builtin_documents_.contains(flow_id), "BUILTIN_FLOW_NOT_FOUND");
    return workflow_store_->inspect_builtin(builtin_documents_.at(flow_id));
}

Application::J Application::sync_builtin(const std::string &flow_id, const J &request) {
    require(builtin_documents_.contains(flow_id), "BUILTIN_FLOW_NOT_FOUND");
    const auto updated = workflow_store_->sync_builtin(builtin_documents_.at(flow_id),
        request.at("local_revision").get<std::string>(),
        request.at("builtin_revision").get<std::string>());
    return ui_document_from_author(updated);
}

Application::J Application::create_workflow(const J &request) {
    return ui_document_from_author(workflow_store_->create(author_document_from_ui(request)));
}

Application::J Application::import_task_workflow(const J &request) {
    const auto task_id = request.at("task_id").get<std::string>();
    const auto flow_id = request.at("flow_id").get<std::string>();
    const auto &task = catalog_->at(task_id);
    require(task.type == "dungeon", "TASK_NOT_COMPOSABLE");
    const auto name = task.source.value("questName", task_id) + "（可编辑副本）";
    const auto node = [](const char *id, const char *title, const std::string &task,
                         const char *stage) {
        return J{{"id", id}, {"type", "business"}, {"name", title},
                 {"parameters", {{"binding", "task_stage"}, {"task_id", task},
                                  {"stage", stage}}}};
    };
    J document{{"schema", 1},
               {"flow", {{"id", flow_id}, {"name", name},
                          {"description", "由现有任务复制；入本准备、进入地下城和路线执行保持独立节点。"}}},
               {"entry", "prepare"},
               {"nodes", J::array({node("prepare", "入本准备与补给", task_id, "prepare"),
                                    node("enter", "进入地下城", task_id, "enter"),
                                    node("traverse", "路线、战斗与开箱", task_id, "traverse"),
                                    J{{"id", "complete"}, {"type", "end"},
                                      {"name", "任务段完成"},
                                      {"parameters", {{"outcome", "success"}}}}})},
               {"edges", J::array({
                    J{{"id", "prepare_enter"}, {"from", "prepare"}, {"to", "enter"},
                      {"outcome", "success"}, {"order", 0}},
                    J{{"id", "enter_traverse"}, {"from", "enter"}, {"to", "traverse"},
                      {"outcome", "success"}, {"order", 0}},
                    J{{"id", "traverse_complete"}, {"from", "traverse"}, {"to", "complete"},
                      {"outcome", "success"}, {"order", 0}}})},
               {"layout", {{"nodes", J::array({
                    J{{"node_id", "prepare"}, {"x", 40}, {"y", 100}},
                    J{{"node_id", "enter"}, {"x", 300}, {"y", 100}},
                    J{{"node_id", "traverse"}, {"x", 560}, {"y", 100}},
                    J{{"node_id", "complete"}, {"x", 820}, {"y", 100}}})},
                    {"viewport", {{"x", 0}, {"y", 0}, {"zoom", 1}}}}},
               {"execution", {{"time_limit_ms", 1800000}}}};
    if (request.contains("resource_locale"))
        document["execution"]["resource_locale"] =
            authoring::effective_resource_locale(request, J::object());
    return ui_document_from_author(workflow_store_->create(document));
}

Application::J Application::read_workflow(const std::string &flow_id) const {
    return ui_document_from_author(workflow_store_->read(flow_id));
}

Application::J Application::save_workflow(const std::string &flow_id, const J &request) {
    const auto document = author_document_from_ui(request);
    require(document.contains("revision"), "WORKFLOW_REVISION_REQUIRED");
    return ui_document_from_author(workflow_store_->compare_exchange(
        flow_id, document.at("revision").get<std::string>(), document));
}

void Application::delete_workflow(const std::string &flow_id, const J &request) {
    const auto revision = request.value("revision", std::string{});
    workflow_store_->erase(flow_id, revision);
}

Application::J Application::prepare_workflow(const std::string &flow_id, const J &request,
    const J &stored, J document, std::shared_ptr<devices::DeviceConnection> backend,
    const J &library_snapshot, std::optional<PreparedWorkflow> prepared) {
    require_storage_space();
    require(bool(backend), "DEVICE_NOT_CONNECTED");
    if (stopping_ || cancel_operation_) throw std::runtime_error("PREPARATION_CANCELLED");
    const auto workflow_revision = document.at("revision").get<std::string>();
    const auto workflow_name = document.at("flow").at("name").get<std::string>();
    backend->set_vpn_required(stored.at("values").at("AUTO_START_CLASH").get<bool>());
    std::map<std::string, std::string> pipeline_to_node;
    J source_paths = J::object();
    auto definition = assemble_workflow(request, stored, std::move(document),
        backend->lifecycle_target(), &pipeline_to_node, library_snapshot, &source_paths, std::move(prepared));
    const auto request_id = definition.request_id;
    std::lock_guard handoff(command_mutex_);
    const auto snapshot = start_prepared_run(std::move(definition), backend);
    {
        std::lock_guard lock(mutex_);
        active_workflow_id_ = flow_id;
        active_run_id_ = snapshot.run_id;
        active_workflow_revision_ = workflow_revision;
        active_task_name_ = workflow_name;
        active_started_ = std::chrono::steady_clock::now();
        active_pipeline_to_node_ = std::move(pipeline_to_node);
        active_source_paths_ = std::move(source_paths);
    }
    auto result = storage::snapshot_json(snapshot);
    result["accepted"] = true;
    result["workflow_id"] = flow_id;
    result["workflow_revision"] = workflow_revision;
    result["request_id"] = request_id;
    return result;
}

Application::PreparedWorkflow Application::compile_workflow_graph(
    const J &request, const J &stored, J document, const J &library_snapshot) {
    if (stopping_ || cancel_operation_) throw std::runtime_error("PREPARATION_CANCELLED");
    require(request.value("revision", std::string{}) ==
                document.at("revision").get<std::string>(),
            "WORKFLOW_REVISION_MISMATCH");
    // 调试图会移除持久化 revision；请求已先与已保存版本完成CAS身份核对。
    if (request.value("mode", "workflow") == "selected_node") {
        document = authoring::instantiate_document(document, request.value("arguments", J::object()));
        document = selected_node_document(std::move(document), request.at("node_id"));
    }
    const games::tasks::PublicFlowLibrary library(library_snapshot, semantic_catalogue_);
    const auto locale = authoring::effective_resource_locale(request, document.at("execution"));
    const games::tasks::PublicStepScope steps([&](const std::string &id, const J &arguments) {
        return library.compile_step(id, arguments, locale);
    });
    auto supplied = request.value("arguments", J::object());
    if (request.value("mode", "workflow") == "selected_node") supplied = J::object();
    const auto task_ids = library.task_profiles(document, supplied, locale);
    require(task_ids.size() <= 1, "AUTHOR_MULTIPLE_TASK_PROFILES_UNSUPPORTED");
    auto values = task_ids.empty() ? stored.at("values")
                                   : effective_profile_values(*task_ids.begin(), stored);
    auto compiled = library.compile(
        document, [this, &values](const J &parameters) {
            const auto binding = parameters.at("binding").get<std::string>();
            if (binding == "combat")
                return games::combat::fight_encounter(values, available_images_);
            if (binding == "chest")
                return games::chest::open_chest(
                    static_cast<int>(parameters.at("preferred").get<std::int64_t>()),
                    parameters.value("quick", values.at("QUICK_DISARM_CHEST").get<bool>()),
                    parameters.value("seed", 0u));
            const auto &task = catalog_->at(parameters.at("task_id").get<std::string>());
            const auto plan = games::WvdTaskPlan::parse(task);
            const auto stage = parameters.at("stage").get<std::string>();
            if (stage == "prepare")
                return games::tasks::prepare_departure(plan, values);
            if (stage == "enter")
                return games::navigation::enter_dungeon(plan);
            if (stage == "traverse")
                return games::tasks::traverse_dungeon(plan, values, available_images_);
            throw std::runtime_error("AUTHOR_TASK_STAGE_UNSUPPORTED");
        }, supplied, locale);
    const bool combat_debug = request.value("mode", "workflow") == "combat_debug";
    auto executable = combat_debug ? compiled.workflow : games::recovery::with_boot_recovery(compiled.workflow, true);
    if (combat_debug) executable.random_maze_events = false;
    games::tasks::localize_task_assets(executable, semantic_catalogue_, locale);
    declare_portrait_assets(executable, portrait_bundle(values, false));
    games::tasks::require_locale_asset_coverage(executable, locale);
    require(combat_debug || executable.nodes.contains("Boot_Entry"), "PRODUCTION_BOOT_ENTRY_MISSING");
    return {std::move(executable), std::move(values), std::move(document),
        std::move(compiled.pipeline_to_node), locale};
}

runtime::NativeRunDefinition Application::assemble_workflow(
    const J &request, const J &stored, J document, const devices::LifecycleTarget &lifecycle,
    std::map<std::string, std::string> *pipeline_to_node, const J &library_snapshot, J *source_paths,
    std::optional<PreparedWorkflow> prepared) {
    auto graph = prepared ? std::move(*prepared) : compile_workflow_graph(request, stored, document, library_snapshot);
    auto &executable = graph.executable;
    const auto &values = graph.values;
    const auto &locale = graph.locale;
    document = std::move(graph.document);
    const bool combat_debug = request.value("mode", "workflow") == "combat_debug";
    games::tasks::require_locale_asset_coverage(executable, locale);
    const auto request_id = checked_request_id(request);
    const auto destination = paths_.data_root / "published" / request_id;
    const auto provenance = executable.authoring.value("source_paths", J::object());
    const auto portraits = portrait_bundle(values);
    auto publication = games::tasks::publish_native(executable, author_bundle_,
        destination, aliases_, provenance, portraits ? &*portraits : nullptr,
        [this] { require(!stopping_ && !cancel_operation_, "PREPARATION_CANCELLED"); },
        preparation_observer(stored));
    const auto &root = publication.program.definitions.at(publication.program.root_definition);
    require(root.steps.contains(executable.checkpoint), "NATIVE_CHECKPOINT_MISSING");
    const auto checkpoint_source = root.steps.at(executable.checkpoint).source_path;
    runtime::NativeRunDefinition definition;
    definition.request_id = request_id;
    definition.match_budget = match_budget_;
    definition.logging = storage::LoggingPolicy::from_profile(stored);
    if (definition.logging.performance && definition.logging.accepts(storage::LogLevel::Info))
        definition.preparation = std::move(publication.preparation);
    definition.units.push_back({std::make_shared<const wvd::workflow::FlowProgram>(std::move(publication.program)),
        std::move(publication.bundle), games::vision::native_handlers(aliases_, locale,
            executable.dialogue_policy, executable.random_maze_events),
        checkpoint_source, executable.time_limit});
    definition.total_time_limit = std::chrono::milliseconds(
        document.at("execution").at("time_limit_ms").get<std::int64_t>());
    definition.policy = {lifecycle.device_id, "wvd",
                         "jp.co.drecom.wizardry.daphne", definition.units.front().bundle.revision,
                         "900x1600", {900, 1600},
                         {contracts::ActionKind::Click, contracts::ActionKind::ClickKey,
                          contracts::ActionKind::Swipe}, {}, {"wvd"}, 0ms};
    for (const auto &action : executable.required_actions)
        definition.policy.permissions.insert(action_kind(action));
    definition.startup = devices::LifecyclePlan{lifecycle,
        lifecycle.vpn_required
            ? std::vector{devices::LifecycleOperation::EnsureVpn,
                          devices::LifecycleOperation::StartApplication}
            : std::vector{devices::LifecycleOperation::StartApplication}, 1};
    definition.create_state = [values](const contracts::StateCreationContext &creation) {
        return std::make_unique<games::WvdRunState>(values, creation);
    };
    definition.operations = [](contracts::BusinessRunState &base,
        auto event, auto checkpoint) {
        return games::wvd_operation_factory(dynamic_cast<games::WvdRunState &>(base),
            std::move(event), std::move(checkpoint));
    };
    definition.recovery = games::tasks::recovery_policy(lifecycle);
    if (combat_debug) {
        // 调试只接管当前一场战斗，不拉起应用、重启游戏、返回王城或启动任务循环。
        definition.startup.reset();
        definition.recovery = {};
    }
    if (source_paths) *source_paths = executable.authoring.value("source_paths", J::object());
    if (pipeline_to_node) {
        pipeline_to_node->clear();
        for (const auto &[pipeline, node] : graph.pipeline_to_node)
            (*pipeline_to_node)["Task_" + pipeline] = node;
    }
    return definition;
}

Application::J Application::stop_run(std::optional<std::uint64_t> requested_run_id,
                                    const std::string &requested_submission) {
    const auto current = coordinator_->snapshot();
    if (requested_run_id)
        require(current.run_id == *requested_run_id, "RUN_ID_MISMATCH");
    if (!requested_submission.empty()) {
        std::lock_guard lock(mutex_);
        require(submission_.is_object() && submission_.value("request_id", "") == requested_submission,
                "SUBMISSION_ID_MISMATCH");
    }
    ++stop_epoch_;
    cancel_operation_ = true;
    measurement_.cancel();
    {
        std::lock_guard lock(mutex_);
        if (repeat_status_.is_object() && repeat_status_.value("active", false))
            repeat_status_["state"] = "stopping";
        if (handoff_status_.is_object() &&
            (handoff_status_.value("state", "") == "watching" ||
             handoff_status_.value("state", "") == "starting"))
            handoff_status_["state"] = "cancelled";
    }
    coordinator_->request_stop();
    return {{"accepted", true}, {"run_id", current.run_id},
            {"request_id", requested_submission}, {"stop_requested", true},
            {"quiescent", false}};
}

api::DynamicReply Application::diagnostic_image(const std::string &identity) const {
    const auto first = identity.find('/');
    const auto second = identity.find('/', first == std::string::npos ? first : first + 1);
    const auto third = identity.find('/', second == std::string::npos ? second : second + 1);
    require(first != std::string::npos && second != std::string::npos && third != std::string::npos,
            "DIAGNOSTIC_IDENTITY_INVALID");
    const auto instance = identity.substr(0, first);
    require(!instance.empty() && instance.size() <= 64 &&
        std::all_of(instance.begin(), instance.end(), [](unsigned char c) {
            return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                   (c >= '0' && c <= '9') || c == '-';
        }), "DIAGNOSTIC_IDENTITY_INVALID");
    const auto number = [](std::string_view text) {
        std::uint64_t value{};
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        require(error == std::errc{} && end == text.data() + text.size() && value > 0,
                "DIAGNOSTIC_IDENTITY_INVALID");
        return value;
    };
    const auto run = number(std::string_view(identity).substr(first + 1, second - first - 1));
    const auto generation = number(std::string_view(identity).substr(second + 1, third - second - 1));
    const auto name = identity.substr(third + 1);
    require(name.size() > 4 && name.ends_with(".png"), "DIAGNOSTIC_NAME_INVALID");
    std::uint64_t id{};
    const auto digits = std::string_view(name).substr(0, name.size() - 4);
    const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), id);
    require(error == std::errc{} && end == digits.data() + digits.size(),
            "DIAGNOSTIC_NAME_INVALID");
    const auto view = coordinator_->read_view();
    const bool current = view.store && view.instance_id == instance && view.snapshot.run_id == run;
    const auto directory = current ? view.store->directory() : paths_.data_root / "runs" / instance / std::to_string(run);
    const auto index = current ? view.store->diagnostic_summary() :
        storage::RunStore::read_summary(directory).at("diagnostics");
    return {api::http::status::ok,
            storage::RunStore::read_diagnostic(directory, index, instance, run, generation, id),
            "image/png"};
}

Application::J Application::recognition_probe(const J &request) {
    contracts::FrameEnvelope frame;
    if (request.contains("image_base64")) {
        require(request.at("image_base64").is_string(), "PROBE_IMAGE_ENCODING_INVALID");
        frame.encoded_image = decode_base64(request.at("image_base64").get<std::string>());
        const auto image = cv::imdecode(frame.encoded_image, cv::IMREAD_COLOR);
        require(!image.empty() && image.type() == CV_8UC3,
                "PROBE_IMAGE_DECODE_FAILED");
        const contracts::Size size{image.cols, image.rows};
        require(size == contracts::Size{900, 1600}, "PROBE_IMAGE_SIZE_MUST_BE_900X1600");
        frame.identity = {"local-upload", "wvd", author_bundle_.revision, "900x1600", 1, 1, 0,
                          size, size, std::chrono::steady_clock::now(), "BGR8", 1,
                          "local-upload", "local-upload"};
    } else {
        std::lock_guard lock(mutex_);
        require(!frame_png_.empty() && frame_info_.is_object() && frame_captured_at_,
                "FRAME_NOT_AVAILABLE");
        frame.encoded_image = frame_png_;
        frame.identity = {frame_info_.at("device_id"), "wvd", author_bundle_.revision,
                          frame_info_.at("viewport"), 1, 1, 0,
                          {frame_info_.at("width"), frame_info_.at("height")}, {900, 1600},
                          *frame_captured_at_, "BGR8",
                          frame_info_.at("connection_generation"), frame_info_.at("backend"),
                          frame_info_.at("foreground_application")};
    }
    auto bundle = author_bundle_;
    bundle.snapshot_parent = paths_.data_root / "probe-snapshots";
    J recognition;
    if (request.contains("request")) {
        recognition = request.at("request");
        if (recognition.value("type", "") == "custom" && recognition.value("binding", "") == "WvdVision")
            recognition["parameters"] = authoring::SemanticAssets(semantic_catalogue_).resolve(
                recognition.at("parameters"), request.value("resource_locale", std::string{}));
    } else {
        const auto &parameters = request.at("recognition");
        const auto condition = authoring::SemanticAssets(semantic_catalogue_).resolve(
            parameters.contains("condition") ? parameters.at("condition") : parameters,
            request.value("resource_locale", std::string{}));
        const auto mode = condition.value("mode", std::string{});
        const auto roi = condition.value("roi", J::array({0, 0, 900, 1600}));
        recognition = {{"id", request.value("node_id", "probe")},
                       {"revision", author_bundle_.revision}, {"roi", roi}};
        if (mode == "ocr") {
            recognition["type"] = "ocr";
            recognition.update(recognition::ocr_parameters_json(recognition::parse_ocr_parameters(condition)));
        } else {
            // 诊断与发布后的 WVD 条件共用匹配器，保留灰度/遮罩/预处理等配方字段。
            recognition["type"] = "custom";
            recognition["binding"] = "WvdVision";
            recognition["parameters"] = condition;
        }
    }
    const auto parsed = recognition::parse_request(recognition);
    recognition::Service recognizer(std::move(bundle),
        games::vision::native_handlers(aliases_,
            authoring::effective_resource_locale(request, J::object()),
            games::recovery::dialogue_policy_from_name(
                request.value("dialogue_policy", std::string{}))), match_budget_);
    return observation_json(recognizer.evaluate(frame, frame.identity, parsed));
}

api::DynamicReply Application::handle(const api::Request &request) {
    try {
        require(!stopping_, "APPLICATION_STOPPING");
        const auto path = target_path(request);
        const auto method = request.method();
        if (path == "/api/v1/profile" && (method == api::http::verb::get || method == api::http::verb::head))
            return json_reply(profile());
        if (path == "/api/v1/profile" && method == api::http::verb::put)
            return json_reply(save_profile(parse_body(request)));
        if (path == "/api/v1/profile/effective" && method == api::http::verb::post)
            return json_reply(profile_for_task(parse_body(request).at("task_id")));
        if (path == "/api/v1/catalog" && (method == api::http::verb::get || method == api::http::verb::head))
            return json_reply(catalog());
        if (path == "/api/v1/workflows" && (method == api::http::verb::get || method == api::http::verb::head))
            return json_reply(list_workflows());
        if (path == "/api/v1/workflows" && method == api::http::verb::post)
            return json_reply(create_workflow(parse_body(request)), api::http::status::created);
        if (path == "/api/v1/workflows/from-task" && method == api::http::verb::post)
            return json_reply(import_task_workflow(parse_body(request)), api::http::status::created);
        constexpr std::string_view workflow_prefix = "/api/v1/workflows/";
        if (path.starts_with(workflow_prefix)) {
            auto suffix = path.substr(workflow_prefix.size());
            const bool run = suffix.ends_with("/run");
            const bool builtin = suffix.ends_with("/builtin");
            if (run)
                suffix.resize(suffix.size() - 4);
            if (builtin)
                suffix.resize(suffix.size() - 8);
            require(!suffix.empty() && suffix.find('/') == std::string::npos,
                    "WORKFLOW_PATH_INVALID");
            if (run && method == api::http::verb::post)
                return json_reply(start_workflow(suffix, parse_body(request)), api::http::status::accepted);
            if (builtin && (method == api::http::verb::get || method == api::http::verb::head))
                return json_reply(inspect_builtin(suffix));
            if (builtin && method == api::http::verb::post)
                return json_reply(sync_builtin(suffix, parse_body(request)));
            if (method == api::http::verb::get || method == api::http::verb::head)
                return json_reply(read_workflow(suffix));
            if (method == api::http::verb::put)
                return json_reply(save_workflow(suffix, parse_body(request)));
            if (method == api::http::verb::delete_) {
                delete_workflow(suffix, parse_body(request));
                return {api::http::status::no_content, {}, "application/json; charset=utf-8"};
            }
        }
        if (path == "/api/v1/device" && (method == api::http::verb::get || method == api::http::verb::head))
            return json_reply(device_status());
        if (path == "/api/v1/device/select-emulator" && method == api::http::verb::post)
            return json_reply(select_emulator_path());
        if (path == "/api/v1/device/connect" && method == api::http::verb::post)
            return json_reply(connect_device(parse_body(request)), api::http::status::accepted);
        if (path == "/api/v1/device/disconnect" && method == api::http::verb::post)
            return json_reply(disconnect_device(), api::http::status::accepted);
        if (path == "/api/v1/device/capture" && method == api::http::verb::post)
            return json_reply(capture_device(), api::http::status::accepted);
        if (path == "/api/v1/combat/debug" && method == api::http::verb::post)
            return json_reply(start_combat_debug(parse_body(request)), api::http::status::accepted);
        constexpr std::string_view portrait_prefix = "/api/v1/combat/portraits/";
        if (path.starts_with(portrait_prefix) && method == api::http::verb::get) {
            const auto encoded = path.substr(portrait_prefix.size());
            require(!encoded.empty() && encoded.size() <= 512 && encoded.size() % 2 == 0, "ENEMY_PORTRAIT_ID_INVALID");
            const auto hex = [](char value) -> int {
                if (value >= '0' && value <= '9') return value - '0';
                if (value >= 'a' && value <= 'f') return value - 'a' + 10;
                return -1;
            };
            std::string image;
            for (std::size_t i = 0; i < encoded.size(); i += 2) {
                const int high = hex(encoded[i]), low = hex(encoded[i + 1]);
                require(high >= 0 && low >= 0, "ENEMY_PORTRAIT_ID_INVALID");
                image.push_back(static_cast<char>(high * 16 + low));
            }
            require(available_images_.contains(image + ".png"), "ENEMY_PORTRAIT_MISSING");
            const auto source = games::vision::resolve_image_source(author_bundle_, aliases_, image);
            std::ifstream input(source.bundle->root / platform::BundleLease::checked_relative(source.relative_path), std::ios::binary);
            require(bool(input), "ENEMY_PORTRAIT_MISSING");
            return {api::http::status::ok, std::string(std::istreambuf_iterator<char>(input), {}), "image/png"};
        }
        if (path == "/api/v1/device/frame" && (method == api::http::verb::get || method == api::http::verb::head)) {
            std::lock_guard lock(mutex_);
            require(!frame_png_.empty(), "FRAME_NOT_AVAILABLE");
            return {api::http::status::ok,
                    std::string(reinterpret_cast<const char *>(frame_png_.data()), frame_png_.size()),
                    "image/png"};
        }
        constexpr std::string_view event_prefix = "/api/v1/runs/current/events/";
        if (path.starts_with(event_prefix) && method == api::http::verb::get) {
            const auto cursor = std::string_view(path).substr(event_prefix.size());
            std::uint64_t after{};
            const auto [end, error] = std::from_chars(cursor.data(), cursor.data() + cursor.size(), after);
            require(error == std::errc{} && end == cursor.data() + cursor.size(), "EVENT_CURSOR_INVALID");
            const auto view = coordinator_->read_view();
            auto page = view.journal ? view.journal->read_page(after, 128) :
                J{{"events", J::array()}, {"last_seq", 0}, {"head_seq", 0}, {"resync_required", false}};
            page["instance_id"] = view.instance_id; page["run_id"] = view.snapshot.run_id;
            return json_reply(page);
        }
        if (path == "/api/v1/runs/current" && (method == api::http::verb::get || method == api::http::verb::head))
            return json_reply(run_status());
        if(path=="/api/v1/runs/measurement" && method==api::http::verb::get)
            return json_reply(measurement_.status());
        if(path=="/api/v1/runs/measurement/arm" && method==api::http::verb::post) {
            const auto body=parse_body(request);
            std::lock_guard lock(mutex_);
            require(task_session_active_ && repeat_status_.value("active",false),"MEASUREMENT_BATCH_REQUIRED");
            measurement_.arm(body.at("controller_id"),body.at("configuration"),body.value("endpoints",2u),
                std::chrono::milliseconds{body.value("hold_ms",20000)},
                std::chrono::milliseconds{body.value("total_ms",1200000)});
            return json_reply(measurement_.status(),api::http::status::accepted);
        }
        if(path=="/api/v1/runs/measurement/release" && method==api::http::verb::post) {
            const auto body=parse_body(request);
            measurement_.release(body.at("controller_id"),body.at("sequence"));
            return json_reply(measurement_.status());
        }
        constexpr std::string_view diagnostic_prefix = "/api/v1/diagnostics/";
        if (path.starts_with(diagnostic_prefix) &&
            (method == api::http::verb::get || method == api::http::verb::head))
            return diagnostic_image(path.substr(diagnostic_prefix.size()));
        if (path == "/api/v1/runs/start" && method == api::http::verb::post)
            return json_reply(start_task(parse_body(request)), api::http::status::accepted);
        if (path == "/api/v1/runs/current/stop" && method == api::http::verb::post)
            return json_reply(stop_run(std::nullopt,
                parse_body(request).value("request_id", std::string{})), api::http::status::accepted);
        constexpr std::string_view run_prefix = "/api/v1/runs/";
        if (path.starts_with(run_prefix) && path.ends_with("/stop") &&
            method == api::http::verb::post) {
            const auto text = std::string_view(path).substr(run_prefix.size(),
                                                path.size() - run_prefix.size() - 5);
            std::uint64_t run_id{};
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), run_id);
            require(error == std::errc{} && end == text.data() + text.size(), "RUN_ID_INVALID");
            return json_reply(stop_run(run_id), api::http::status::accepted);
        }
        if (path == "/api/v1/recognition/probe" && method == api::http::verb::post)
            return json_reply(recognition_probe(parse_body(request)));
        return {api::http::status::not_found,
                J{{"error_code", "UNKNOWN_API"}}.dump(), "application/json; charset=utf-8"};
    } catch (const std::exception &error) {
        return error_reply(error);
    }
}

void Application::request_shutdown() {
    ++stop_epoch_;
    stopping_ = true;
    cancel_operation_ = true;
    measurement_.cancel();
    if (coordinator_) coordinator_->request_stop();
}
void Application::stop() {
    request_shutdown();
    if (device_worker_.joinable())
        device_worker_.join();
    if (handoff_worker_.joinable())
        handoff_worker_.join();
    if (coordinator_)
        while (!coordinator_->wait_for_worker(1s))
            std::this_thread::sleep_for(50ms);
    std::shared_ptr<devices::DeviceConnection> backend;
    {
        std::lock_guard lock(mutex_);
        backend = std::move(backend_);
    }
    if (backend) backend->disconnect();
    { std::lock_guard lock(mutex_); preview_lease_.reset(); }
}
} // namespace wvd::app
