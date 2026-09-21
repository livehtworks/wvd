#include "application.hpp"

#include "games/wvd/chest/chest.hpp"
#include "games/wvd/combat/turn.hpp"
#include "games/wvd/diagnostics.hpp"
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
#include "games/wvd/tasks/manual_separation.hpp"
#include "games/wvd/tasks/mining.hpp"
#include "games/wvd/tasks/repel_forces.hpp"
#include "games/wvd/tasks/sandman.hpp"
#include "games/wvd/tasks/sleep_visits.hpp"
#include "games/wvd/tasks/steel_trial.hpp"
#include "games/wvd/tasks/workflow_session.hpp"
#include "games/wvd/tasks/author_workflow.hpp"
#include "games/wvd/vision/recognizers.hpp"
#include "maafw/buffers.hpp"
#include "maafw/gateway.hpp"
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
#include <windows.h>

#include <commdlg.h>

namespace wvd::app {
using namespace std::chrono_literals;
namespace {
using J = nlohmann::json;
void require(bool condition, const char *code) {
    if (!condition)
        throw std::runtime_error(code);
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
    int accumulator = 0, bits = -8;
    bool padding = false;
    for (const auto byte : text) {
        if (byte == '=') {
            padding = true;
            continue;
        }
        require(!padding && table[static_cast<unsigned char>(byte)] >= 0,
                "PROBE_IMAGE_ENCODING_INVALID");
        accumulator = (accumulator << 6) + table[static_cast<unsigned char>(byte)];
        bits += 6;
        if (bits >= 0) {
            result.push_back(static_cast<std::uint8_t>((accumulator >> bits) & 0xff));
            bits -= 8;
        }
    }
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
    for (const auto &source : ui.at("nodes")) {
        const auto &data = source.at("data");
        J parameters = data.value("parameters", J::object());
        J node{{"id", source.at("id")},
               {"type", data.at("node_type")},
               {"name", data.value("label", source.at("id").get<std::string>())},
               {"parameters", std::move(parameters)}};
        if (source.contains("repeat_limit"))
            node["repeat_limit"] = source.at("repeat_limit");
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
    const std::string success = "debug_success";
    const std::string failure = "debug_failure";
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

Application::Application(ApplicationPaths paths) : paths_(std::move(paths)) {
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
    registry_ = std::make_shared<runtime::BehaviorRegistry>("windows-functional-1");
    games::vision::register_wvd(*registry_);
    games::register_wvd_state(*registry_);
    games::register_wvd_confirmations(*registry_);
    games::combat::register_combat(*registry_);
    games::chest::register_chest(*registry_);
    games::recovery::register_recovery(*registry_);
    registry_->seal();
    coordinator_ = std::make_unique<runtime::RunCoordinator>(paths_.data_root / "runs", registry_, 1024);
    operation_ = {{"state", "idle"}, {"name", nullptr}, {"error", nullptr}};
}

Application::~Application() { stop(); }

bool Application::run_active() const {
    return coordinator_ && !terminal(coordinator_->snapshot().state);
}

Application::J Application::profile() const {
    const auto stored = profile_store_->load();
    const auto &values = stored.at("values");
    const auto task = values.value("FARM_TARGET", std::string{});
    const bool task_specific = values.value("TASK_SPECIFIC_CONFIG", false);
    return {{"profile", values}, {"revision", stored.at("revision")},
            {"effective_source", task_specific ? "任务覆盖" : "默认配置"},
            {"task_override_active", task_specific && !task.empty() &&
                 stored.value("task_overrides", J::object()).contains(task)}};
}

Application::J Application::profile_for_task(const std::string &task_id) const {
    (void)catalog_->at(task_id);
    const auto stored = profile_store_->load();
    auto values = effective_profile_values(task_id);
    const bool overridden = stored.value("task_overrides", J::object()).contains(task_id);
    return {{"profile", values}, {"revision", stored.at("revision")},
            {"effective_source", overridden ? "任务覆盖" : "默认配置"},
            {"task_override_active", overridden}};
}

Application::J Application::effective_profile_values(const std::string &task_id) const {
    const auto &task = catalog_->at(task_id);
    const auto stored = profile_store_->load();
    auto values = stored.value("default_values", stored.at("values"));
    const auto overrides = stored.value("task_overrides", J::object());
    const bool overridden = overrides.contains(task_id);
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
                  {"defaults", {{"binding", "combat"},
                    {"condition", {{"mode", "combat_active"}}},
                    {"arguments", {{"operation", "auto_confirmed"}, {"index", 0}}}}}},
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
            {"skill_frequencies", options({J{{"value", "重复"}, {"label", "重复"}},
                                             J{{"value", "每场战斗仅一次"}, {"label", "每场战斗仅一次"}},
                                             J{{"value", "每次启动仅一次"}, {"label", "每次启动仅一次"}}})},
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
        const auto task = values.value("FARM_TARGET", std::string{});
        const bool task_specific = values.value("TASK_SPECIFIC_CONFIG", false) && !task.empty();
        if (task_specific) {
            J task_values = J::object();
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
    const auto saved = profile_store_->compare_exchange(expected, document);
    const auto &values = saved.at("values");
    const auto task = values.value("FARM_TARGET", std::string{});
    const bool task_specific = values.value("TASK_SPECIFIC_CONFIG", false);
    return {{"profile", values}, {"revision", saved.at("revision")},
            {"effective_source", task_specific ? "任务覆盖" : "默认配置"},
            {"task_override_active", task_specific && !task.empty() &&
                 saved.value("task_overrides", J::object()).contains(task)}};
}

void Application::start_device_job(std::string name, std::function<void()> job) {
    std::lock_guard lock(mutex_);
    require(!run_active(), "RUN_ACTIVE");
    require(operation_.value("state", "idle") != "running", "DEVICE_OPERATION_BUSY");
    if (device_worker_.joinable())
        device_worker_.join();
    operation_ = {{"state", "running"}, {"name", name}, {"error", nullptr}};
    device_worker_ = std::jthread([this, name = std::move(name), job = std::move(job)] {
        try {
            job();
            std::lock_guard finished(mutex_);
            operation_ = {{"state", "completed"}, {"name", name}, {"error", nullptr}};
        } catch (const std::exception &error) {
            std::lock_guard failed(mutex_);
            operation_ = {{"state", "failed"}, {"name", name}, {"error", error.what()}};
        }
    });
}

Application::J Application::connect_device(const J &request) {
    require(request.is_object(), "DEVICE_REQUEST_INVALID");
    {
        std::lock_guard lock(mutex_);
        require(!backend_, "DEVICE_ALREADY_CONNECTED");
    }
    const auto path = maafw::path_from_utf8(request.contains("emulator_path")
                                                ? request.at("emulator_path") : request.at("path"));
    const auto manager = manager_from_path(path);
    const auto index = request.value("emulator_index", request.value("index", 0));
    const auto serial = request.value("adb_address", request.value("serial", std::string{}));
    require(!serial.empty(), "ADB_ADDRESS_REQUIRED");
    const bool vpn = request.value("vpn_required", request.value("auto_start_clash", false));
    start_device_job("connect", [this, manager, path, index, serial, vpn] {
        auto binding = platform::create_mumu_binding(manager, index, serial);
        if (!binding.at("initial_manager").value("is_android_started", false)) {
            launch_selected_instance(path, index);
            const auto deadline = std::chrono::steady_clock::now() + 120s;
            do {
                std::this_thread::sleep_for(1s);
                binding = platform::create_mumu_binding(manager, index, serial);
                if (binding.at("initial_manager").value("is_android_started", false))
                    break;
            } while (!stopping_ && std::chrono::steady_clock::now() < deadline);
            require(binding.at("initial_manager").value("is_android_started", false),
                    "MUMU_START_TIMEOUT");
        }
        binding["launcher"] = maafw::utf8(path);
        binding["application_id"] = "jp.co.drecom.wizardry.daphne";
        binding["vpn_required"] = vpn;
        auto lease = std::make_unique<platform::DeviceLease>(serial);
        auto next = std::make_shared<maafw::AdbBackend>(std::move(binding));
        require(next->connect(), "DEVICE_CONNECT_FAILED");
        auto frame = next->capture();
        std::lock_guard connected(mutex_);
        backend_ = std::move(next);
        preview_lease_ = std::move(lease);
        frame_png_ = std::move(frame.encoded);
        frame_captured_at_ = std::chrono::steady_clock::now();
        frame_info_ = {{"width", frame.size.width}, {"height", frame.size.height},
                       {"device_id", frame.device_id}, {"viewport", frame.viewport_id},
                       {"foreground_application", frame.foreground_application},
                       {"backend", frame.backend},
                       {"connection_generation", frame.connection_generation}};
    });
    return device_status();
}

Application::J Application::select_emulator_path() const {
    std::wstring selected(32768, L'\0');
    const auto current = profile().at("profile").value("EMU_PATH", std::string{});
    if (!current.empty()) {
        const auto wide = maafw::path_from_utf8(current).wstring();
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
    return {{"cancelled", false}, {"path", maafw::utf8(path)}};
}

Application::J Application::disconnect_device() {
    start_device_job("disconnect", [this] {
        std::shared_ptr<maafw::AdbBackend> old;
        std::unique_ptr<platform::DeviceLease> lease;
        {
            std::lock_guard lock(mutex_);
            old = std::move(backend_);
            lease = std::move(preview_lease_);
            frame_png_.clear();
            frame_info_ = nullptr;
            frame_captured_at_.reset();
        }
        if (old)
            old->disconnect();
    });
    return device_status();
}

Application::J Application::capture_device() {
    start_device_job("capture", [this] {
        std::shared_ptr<maafw::AdbBackend> backend;
        {
            std::lock_guard lock(mutex_);
            backend = backend_;
            if (backend && !preview_lease_)
                preview_lease_ = std::make_unique<platform::DeviceLease>(
                    backend->lifecycle_target().device_id);
        }
        require(bool(backend), "DEVICE_NOT_CONNECTED");
        require(backend->connect(), "DEVICE_RECONNECT_FAILED");
        auto frame = backend->capture();
        std::lock_guard captured(mutex_);
        frame_png_ = std::move(frame.encoded);
        frame_captured_at_ = std::chrono::steady_clock::now();
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
    value["run_directory"] = directory.empty() ? J(nullptr) : J(maafw::utf8(directory));
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
    {
        std::lock_guard lock(mutex_);
        value["workflow_id"] = active_workflow_id_.empty() ? J(nullptr) : J(active_workflow_id_);
        value["workflow_revision"] = active_workflow_revision_.empty()
                                         ? J(nullptr) : J(active_workflow_revision_);
        mapping = active_pipeline_to_node_;
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
    if (event_page.contains("events")) {
        const auto &events = event_page.at("events");
        for (auto it = events.rbegin(); it != events.rend(); ++it) {
            if (!it->contains("node_id") || !it->at("node_id").is_string())
                continue;
            const auto pipeline = it->at("node_id").get<std::string>();
            if (const auto found = mapping.find(pipeline); found != mapping.end()) {
                value["step_name"] = pipeline;
                value["current_node_id"] = found->second;
                if (snapshot.state == contracts::RunState::Failed)
                    value["failed_node_id"] = found->second;
                break;
            }
        }
    }
    return value;
}

Application::J Application::start_task(const J &request) {
    std::shared_ptr<maafw::AdbBackend> backend;
    {
        std::lock_guard lock(mutex_);
        require(operation_.value("state", "idle") != "running", "DEVICE_OPERATION_BUSY");
        backend = backend_;
    }
    require(bool(backend), "DEVICE_NOT_CONNECTED");
    const auto stored = profile_store_->load();
    const auto &stored_values = stored.at("values");
    const auto task_id = request.value("task_id", stored_values.at("FARM_TARGET").is_string()
                                                     ? stored_values.at("FARM_TARGET").get<std::string>()
                                                     : std::string{});
    require(!task_id.empty(), "TASK_NOT_SELECTED");
    const auto values = effective_profile_values(task_id);
    const auto &task = catalog_->at(task_id);
    const auto plan = games::WvdTaskPlan::parse(task);
    auto workflow = [&] {
        if (task.type == "dungeon")
            return games::tasks::dungeon_iteration(plan, values, available_images_);
        if (task_id == "Scorpionesses" || task_id == "Scorpionesses_plus_6_hands" || task_id == "jier")
            return games::tasks::bounty_cycle(task, values, available_images_);
        if (task_id == "fishing" || task_id == "fishing2")
            return games::tasks::fishing_cycle(task, values, available_images_);
        if (task_id == "SSC-goldenchest")
            return games::tasks::golden_chest_cycle(task, values, available_images_);
        if (task_id == "sandman")
            return games::tasks::sandman_cycle(task, values, available_images_);
        if (task_id == "7000G")
            return games::tasks::gold_income_cycle(task);
        if (task_id == "LBC-oneGorgon")
            return games::tasks::bull_cave_cycle(task, values, available_images_);
        if (task_id == "steeltrail")
            return games::tasks::steel_trial_cycle(task, values, available_images_);
        if (task_id == "repelEnemyForces")
            return games::tasks::repel_forces_cycle(task, values, available_images_);
        if (task_id == "lovesleep")
            return games::tasks::sleep_visits(task, values);
        if (task_id == "manualSepDemon")
            return games::tasks::manual_separation(task, values, available_images_);
        if (task_id == "FFXI-Org")
            return games::tasks::mining_iteration(task, values);
        if (task_id == "darkLight")
            return games::tasks::dark_light(task, values, available_images_);
        if (task_id == "gaintKiller")
            return games::tasks::giant_iteration(task, values, available_images_);
        if (task_id == "fortress-B8F_trap")
            return games::tasks::fortress_trap_iteration(task, values, available_images_);
        throw std::runtime_error("TASK_EXECUTION_NOT_IMPLEMENTED");
    }();
    // 生产恢复策略会在新代次切换到 Boot_Entry。必须在发布前把该入口及其
    // 资源封入同一 Bundle；只挂 recover binding 会在游戏拉起后找不到节点。
    workflow = games::recovery::with_boot_recovery(workflow, true);
    const auto request_id = request.value("request_id", platform::unique_id());
    const auto destination = paths_.data_root / "published" / request_id;
    auto session = games::tasks::publish_workflow(workflow, author_bundle_, *registry_, destination,
                                                  aliases_);
    runtime::RunDefinition definition;
    definition.request_id = request_id;
    definition.initial = std::move(session);
    definition.max_business_units = 1;
    definition.state_factory = games::wvd_state_binding(values);
    definition.policy = {backend->lifecycle_target().device_id, "wvd",
                         "jp.co.drecom.wizardry.daphne", definition.initial.bundle.revision,
                         "900x1600", {900, 1600},
                         {contracts::ActionKind::Click, contracts::ActionKind::ClickKey,
                          contracts::ActionKind::Swipe},
                         {}, {"wvd"}, 2000ms};
    for (const auto &action : workflow.required_actions)
        definition.policy.permissions.insert(action_kind(action));
    const auto lifecycle = backend->lifecycle_target();
    definition.recover = games::recovery::recovery_binding(lifecycle, values);
    definition.recovery_limit = 3;
    if (task_id == "Scorpionesses" || task_id == "Scorpionesses_plus_6_hands" ||
        task_id == "jier")
        games::tasks::configure_bounty_units(definition,
                                              task_id == "Scorpionesses_plus_6_hands");
    else if (task_id == "fishing" || task_id == "fishing2")
        games::tasks::configure_fishing_units(definition, 1);
    else if (task_id == "SSC-goldenchest")
        games::tasks::configure_golden_chest_units(definition);
    else if (task_id == "sandman")
        games::tasks::configure_sandman_units(definition);
    else if (task_id == "LBC-oneGorgon")
        games::tasks::configure_bull_cave_units(definition, values.at("ACTIVE_REST").get<bool>());
    else if (task_id == "repelEnemyForces")
        games::tasks::configure_repel_forces_units(definition, values);
    else if (task_id == "lovesleep")
        games::tasks::configure_sleep_units(definition);
    else if (task_id == "manualSepDemon")
        games::tasks::configure_manual_separation_units(definition);
    // 多段配置会以 initial 为蓝本生成普通续段。先完成复制，再只给首段绑定
    // EnsureVpn；否则生命周期操作会被复制进续段，并被恢复边界校验拒绝。
    games::recovery::bind_initial_vpn(definition, lifecycle, values);
    {
        std::lock_guard lock(mutex_);
        // 预览与运行共用同一设备租约。启动前将所有权交给 RunCoordinator，
        // 运行结束后的下一次截图会重新取得预览租约。
        preview_lease_.reset();
    }
    const auto snapshot = coordinator_->start(std::move(definition), backend);
    {
        std::lock_guard lock(mutex_);
        active_workflow_id_ = "task:" + task_id;
        active_workflow_revision_ = stored.at("revision").get<std::string>();
        active_task_name_ = task.source.value("questName", task_id);
        active_started_ = std::chrono::steady_clock::now();
        active_pipeline_to_node_.clear();
    }
    auto result = storage::snapshot_json(snapshot);
    result["accepted"] = true;
    result["task_id"] = task_id;
    result["task_name"] = task.source.value("questName", task_id);
    result["request_id"] = request_id;
    return result;
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

Application::J Application::start_workflow(const std::string &flow_id, const J &request) {
    std::shared_ptr<maafw::AdbBackend> backend;
    {
        std::lock_guard lock(mutex_);
        require(operation_.value("state", "idle") != "running", "DEVICE_OPERATION_BUSY");
        backend = backend_;
    }
    require(bool(backend), "DEVICE_NOT_CONNECTED");
    auto document = workflow_store_->read(flow_id);
    require(request.value("revision", std::string{}) ==
                document.at("revision").get<std::string>(),
            "WORKFLOW_REVISION_MISMATCH");
    // 调试图会移除持久化 revision；运行身份必须先冻结原始已保存版本。
    const auto workflow_revision = document.at("revision").get<std::string>();
    if (request.value("mode", "workflow") == "selected_node")
        document = selected_node_document(std::move(document), request.at("node_id"));
    std::set<std::string> task_ids;
    for (const auto &node : document.at("nodes")) {
        const auto &parameters = node.at("parameters");
        if (node.at("type") == "business" && parameters.value("binding", "") == "task_stage")
            task_ids.insert(parameters.at("task_id").get<std::string>());
    }
    require(task_ids.size() <= 1, "AUTHOR_MULTIPLE_TASK_PROFILES_UNSUPPORTED");
    auto values = task_ids.empty() ? profile_store_->load().at("values")
                                   : effective_profile_values(*task_ids.begin());
    auto compiled = games::tasks::compile_author_workflow(
        document, [this, &values](const J &parameters) {
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
        });
    const bool recoverable = !compiled.workflow.checkpoint.empty();
    auto executable = recoverable
        ? games::recovery::with_boot_recovery(compiled.workflow, true)
        : compiled.workflow;
    const auto request_id = request.value("request_id", platform::unique_id());
    const auto destination = paths_.data_root / "published" / request_id;
    auto session = games::tasks::publish_workflow(executable, author_bundle_, *registry_,
                                                   destination, aliases_);
    runtime::RunDefinition definition;
    definition.request_id = request_id;
    definition.initial = std::move(session);
    definition.max_business_units = 1;
    if (recoverable) {
        definition.state_factory = games::wvd_state_binding(values);
        const auto lifecycle = backend->lifecycle_target();
        definition.recover = games::recovery::recovery_binding(lifecycle, values);
        definition.recovery_limit = 3;
        games::recovery::bind_initial_vpn(definition, lifecycle, values);
    }
    definition.policy = {backend->lifecycle_target().device_id, "wvd",
                         "jp.co.drecom.wizardry.daphne", definition.initial.bundle.revision,
                         "900x1600", {900, 1600},
                         {contracts::ActionKind::Click, contracts::ActionKind::ClickKey,
                          contracts::ActionKind::Swipe}, {}, {"wvd"}, 2000ms};
    for (const auto &action : executable.required_actions)
        definition.policy.permissions.insert(action_kind(action));
    {
        std::lock_guard lock(mutex_);
        preview_lease_.reset();
    }
    const auto snapshot = coordinator_->start(std::move(definition), backend);
    {
        std::lock_guard lock(mutex_);
        active_workflow_id_ = flow_id;
        active_workflow_revision_ = workflow_revision;
        active_task_name_ = document.at("flow").at("name").get<std::string>();
        active_started_ = std::chrono::steady_clock::now();
        active_pipeline_to_node_ = compiled.pipeline_to_node;
    }
    auto result = storage::snapshot_json(snapshot);
    result["accepted"] = true;
    result["workflow_id"] = flow_id;
    result["workflow_revision"] = workflow_revision;
    result["request_id"] = request_id;
    return result;
}

Application::J Application::stop_run(std::optional<std::uint64_t> requested_run_id) {
    const auto current = coordinator_->snapshot();
    if (requested_run_id)
        require(current.run_id == *requested_run_id, "RUN_ID_MISMATCH");
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
        auto image = maafw::image_buffer();
        auto bytes = frame.encoded_image;
        require(MaaImageBufferSetEncoded(image.get(), bytes.data(), bytes.size()) &&
                    MaaImageBufferGetRawData(image.get()) && MaaImageBufferChannels(image.get()) == 3,
                "PROBE_IMAGE_DECODE_FAILED");
        const contracts::Size size{static_cast<int>(MaaImageBufferWidth(image.get())),
                                   static_cast<int>(MaaImageBufferHeight(image.get()))};
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
    } else {
        const auto &parameters = request.at("recognition");
        const auto &condition = parameters.contains("condition") ? parameters.at("condition") : parameters;
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
    const auto parsed = maafw::parse_recognition_request(recognition);
    if (std::holds_alternative<maafw::RecognitionRequest::CustomParameters>(parsed.parameters)) {
        maafw::MaaGateway gateway(
            std::move(bundle), nullptr, {}, {},
            registry_->bind_recognitions({games::vision::binding(aliases_)}));
        gateway.initialize();
        return observation_json(gateway.recognize(frame, frame.identity, parsed));
    }
    maafw::OfflineRecognizer recognizer(std::move(bundle));
    return observation_json(recognizer.evaluate(frame, frame.identity, parsed));
}

api::DynamicReply Application::handle(const api::Request &request) {
    try {
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
            return json_reply(stop_run(), api::http::status::accepted);
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

void Application::stop() {
    if (stopping_.exchange(true))
        return;
    if (coordinator_)
        coordinator_->request_stop();
    if (device_worker_.joinable())
        device_worker_.join();
    if (coordinator_)
        while (!coordinator_->wait_for(1s))
            std::this_thread::sleep_for(50ms);
    std::shared_ptr<maafw::AdbBackend> backend;
    {
        std::lock_guard lock(mutex_);
        backend = std::move(backend_);
        preview_lease_.reset();
    }
    if (backend)
        backend->disconnect();
}
} // namespace wvd::app
