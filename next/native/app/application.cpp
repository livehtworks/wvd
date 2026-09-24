#include "application.hpp"

#include "games/wvd/chest/chest.hpp"
#include "games/wvd/combat/encounter.hpp"
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
#include "games/wvd/vision/native_recognizers.hpp"
#include "platform/windows/path_utf8.hpp"
#include "platform/windows/bundle_lease.hpp"
#include "platform/windows/file_digest.hpp"
#include "platform/windows/mumu_binding.hpp"
#include "platform/windows/runtime_files.hpp"
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
auto wvd_recovery_policy(devices::LifecycleTarget target) {
    return [target = std::move(target)](const contracts::SessionResult &result,
        const contracts::BusinessRunState &business, unsigned attempt)
        -> std::optional<devices::LifecyclePlan> {
        if (result.end != contracts::SessionEnd::Failed || !result.quiescent ||
            attempt < 1 || attempt > 3) return std::nullopt;
        const auto facts = business.summary();
        if (games::tasks::handoff_has_unconfirmed_effect(facts)) return std::nullopt;
        const auto leap = facts.value("handoff_intent", J(nullptr));
        const bool deferred_leap = result.reason == "leap.unknown" && attempt == 1 &&
            leap.is_object() && leap.value("kind", "") == "wait_7300" &&
            facts.at("leap_wait").value("active", false);
        const bool frozen_pause = result.reason == "pause.physics_frozen" &&
            !leap.is_object() && !facts.at("leap_wait").value("active", false);
        if (!deferred_leap && !frozen_pause) return std::nullopt;
        devices::LifecyclePlan plan;
        plan.target = target;
        plan.attempt = attempt;
        if (deferred_leap) {
            const auto remaining = 7300000LL - facts.at("leap_wait").at("elapsed_ms").get<std::int64_t>();
            plan.defer_for = std::chrono::milliseconds{std::max(0LL, remaining)};
        }
        if (target.vpn_required)
            plan.operations.push_back(devices::LifecycleOperation::EnsureVpn);
        plan.operations.push_back(devices::LifecycleOperation::StopApplication);
        plan.operations.push_back(devices::LifecycleOperation::StartApplication);
        return plan;
    };
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
void launch_selected_instance(const std::filesystem::path &launcher, int index) {
    require(launcher.filename() != "MuMuManager.exe" &&
                std::filesystem::is_regular_file(launcher),
            "MUMU_LAUNCHER_REQUIRED");
    std::wstring command = L"\"" + launcher.wstring() + L"\" control -v " +
                           std::to_wstring(index);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    require(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                           nullptr, launcher.parent_path().c_str(), &startup, &process),
            "MUMU_START_FAILED");
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
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
             {"outcome", data.value("kind", "sequence") == "failure" ? "failure" : "success"},
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
        const auto kind = outcome == "failure" ? "failure"
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
    const auto cache_root = data_root / "asset-cache";
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
    // 首次引入公共定义；已有同 ID 的用户编辑版本绝不覆盖。公共定义仍存于同一 WorkflowRepository。
    const auto semantic_path = author_bundle_.root / "parameters/semantic-assets.json";
    if (std::filesystem::is_regular_file(semantic_path)) semantic_catalogue_ = load_json(semantic_path);
    const auto library_path = author_bundle_.root / "parameters/public-flows.json";
    if (std::filesystem::is_regular_file(library_path)) {
        std::set<std::string> existing;
        for (const auto &entry : workflow_store_->list()) existing.insert(entry.at("id").get<std::string>());
        const auto library = load_json(library_path);
        require(library.is_array(), "FLOW_SEED_INVALID");
        for (const auto &document : library) {
            const auto id = document.at("flow").at("id").get<std::string>();
            if (!existing.contains(id)) { workflow_store_->create(document); existing.insert(id); }
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
    coordinator_ = std::make_unique<runtime::NativeRunCoordinator>(paths_.data_root / "runs", 1024);
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
    return {{"profile", values}, {"revision", stored.at("revision")},
            {"effective_source", task_specific ? "任务覆盖" : "默认配置"},
            {"task_override_active", task_specific && !task.empty() &&
                 stored.value("task_overrides", J::object()).contains(task)}};
}

Application::J Application::profile_for_task(const std::string &task_id) const {
    (void)catalog_->at(task_id);
    const auto stored = profile_store_->load();
    auto values = effective_profile_values(task_id, stored);
    const bool overridden = values.value("TASK_SPECIFIC_CONFIG", false);
    return {{"profile", values}, {"revision", stored.at("revision")},
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
                    const auto variants = entry.value("variants", J::object());
                    for (const auto &[locale, variant] : variants.items()) { (void)variant; locales.push_back(locale); }
                    result.push_back({{"value", id}, {"label", entry.value("label", id)},
                        {"category", entry.value("category", "未分类")}, {"role", entry.value("role", "observation")},
                        {"locales", locales}});
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
                                         J{{"value", "右下角色"}, {"label", "右下角色"}},
                                         J{{"value", "低生命值"}, {"label", "低生命值"}},
                                         J{{"value", "不可用"}, {"label", "不可用"}}})},
            {"skill_frequencies", options({J{{"value", ""}, {"label", "沿用旧版消费规则"}}})},
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
    const auto old_names = strategy_names(document.at("values"));
    if (request.contains("strategy_renames")) {
        const auto &renames = request.at("strategy_renames");
        require(renames.is_object() && renames.size() <= 128, "STRATEGY_RENAME_INVALID");
        const auto desired_names = strategy_names(request.at("profile"));
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
        const auto values = request.contains("profile") ? request.at("profile")
                                                         : request.at("document").at("values");
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
    return {{"profile", values}, {"revision", saved.at("revision")},
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
        require(!handoff_status_.is_object() ||
            (handoff_status_.value("state", "") != "watching" &&
             handoff_status_.value("state", "") != "starting"),
            "TASK_HANDOFF_IN_PROGRESS");
        require(operation_.value("state", "idle") != "running", "DEVICE_OPERATION_BUSY");
    }
    if (device_worker_.joinable()) device_worker_.join();
    cancel_operation_ = false;
    {
        std::lock_guard lock(mutex_);
        operation_ = {{"state", "running"}, {"name", name}, {"error", nullptr}};
    }
    try {
        device_worker_ = std::jthread([this, name = std::move(name), job = std::move(job)] {
            std::string failure;
            try {
                if (stopping_ || cancel_operation_) throw std::runtime_error("PREPARATION_CANCELLED");
                job();
            } catch (const std::exception &error) { failure = error.what(); }
              catch (...) { failure = "APPLICATION_OPERATION_EXCEPTION"; }
            std::lock_guard finished(mutex_);
            operation_ = {{"state", failure.empty() ? "completed" : "failed"},
                          {"name", name}, {"error", failure.empty() ? J(nullptr) : J(failure)}};
            if (name == "start_task" || name == "start_workflow") {
                submission_["state"] = failure.empty() ? "submitted" :
                    failure == "PREPARATION_CANCELLED" ? "cancelled" : "failed";
                submission_["error"] = failure.empty() ? J(nullptr) : J(failure);
                submissions_.at(submission_.at("request_id").get<std::string>())["receipt"] = submission_;
            }
        });
    } catch (...) {
        std::lock_guard lock(mutex_);
        operation_ = {{"state", "failed"}, {"name", "worker"}, {"error", "WORKER_START_FAILED"}};
        throw;
    }
}

Application::J Application::queue_run(const std::string &kind, const J &request,
                                      const J &identity, std::function<J()> prepare) {
    std::lock_guard command(command_mutex_);
    const auto id = checked_request_id(request);
    const auto signature = identity.dump();
    {
        std::lock_guard lock(mutex_);
        if (auto it = submissions_.find(id); it != submissions_.end()) {
            require(it->second.at("identity") == signature, "IDEMPOTENCY_CONFLICT");
            auto result = it->second.at("receipt");
            if (auto known = coordinator_->request_snapshot(id)) {
                result["run"] = storage::snapshot_json(*known);
                result["run_id"] = known->run_id;
            }
            result["accepted"] = true;
            result["replayed"] = true;
            return result;
        }
        require(!stopping_, "APPLICATION_STOPPING");
        require(!run_active(), "RUN_ACTIVE");
        require(operation_.value("state", "idle") != "running", "DEVICE_OPERATION_BUSY");
        require(submissions_.size() < 256, "REQUEST_HISTORY_CAPACITY_EXCEEDED");
        submission_ = {{"request_id", id}, {"kind", kind}, {"state", "preparing"},
                       {"error", nullptr}, {"accepted", true}};
        submissions_[id] = {{"identity", signature}, {"receipt", submission_}};
    }
    try {
        start_device_job(kind, [this, id, prepare = std::move(prepare)] {
            const auto result = prepare();
            std::lock_guard lock(mutex_);
            submissions_.at(id)["run_id"] = result.at("run_id");
        });
    } catch (const std::exception &error) {
        std::lock_guard lock(mutex_);
        submission_["state"] = "failed";
        submission_["error"] = error.what();
        submissions_.at(id)["receipt"] = submission_;
        throw;
    }
    return {{"accepted", true}, {"request_id", id}, {"submission_state", "preparing"}};
}

Application::J Application::start_task(const J &request) {
    std::lock_guard command(command_mutex_);
    auto frozen = request;
    frozen["request_id"] = checked_request_id(request);
    const auto stored = profile_store_->load();
    const auto locale = frozen.value("resource_locale", std::string{"en"});
    require(locale == "en" || locale == "zh-Hant", "TASK_RESOURCE_LOCALE_UNSUPPORTED");
    if (request.contains("profile_revision"))
        require(request.at("profile_revision") == stored.at("revision"), "PROFILE_REVISION_MISMATCH");
    return queue_run("start_task", frozen,
        {{"kind", "task"}, {"request", frozen}, {"profile_revision", stored.at("revision")}},
        [this, frozen, stored] {
            const auto backend = ensure_connected_for_run(stored);
            auto result = prepare_task(frozen, stored, backend);
            const auto &selected = stored.at("values").at("FARM_TARGET");
            const auto task_id = frozen.value("task_id", selected.is_string()
                ? selected.get<std::string>() : std::string{});
            const auto source_values = effective_profile_values(task_id, stored);
            if (task_id != "7000G" && source_values.at("ACTIVE_BEG_MONEY").get<bool>())
                watch_task_handoff(stored, source_values, backend, frozen.at("request_id"));
            return result;
        });
}
Application::J Application::start_workflow(const std::string &flow_id, const J &request) {
    std::lock_guard command(command_mutex_);
    auto frozen = request;
    frozen["request_id"] = checked_request_id(request);
    const auto stored = profile_store_->load();
    if (request.contains("profile_revision"))
        require(request.at("profile_revision") == stored.at("revision"), "PROFILE_REVISION_MISMATCH");
    const auto document = workflow_store_->read(flow_id);
    require(frozen.value("revision", std::string{}) == document.at("revision").get<std::string>(),
            "WORKFLOW_REVISION_MISMATCH");
    const auto library = workflow_store_->snapshot_closure(document);
    return queue_run("start_workflow", frozen,
        {{"kind", "workflow"}, {"flow_id", flow_id}, {"request", frozen},
         {"profile_revision", stored.at("revision")}, {"library", library}},
        [this, flow_id, frozen, stored, document, library] {
            // 不齐全的语言素材/循环引用在连接和启动模拟器之前暴露。
            const auto locale = frozen.value("resource_locale", document.at("execution").value("resource_locale", std::string{}));
            (void)games::tasks::PublicFlowLibrary(library, semantic_catalogue_).task_profiles(
                document, frozen.value("arguments", J::object()), locale);
            require(!stopping_ && !cancel_operation_, "PREPARATION_CANCELLED");
            const auto backend = ensure_connected_for_run(stored);
            return prepare_workflow(flow_id, frozen, stored, document, backend, library);
        });
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
        launch_selected_instance(path, index);
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
        return backend;
    }
    // 在现有准备作业中同步调用同一连接实现，不再嵌套第二个作业或第二个控制器。
    connect_selected_device({{"emulator_path", platform::utf8(path)}, {"emulator_index", index},
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
        if (old) old->disconnect(); // 失败时 backend_/租约仍由产品持有。
        {
            std::lock_guard lock(mutex_);
            require(backend_ == old, "DEVICE_OWNER_CHANGED");
            backend_.reset();
            preview_lease_.reset();
            frame_png_.clear();
            frame_info_ = nullptr;
            frame_captured_at_.reset();
        }
    });
    return device_status();
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
    const auto snapshot = coordinator_->snapshot();
    auto value = storage::snapshot_json(snapshot);
    const auto event_page = coordinator_->events();
    value["events"] = event_page;
    const auto directory = coordinator_->run_directory();
    value["run_directory"] = directory.empty() ? J(nullptr) : J(platform::utf8(directory));
    value["result"] = contracts::name(snapshot.state);
    value["error_code"] = snapshot.reason.empty() ? J(nullptr) : J(snapshot.reason);
    value["message"] = snapshot.reason.empty() ? J(nullptr) : J(snapshot.reason);
    const auto diagnostic_summary = coordinator_->diagnostics();
    J diagnostics = J::array();
    for (const auto &entry : diagnostic_summary.value("entries", J::array())) {
        auto item = entry;
        item["label"] = entry.value("reason", entry.value("stage", "诊断"));
        if (entry.value("status", "") == "saved" && entry.contains("id"))
            item["image_url"] = "/api/v1/runs/current/diagnostics/" +
                                std::to_string(entry.at("id").get<std::uint64_t>()) + ".png";
        if (entry.contains("frame") && entry.at("frame").is_object())
            item["frame_age_ms"] = entry.at("frame").value("age_at_submit_ms", 0);
        diagnostics.push_back(std::move(item));
    }
    value["diagnostics"] = std::move(diagnostics);
    std::map<std::string, std::string> mapping;
    J source_paths = J::object();
    {
        std::lock_guard lock(mutex_);
        value["workflow_id"] = active_workflow_id_.empty() ? J(nullptr) : J(active_workflow_id_);
        value["workflow_revision"] = active_workflow_revision_.empty()
                                         ? J(nullptr) : J(active_workflow_revision_);
        mapping = active_pipeline_to_node_;
        source_paths = active_source_paths_;
        value["submission"] = submission_;
        value["busy"] = run_active() || operation_.value("state", "idle") == "running";
        value["task_name"] = active_task_name_.empty() ? J(nullptr) : J(active_task_name_);
        value["elapsed_seconds"] = active_started_
            ? std::chrono::duration_cast<std::chrono::seconds>(
                  std::chrono::steady_clock::now() - *active_started_).count()
            : 0;
    }
    value["statistics"] = {
        {"已完成业务段", snapshot.completed_business_units},
        {"动作尝试", snapshot.inputs.attempted},
        {"动作执行", snapshot.inputs.backend_called},
        {"动作拒绝", snapshot.inputs.rejected}};
    {
        std::lock_guard lock(mutex_);
        value["handoff"] = handoff_status_;
    }
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
    return value;
}

runtime::NativeRunDefinition Application::assemble_task(const J &request, const J &stored,
    const devices::LifecycleTarget &lifecycle, std::optional<J> frozen_values,
    bool continuation) {
    if (stopping_ || cancel_operation_) throw std::runtime_error("PREPARATION_CANCELLED");
    const auto &stored_values = stored.at("values");
    const auto task_id = request.value("task_id", stored_values.at("FARM_TARGET").is_string()
        ? stored_values.at("FARM_TARGET").get<std::string>() : std::string{});
    require(!task_id.empty(), "TASK_NOT_SELECTED");
    const auto values = frozen_values ? *frozen_values : effective_profile_values(task_id, stored);
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
    const auto plan = games::WvdTaskPlan::parse(task);
    auto workflow = [&] {
        if (task.type == "dungeon") return games::tasks::dungeon_iteration(plan, values, available_images_);
        if (task_id == "Scorpionesses" || task_id == "Scorpionesses_plus_6_hands" || task_id == "jier")
            return games::tasks::bounty_cycle(task, values, available_images_);
        if (task_id == "fishing" || task_id == "fishing2") return games::tasks::fishing_cycle(task, values, available_images_);
        if (task_id == "SSC-goldenchest") return games::tasks::golden_chest_cycle(task, values, available_images_);
        if (task_id == "sandman") return games::tasks::sandman_cycle(task, values, available_images_);
        if (task_id == "7000G") return games::tasks::gold_income_cycle(task);
        if (task_id == "LBC-oneGorgon") return games::tasks::bull_cave_cycle(task, values, available_images_);
        if (task_id == "steeltrail") return games::tasks::steel_trial_cycle(task, values, available_images_);
        if (task_id == "repelEnemyForces") return games::tasks::repel_forces_cycle(task, values, available_images_);
        if (task_id == "lovesleep") return games::tasks::sleep_visits(task, values);
        if (task_id == "manualSepDemon") return games::tasks::manual_separation(task, values, available_images_);
        if (task_id == "FFXI-Org") return games::tasks::mining_iteration(task, values);
        if (task_id == "darkLight") return games::tasks::dark_light(task, values, available_images_);
        if (task_id == "gaintKiller") return games::tasks::giant_iteration(task, values, available_images_);
        if (task_id == "fortress-B8F_trap") return games::tasks::fortress_trap_iteration(task, values, available_images_);
        throw std::runtime_error("TASK_EXECUTION_NOT_IMPLEMENTED");
    }();
    workflow = games::recovery::with_boot_recovery(workflow, true);
    require(workflow.nodes.contains("Boot_Entry"), "PRODUCTION_BOOT_ENTRY_MISSING");
    games::tasks::localize_task_assets(workflow, semantic_catalogue_,
        request.value("resource_locale", std::string{"en"}));
    const auto request_id = checked_request_id(request);
    const auto destination = paths_.data_root / "published" / request_id;
    auto publication = games::tasks::publish_native(workflow, author_bundle_,
        destination, aliases_);
    const auto &root = publication.program.definitions.at(publication.program.root_definition);
    require(root.steps.contains(workflow.checkpoint), "NATIVE_CHECKPOINT_MISSING");
    const auto checkpoint_source = root.steps.at(workflow.checkpoint).source_path;
    runtime::NativeUnit unit{std::move(publication.program),
        std::move(publication.bundle), games::vision::native_handlers(aliases_),
        checkpoint_source, workflow.time_limit};
    std::size_t count = 1;
    if (task_id == "Scorpionesses_plus_6_hands") count = 4;
    else if (task_id == "Scorpionesses" || task_id == "jier") count = 3;
    else if (task_id == "SSC-goldenchest" || task_id == "sandman" ||
             task_id == "manualSepDemon") count = 2;
    else if (task_id == "LBC-oneGorgon") count = values.at("ACTIVE_REST").get<bool>() ? 3 : 2;
    else if (task_id == "repelEnemyForces") count = games::quests::RepelForces::rounds(values) + 2;
    else if (task_id == "lovesleep") count = games::quests::SleepVisits::units;
    runtime::NativeRunDefinition definition;
    definition.request_id = request_id;
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
    definition.recovery = wvd_recovery_policy(lifecycle);
    if (handoff_source.is_object()) {
        definition.handoff_ready = [source = handoff_source](
            const contracts::SessionResult &last, const contracts::BusinessRunState &state) {
            if (last.reason != "leap.unknown" || !last.quiescent) return false;
            const auto facts = state.summary();
            const auto intent = facts.value("handoff_intent", J(nullptr));
            return intent.is_object() && intent.value("kind", "") == "turn_to_7000G" &&
                facts.at("handoff_source") == source &&
                intent.at("run_identity") == facts.at("run_identity") &&
                intent.at("generation") == facts.at("generation") &&
                intent.at("unknown_samples").get<std::uint64_t>() >= 5 &&
                !games::tasks::handoff_has_unconfirmed_effect(facts);
        };
    }
    return definition;
}
Application::J Application::prepare_task(const J &request, const J &stored,
    std::shared_ptr<devices::DeviceConnection> backend,
    std::optional<J> frozen_values, J handoff_parent) {
    require(bool(backend), "DEVICE_NOT_CONNECTED");
    backend->set_vpn_required(stored.at("values").at("AUTO_START_CLASH").get<bool>());
    auto definition = assemble_task(request, stored, backend->lifecycle_target(),
        std::move(frozen_values), handoff_parent.is_object());
    definition.handoff_parent = std::move(handoff_parent);
    const auto &stored_values = stored.at("values");
    const auto selected = stored_values.at("FARM_TARGET").is_string()
        ? stored_values.at("FARM_TARGET").get<std::string>() : std::string{};
    const auto task_id = request.value("task_id", selected);
    const auto &task = catalog_->at(task_id);
    const auto request_id = definition.request_id;
    std::lock_guard command(command_mutex_);
    require(!stopping_ && !cancel_operation_, "PREPARATION_CANCELLED");
    { std::lock_guard lock(mutex_); preview_lease_.reset(); }
    const auto snapshot = coordinator_->start(std::move(definition), backend);
    {
        std::lock_guard lock(mutex_);
        active_workflow_id_ = "task:" + task_id;
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

void Application::watch_task_handoff(const J &stored, J source_values,
    std::shared_ptr<devices::DeviceConnection> backend, std::string request_id) {
    if (handoff_worker_.joinable()) handoff_worker_.join();
    require(!stopping_ && !cancel_operation_, "PREPARATION_CANCELLED");
    {
        std::lock_guard lock(mutex_);
        handoff_status_ = {{"state", "watching"}, {"source_request_id", request_id}};
    }
    handoff_worker_ = std::jthread([this, stored, source_values = std::move(source_values),
        backend = std::move(backend), request_id = std::move(request_id)](std::stop_token stop) mutable {
        const auto update = [this](const std::string &state, const J &detail) {
            std::lock_guard lock(mutex_);
            handoff_status_ = {{"state", state}, {"detail", detail}};
        };
        try {
            while (!stop.stop_requested() && !stopping_ && !cancel_operation_) {
                if (coordinator_->wait_for(250ms)) break;
                if (coordinator_->wait_for_worker(0ms) && !coordinator_->snapshot().quiescent) {
                    update("blocked", "SOURCE_CLEANUP_PENDING");
                    return;
                }
            }
            if (stop.stop_requested() || stopping_ || cancel_operation_) return;
            const auto snapshot = coordinator_->request_snapshot(request_id);
            require(snapshot && snapshot->quiescent && snapshot->result_saved &&
                snapshot->storage_error.empty(), "HANDOFF_SOURCE_NOT_COMMITTED");
            if (snapshot->outcome_category != "handoff_ready") {
                update("not_requested", snapshot->reason);
                return;
            }
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
        } catch (const std::exception &error) {
            update("blocked", error.what());
        } catch (...) {
            update("blocked", "HANDOFF_UNEXPECTED_EXCEPTION");
        }
    });
}

Application::J Application::list_workflows() const {
    return {{"workflows", workflow_store_->list()}};
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
    const J &library_snapshot) {
    require(bool(backend), "DEVICE_NOT_CONNECTED");
    if (stopping_ || cancel_operation_) throw std::runtime_error("PREPARATION_CANCELLED");
    const auto workflow_revision = document.at("revision").get<std::string>();
    const auto workflow_name = document.at("flow").at("name").get<std::string>();
    backend->set_vpn_required(stored.at("values").at("AUTO_START_CLASH").get<bool>());
    std::map<std::string, std::string> pipeline_to_node;
    J source_paths = J::object();
    auto definition = assemble_workflow(request, stored, std::move(document),
                                        backend->lifecycle_target(), &pipeline_to_node, library_snapshot, &source_paths);
    const auto request_id = definition.request_id;
    std::lock_guard handoff(command_mutex_);
    require(!stopping_ && !cancel_operation_, "PREPARATION_CANCELLED");
    {
        std::lock_guard lock(mutex_);
        preview_lease_.reset();
    }
    const auto snapshot = coordinator_->start(std::move(definition), backend);
    {
        std::lock_guard lock(mutex_);
        active_workflow_id_ = flow_id;
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

runtime::NativeRunDefinition Application::assemble_workflow(
    const J &request, const J &stored, J document, const devices::LifecycleTarget &lifecycle,
    std::map<std::string, std::string> *pipeline_to_node, const J &library_snapshot, J *source_paths) {
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
    const auto locale = request.value("resource_locale", document.at("execution").value("resource_locale", std::string{}));
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
                return games::combat::fight_encounter(values, available_images_, 16);
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
    auto executable = games::recovery::with_boot_recovery(compiled.workflow, true);
    games::tasks::localize_task_assets(executable, semantic_catalogue_, locale);
    require(executable.nodes.contains("Boot_Entry"), "PRODUCTION_BOOT_ENTRY_MISSING");
    const auto request_id = checked_request_id(request);
    const auto destination = paths_.data_root / "published" / request_id;
    const auto provenance = executable.authoring.value("source_paths", J::object());
    auto publication = games::tasks::publish_native(executable, author_bundle_,
        destination, aliases_, provenance);
    const auto &root = publication.program.definitions.at(publication.program.root_definition);
    require(root.steps.contains(executable.checkpoint), "NATIVE_CHECKPOINT_MISSING");
    const auto checkpoint_source = root.steps.at(executable.checkpoint).source_path;
    runtime::NativeRunDefinition definition;
    definition.request_id = request_id;
    definition.units.push_back({std::move(publication.program),
        std::move(publication.bundle), games::vision::native_handlers(aliases_),
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
    definition.recovery = wvd_recovery_policy(lifecycle);
    if (source_paths) *source_paths = executable.authoring.value("source_paths", J::object());
    if (pipeline_to_node) {
        pipeline_to_node->clear();
        for (const auto &[pipeline, node] : compiled.pipeline_to_node)
            (*pipeline_to_node)["Task_" + pipeline] = node;
    }
    return definition;
}

Application::J Application::stop_run(std::optional<std::uint64_t> requested_run_id,
                                    const std::string &requested_submission) {
    std::lock_guard command(command_mutex_);
    const auto current = coordinator_->snapshot();
    if (requested_run_id)
        require(current.run_id == *requested_run_id, "RUN_ID_MISMATCH");
    if (!requested_submission.empty()) {
        std::lock_guard lock(mutex_);
        require(submission_.is_object() && submission_.value("request_id", "") == requested_submission,
                "SUBMISSION_ID_MISMATCH");
    }
    cancel_operation_ = true;
    handoff_worker_.request_stop();
    {
        std::lock_guard lock(mutex_);
        if (handoff_status_.is_object() &&
            (handoff_status_.value("state", "") == "watching" ||
             handoff_status_.value("state", "") == "starting"))
            handoff_status_["state"] = "cancelled";
    }
    coordinator_->request_stop();
    auto result = run_status();
    result["accepted"] = true;
    return result;
}

api::DynamicReply Application::diagnostic_image(const std::string &name) const {
    require(name.size() > 4 && name.ends_with(".png"), "DIAGNOSTIC_NAME_INVALID");
    std::uint64_t id{};
    const auto digits = std::string_view(name).substr(0, name.size() - 4);
    const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), id);
    require(error == std::errc{} && end == digits.data() + digits.size(),
            "DIAGNOSTIC_NAME_INVALID");
    const auto relative = "diagnostics/" + std::to_string(id) + ".png";
    bool indexed = false;
    for (const auto &entry : coordinator_->diagnostics().value("entries", J::array()))
        if (entry.value("status", "") == "saved" && entry.value("path", "") == relative) {
            indexed = true;
            break;
        }
    require(indexed, "DIAGNOSTIC_NOT_FOUND");
    const auto file = coordinator_->run_directory() / relative;
    std::ifstream input(file, std::ios::binary);
    require(bool(input), "DIAGNOSTIC_NOT_FOUND");
    return {api::http::status::ok,
            std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()),
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
        if (mode == "template") {
            recognition["type"] = "template";
            recognition["image"] = condition.at("image");
            recognition["threshold"] = condition.value("threshold", 0.8);
        } else if (mode == "ocr") {
            recognition["type"] = "ocr";
            recognition["expected"] = condition.at("expected");
        } else {
            recognition["type"] = "custom";
            recognition["binding"] = "WvdVision";
            recognition["parameters"] = condition;
        }
    }
    const auto parsed = recognition::parse_request(recognition);
    recognition::Service recognizer(std::move(bundle),
        games::vision::native_handlers(aliases_));
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
            if (run)
                suffix.resize(suffix.size() - 4);
            require(!suffix.empty() && suffix.find('/') == std::string::npos,
                    "WORKFLOW_PATH_INVALID");
            if (run && method == api::http::verb::post)
                return json_reply(start_workflow(suffix, parse_body(request)), api::http::status::accepted);
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
        if (path == "/api/v1/device/frame" && (method == api::http::verb::get || method == api::http::verb::head)) {
            std::lock_guard lock(mutex_);
            require(!frame_png_.empty(), "FRAME_NOT_AVAILABLE");
            return {api::http::status::ok,
                    std::string(reinterpret_cast<const char *>(frame_png_.data()), frame_png_.size()),
                    "image/png"};
        }
        if (path == "/api/v1/runs/current" && (method == api::http::verb::get || method == api::http::verb::head))
            return json_reply(run_status());
        constexpr std::string_view diagnostic_prefix = "/api/v1/runs/current/diagnostics/";
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
    std::lock_guard command(command_mutex_);
    stopping_ = true;
    cancel_operation_ = true;
    handoff_worker_.request_stop();
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
