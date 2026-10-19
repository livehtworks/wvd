#include "recorded_input_cases.hpp"
#include "games/wvd/tasks/native_program.hpp"
#include "games/wvd/tasks/public_flow_library.hpp"
#include "games/wvd/tasks/public_step_scope.hpp"
#include "games/wvd/tasks/locale_assets.hpp"
#include "games/wvd/tasks/bounty_cycle.hpp"
#include "games/wvd/tasks/dungeon_route.hpp"
#include "games/wvd/navigation/dungeon_entry.hpp"
#include "games/wvd/navigation/auto_route.hpp"
#include "games/wvd/recovery/party_death.hpp"
#include "games/wvd/recovery/boot.hpp"
#include "games/wvd/supply/dungeon_recover.hpp"
#include "games/wvd/combat/turn.hpp"
#include "games/wvd/combat/auto_combat.hpp"
#include "games/wvd/state.hpp"
#include "games/wvd/native_operations.hpp"
#include "games/wvd/vision/native_recognizers.hpp"
#include "games/wvd/vision/combat_phase_probes.hpp"
#include "storage/legacy_import.hpp"
#include "platform/windows/file_digest.hpp"
#include "runtime/native_flow_ports.hpp"
#include <opencv2/imgcodecs.hpp>
#include <fstream>
#include <iostream>
#include <thread>

namespace recorded_input_contract {
using namespace wvd;
using J = nlohmann::json;
using namespace std::chrono_literals;
namespace {
J read(const std::filesystem::path &path) { std::ifstream in(path); return J::parse(in); }
void require(bool value, const std::string &code) { if (!value) throw std::runtime_error(code); }
J observation(const contracts::Observation &value) {
    const auto &id = value.basis;
    return {{"outcome",static_cast<int>(value.outcome)},{"action_eligible",value.action_eligible},
        {"error",value.error_code},{"evidence",value.evidence},{"timing_ms",value.timing_ms},
        {"box",value.box ? J{value.box->x,value.box->y,value.box->width,value.box->height} : J(nullptr)},
        {"basis",{{"device",id.device_id},{"game",id.game_id},{"pack",id.pack_revision},
            {"viewport",id.viewport_id},{"generation",id.generation},{"frame",id.frame_id},{"epoch",id.action_epoch},
            {"connection_generation",id.connection_generation},{"instance",id.instance_id},
            {"creation",id.instance_created_identity},{"foreground",id.foreground_application},
            {"captured_ns",std::chrono::duration_cast<std::chrono::nanoseconds>(id.captured_at.time_since_epoch()).count()}}}};
}

// Only device transport is isolated. Service, gate, executor and business
// operations below are the production implementations, with unchanged pixels.
class RecordedBackend final : public devices::DeviceBackend {
  public:
    explicit RecordedBackend(const cv::Mat &image) : pixels_(std::make_shared<const std::vector<std::uint8_t>>(
        image.data, image.data + image.total() * image.elemSize())) {}
    void after_input(const cv::Mat &image) {
        after_input_ = std::make_shared<const std::vector<std::uint8_t>>(
            image.data, image.data + image.total() * image.elemSize());
    }
    void after_inputs(const std::vector<cv::Mat> &images) {
        for (const auto &image : images)
            after_inputs_.push_back(std::make_shared<const std::vector<std::uint8_t>>(
                image.data, image.data + image.total() * image.elemSize()));
    }
    std::vector<contracts::Command> commands;
    bool offline() const override { return true; }
    bool verified_access() const override { return true; }
    bool connect() override { return true; }
    devices::RawFrame capture() override {
        devices::RawFrame frame;
        frame.raw_bgr = after_input_ && !commands.empty() ? after_input_ : pixels_; frame.size = {900,1600};
        if (!after_inputs_.empty() && !commands.empty())
            frame.raw_bgr = after_inputs_.at(std::min(commands.size() - 1, after_inputs_.size() - 1));
        frame.device_id = "recorded-input"; frame.viewport_id = "900x1600";
        frame.foreground_application = "jp.co.drecom.wizardry.daphne";
        frame.connection_generation = 1; frame.display_rotation = 0;
        frame.instance_id = "isolated-recorded-instance"; frame.instance_created_identity = "isolated-creation";
        frame.captured_at = frame.capture_finished_at = std::chrono::steady_clock::now();
        return frame;
    }
    bool execute(const contracts::Command &command) override { commands.push_back(command); return true; }
  private:
    std::shared_ptr<const std::vector<std::uint8_t>> pixels_;
    std::shared_ptr<const std::vector<std::uint8_t>> after_input_;
    std::vector<std::shared_ptr<const std::vector<std::uint8_t>>> after_inputs_;
};
}

int run(const std::filesystem::path &pack, const std::filesystem::path &cases,
        const std::filesystem::path &output) {
    require(!std::filesystem::exists(output), "RECORDED_INPUT_NEW_ROOT_REQUIRED");
    std::filesystem::create_directories(output);
    const auto manifest = read(pack / "manifest.json");
    recognition::Bundle bundle{pack, manifest.at("revision"), {}};
    bundle.files.push_back({"manifest.json",platform::file_sha256(pack / "manifest.json")});
    for (const auto *name : {"parameters/legacy-config-fields.json","parameters/legacy-quests.json"})
        bundle.files.push_back({name,platform::file_sha256(pack / name)});
    for (const auto &row : manifest.at("files")) bundle.files.push_back({row.at("path"),row.at("sha256")});
    // The caller materializes locked model bytes beside the isolated manifest.
    // Declare those real dependencies too; Service must never see fake models.
    const auto models = read("resources/recognition/ocr-models.json").at("models");
    for (const auto &[locale, model] : models.items())
        for (const auto &[name, member] : model.at("files").items()) {
            const auto path = model.at("bundle_directory").get<std::string>() + "/" + name;
            bundle.files.push_back({path,member.at("sha256")});
        }
    const auto catalogue = read("resources/authoring/semantic-assets.json");
    J documents = J::object();
    for (const auto &doc : read("resources/authoring/public-flows.json")) documents[doc.at("flow").at("id").get<std::string>()] = doc;
    const games::tasks::PublicFlowLibrary library(documents, catalogue);
    J profile = storage::LegacyConfigImporter(read(pack / "parameters/legacy-config-fields.json"))
        .parse({{"GENERAL",J::object()}}).values;
    profile["STRATEGY"] = J::array({{{"group_name","manual"}, {"skill_settings",J::array({
        {{"role_var",""},{"skill_var","左上技能"},{"skill_lvl",1},{"target_var","左上角色"}}})}}});
    profile["DEFAULT_OVERALL_STRATEGY"] = "manual"; profile["TASK_SPECIFIC_CONFIG"] = false;
    J results = J::array();
    for (const auto &sample : read(cases)) {
        const auto image_path = std::filesystem::path(sample.at("frame").get<std::string>());
        require(platform::file_sha256(image_path) == sample.at("sha256").get<std::string>(), "RECORDED_INPUT_HASH_CHANGED");
        const auto image = cv::imread(image_path.string(), cv::IMREAD_COLOR);
        require(image.cols == 900 && image.rows == 1600 && image.type() == CV_8UC3, "RECORDED_INPUT_VIEWPORT_INVALID");
        const std::string locale = sample.at("locale");
        const bool rescue = sample.value("rescue", false);
        const bool attack_target = sample.value("kind", "") == "attack-target";
        auto case_profile = profile;
        if (sample.value("kind", "") == "healing-close") case_profile["RECOVER_WHEN_BEGINNING"] = true;
        if (sample.value("kind", "") == "healing-harken") case_profile["RECOVER_WHEN_BEGINNING"] = true;
        if (sample.value("kind", "") == "navigation-panel") case_profile["RECOVER_WHEN_BEGINNING"] = false;
        if (attack_target) case_profile["STRATEGY"][0]["skill_settings"][0]["role_var"] = sample.at("role");
        const games::tasks::PublicStepScope public_steps([&](const std::string &id, const J &arguments) {
            return library.compile_step(id,arguments,locale);
        });
        RecordedBackend backend(image);
        if (sample.contains("after_input_frame")) {
            const auto path = std::filesystem::path(sample.at("after_input_frame").get<std::string>());
            require(platform::file_sha256(path) == sample.at("after_input_sha256").get<std::string>(),
                "RECORDED_AFTER_INPUT_HASH_CHANGED");
            const auto after = cv::imread(path.string(), cv::IMREAD_COLOR);
            require(after.cols == 900 && after.rows == 1600 && after.type() == CV_8UC3,
                "RECORDED_AFTER_INPUT_VIEWPORT_INVALID");
            backend.after_input(after);
        }
        if (sample.contains("after_inputs")) {
            std::vector<cv::Mat> images;
            for (const auto &next : sample.at("after_inputs")) {
                const auto path = std::filesystem::path(next.at("frame").get<std::string>());
                require(platform::file_sha256(path) == next.at("sha256").get<std::string>(),
                    "RECORDED_SEQUENCE_HASH_CHANGED");
                auto pixels = cv::imread(path.string(), cv::IMREAD_COLOR);
                require(pixels.cols == 900 && pixels.rows == 1600 && pixels.type() == CV_8UC3,
                    "RECORDED_SEQUENCE_VIEWPORT_INVALID");
                images.push_back(std::move(pixels));
            }
            backend.after_inputs(images);
        }
        contracts::InputPolicy policy;
        policy.device_id = "recorded-input"; policy.game_id = "wvd";
        policy.application_id = "jp.co.drecom.wizardry.daphne"; policy.viewport_id = "900x1600";
        policy.pack_revision = bundle.revision; policy.allowed_scenes = {"wvd"};
        policy.permissions = policy.capabilities = {contracts::ActionKind::Click,contracts::ActionKind::ClickKey,contracts::ActionKind::Swipe};
        games::WvdRunState business(case_profile,{"recorded-input",1,std::make_shared<contracts::SteadyClock>()});
        business.enter_segment(contracts::SegmentBoundary::Initial,1,0);
        const bool boot_recovery = sample.value("kind", "") == "boot-guild" && sample.value("phase", "") == "recovery";
        if (boot_recovery) business.enter_segment(contracts::SegmentBoundary::LifecycleRecovery,2,0);
        recognition::Service service(bundle,games::vision::native_handlers(manifest.value("aliases",J::object()),locale));
        runtime::NativeFlowPorts ports(backend,service,business,policy,boot_recovery ? 2 : 1,{});
        ports.set_operation_handler(games::wvd_operation_factory(business,[](const auto &,const auto &){},[](const auto &){})(ports));
        const auto frame = ports.capture();
        if (sample.value("kind", "") == "boot-guild") {
            const auto phase = sample.at("phase").get<std::string>();
            const bool task = phase == "task" || phase == "recovery";
            const bool expected_ready = sample.value("ready", true);
            const auto ready = ports.recognize(frame, {"guild-boot-ready", "1", {0,0,900,1600},
                recognition::CustomParameters{"WvdVision", {{"mode", "boot_ready"}}}});
            require(ready.outcome == (expected_ready ? contracts::RecognitionOutcome::Hit : contracts::RecognitionOutcome::NoHit),
                "GUILD_BOOT_READY_MISMATCH:" + ready.error_code);
            auto graph = phase == "common" ? games::recovery::clear_common_screens(true) :
                games::recovery::wait_boot_ready(true);
            if (task) {
                const games::WvdQuestCatalog quests(read(pack / "parameters/legacy-quests.json"));
                graph = games::recovery::with_boot_recovery(games::tasks::bounty_cycle(
                    quests.at("GiantBounty"), case_profile, {}, library, documents.at("guild-open-bounty-page"), locale), true);
            }
            games::tasks::localize_task_assets(graph, catalogue, locale);
            const auto program = games::tasks::compile_native_program(graph, J::object(), "recorded-guild-boot");
            runtime::FlowExecutor executor(program, ports, 30s);
            runtime::TickResult result{runtime::TickState::Progress};
            const auto started = std::chrono::steady_clock::now();
            while (std::chrono::steady_clock::now() - started < (expected_ready ? 15s : 3s)) {
                result = executor.tick();
                if (!backend.commands.empty() || (result.state != runtime::TickState::Progress &&
                    result.state != runtime::TickState::Waiting)) break;
                std::this_thread::sleep_for(2ms);
            }
            const auto summary = business.summary();
            results.push_back({{"id",sample.at("id")},{"ready",observation(ready)},
                {"flow_code",result.code},{"state",int(result.state)},{"inputs",backend.commands.size()},
                {"business",summary},{"progress",executor.progress_snapshot()},
                {"elapsed_ms",std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now()-started).count()}});
            std::ofstream(output / "results.json") << results.dump(2);
            if (task) {
                require(backend.commands.size() == 1 && backend.commands.front().kind == contracts::ActionKind::Click &&
                    backend.commands.front().x >= 690 && backend.commands.front().x <= 802 &&
                    backend.commands.front().y >= 236 && backend.commands.front().y <= 294 &&
                    summary.at("task_step") == 0 && summary.at("revivals") == 0 &&
                    !summary.at("lifecycle_recovery_active").get<bool>(),
                    "GUILD_BOOT_DID_NOT_REACH_EXISTING_BOUNTY_ENTRY:" + result.code);
            } else require(backend.commands.empty() &&
                ((result.state == runtime::TickState::Completed) == expected_ready),
                "GUILD_BOOT_FALSE_INPUT_OR_HANDOFF:" + result.code);
            ports.stop(); require(ports.cleanup(), "RECORDED_INPUT_CLEANUP_FAILED");
            continue;
        }
        if (sample.value("kind", "") == "healing-harken") {
            business.enter_dungeon();
            const auto initial_step = sample.at("initial_step").get<unsigned>();
            for (unsigned i = 0; i < initial_step; ++i) business.target_point_completed();
            const games::WvdQuestCatalog quests(read(pack / "parameters/legacy-quests.json"));
            auto graph = games::tasks::traverse_dungeon(
                games::tasks::giant_bounty_plan(quests.at("GiantBounty")), case_profile, {});
            graph.entry = "Heal"; // Resume the exact production caller suspended in the recorded fault.
            games::tasks::localize_task_assets(graph, catalogue, locale);
            const auto program = games::tasks::compile_native_program(graph, J::object(), "recorded-healing-harken");
            runtime::FlowExecutor executor(program, ports, 20s);
            runtime::TickResult result{runtime::TickState::Progress};
            const auto started = std::chrono::steady_clock::now();
            const bool completes = sample.at("completes");
            const auto budget = completes ? 10s : 3s;
            while (std::chrono::steady_clock::now() - started < budget) {
                result = executor.tick();
                if (result.state != runtime::TickState::Progress && result.state != runtime::TickState::Waiting) break;
                std::this_thread::sleep_for(2ms);
            }
            const auto summary = business.summary();
            const auto progress = executor.progress_snapshot();
            results.push_back({{"id", sample.at("id")}, {"backend_calls", backend.commands.size()},
                {"flow_code", result.code}, {"state", int(result.state)}, {"progress", progress},
                {"business", summary}, {"elapsed_ms", std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started).count()}});
            std::ofstream(output / "results.json") << results.dump(2);
            require((result.state == runtime::TickState::Completed) == completes &&
                backend.commands.size() == sample.at("inputs").get<unsigned>() &&
                summary.at("task_step") == sample.at("final_step") &&
                summary.at("healing_required").get<bool>() &&
                summary.at("healing_submissions") == 0 && progress.at("pending_inputs").empty(),
                "HEAL_HARKEN_NOT_HANDED_TO_ORIGINAL_ROUTE:" + result.code);
            if (!backend.commands.empty()) require(backend.commands.front().kind == contracts::ActionKind::Click &&
                backend.commands.front().x == 36 && backend.commands.front().y == 1425,
                "HEAL_HARKEN_EXTRA_OR_WRONG_INPUT");
            ports.stop(); require(ports.cleanup(), "RECORDED_INPUT_CLEANUP_FAILED");
            continue;
        }
        if (sample.value("kind", "") == "navigation-panel") {
            business.enter_dungeon();
            require(!business.summary().at("healing_required").get<bool>(), "RECORDED_NAVIGATION_HAS_HEAL_DEMAND");
            using C = games::tasks::PipelineCompiler;
            C root("recorded.navigation-panel");
            const auto child = root.define_child("Navigation", games::navigation::auto_route("dungFlag"));
            root.route("Entry", {"Route"});
            root.call_child("Route", child, {"Terminal"},
                {{"blocked", {"Terminal"}}, {"encounter", {"Terminal"}}, {"stopped", {"Terminal"}}});
            auto graph = root.finish();
            games::tasks::localize_task_assets(graph, catalogue, locale);
            const auto program = games::tasks::compile_native_program(graph, J::object(), "recorded-navigation-panel");
            runtime::FlowExecutor executor(program, ports, 20s);
            runtime::TickResult result{runtime::TickState::Progress};
            const auto started = std::chrono::steady_clock::now();
            while (std::chrono::steady_clock::now() - started < 15s) {
                result = executor.tick();
                if (result.state != runtime::TickState::Progress && result.state != runtime::TickState::Waiting) break;
                std::this_thread::sleep_for(2ms);
            }
            require(result.state == runtime::TickState::Completed && backend.commands.size() == 2 &&
                backend.commands[1].x == 66 && backend.commands[1].y == 1500 &&
                executor.progress_snapshot().at("pending_inputs").empty() &&
                !business.summary().at("healing_required").get<bool>() &&
                business.summary().at("task_step") == 0,
                "NAVIGATION_PANEL_DID_NOT_RETURN_TO_SHARED_OWNER:" + result.code);
            results.push_back({{"id", sample.at("id")}, {"backend_calls", backend.commands.size()},
                {"flow_code", result.code}, {"progress", executor.progress_snapshot()},
                {"business", business.summary()}});
            ports.stop(); require(ports.cleanup(), "RECORDED_INPUT_CLEANUP_FAILED");
            std::ofstream saved(output / "results.json"); saved << results.dump(2);
            require(bool(saved), "RECORDED_INPUT_EVIDENCE_WRITE_FAILED");
            continue;
        }
        if (sample.value("kind", "") == "healing-close") {
            business.enter_dungeon();
            business.confirm_event("recorded.heal.request", "healing_requested", 1, 1);
            business.healing_input_submitted(false);
            auto graph = games::supply::recover_in_dungeon();
            games::tasks::localize_task_assets(graph, catalogue, locale);
            const auto program = games::tasks::compile_native_program(graph, J::object(), "recorded-healing-close");
            runtime::FlowExecutor executor(program, ports, 10s);
            runtime::TickResult result{runtime::TickState::Progress};
            const auto started = std::chrono::steady_clock::now();
            while (std::chrono::steady_clock::now() - started < 8s && backend.commands.size() < 3) {
                result = executor.tick();
                if (result.state != runtime::TickState::Progress && result.state != runtime::TickState::Waiting) break;
                std::this_thread::sleep_for(2ms);
            }
            const bool expected = sample.at("close_allowed");
            require(backend.commands.size() == (expected ? 3U : 0U), "HEAL_RECORDED_CLOSE_ADMISSION:" + result.code);
            if (expected) require(backend.commands[0].key == 4 && backend.commands[1].key == 4 &&
                backend.commands.back().kind == contracts::ActionKind::Click &&
                backend.commands.back().x == 66 && backend.commands.back().y == 1500,
                "HEAL_RECORDED_CLOSE_COORDINATES");
            results.push_back({{"id",sample.at("id")},{"backend_calls",backend.commands.size()},
                {"flow_code",result.code},{"progress",executor.progress_snapshot()}});
            ports.stop(); require(ports.cleanup(), "RECORDED_INPUT_CLEANUP_FAILED");
            std::ofstream saved(output / "results.json"); saved << results.dump(2);
            require(bool(saved), "RECORDED_INPUT_EVIDENCE_WRITE_FAILED");
            continue;
        }
        if (sample.value("kind", "") == "low-willpower") {
            const auto detection = ports.recognize(frame, {"recorded.low-willpower", "1", {0,0,900,1600},
                recognition::CustomParameters{"WvdVision", games::vision::low_willpower_warning()}});
            const bool expected = sample.at("warning");
            require(detection.outcome == (expected ? contracts::RecognitionOutcome::Hit :
                contracts::RecognitionOutcome::NoHit), "LOW_WILLPOWER_RECORDED_DETECTION:" + detection.error_code +
                ":" + detection.evidence.dump());
            J row{{"id", sample.at("id")}, {"detection", observation(detection)}};
            if (expected || sample.contains("after_input_frame")) {
                games::WvdQuestCatalog quests(read(pack / "parameters/legacy-quests.json"));
                auto graph = sample.value("phase", "entry") == "boot" ? games::recovery::wait_boot_ready(true) :
                    games::navigation::enter_dungeon(games::tasks::giant_bounty_plan(quests.at("GiantBounty")), true);
                games::tasks::localize_task_assets(graph, catalogue, locale);
                const auto program = games::tasks::compile_native_program(graph, J::object(), "recorded-input");
                runtime::FlowExecutor executor(program, ports, 10s);
                const auto started = std::chrono::steady_clock::now();
                runtime::TickResult result{runtime::TickState::Progress};
                while (std::chrono::steady_clock::now() - started < 8s) {
                    result = executor.tick();
                    if (result.state != runtime::TickState::Progress && result.state != runtime::TickState::Waiting) break;
                    std::this_thread::sleep_for(2ms);
                }
                const auto expected_inputs = sample.contains("after_input_frame") ? 1U : 0U;
                require(result.state == runtime::TickState::ExternalBlocked &&
                    result.code == "party.low_willpower_manual_required" && backend.commands.size() == expected_inputs,
                    "LOW_WILLPOWER_ENTRY_DID_NOT_STOP_WITHOUT_EXTRA_INPUT:" + result.code);
                const auto progress = executor.progress_snapshot();
                if (expected_inputs) require(!progress.at("pending_inputs").empty(), "LOW_WILLPOWER_LOST_PENDING");
                row.update({{"flow_code", result.code}, {"backend_calls", backend.commands.size()},
                    {"progress", progress}, {"elapsed_ms", std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - started).count()}});
            }
            ports.stop(); require(ports.cleanup(), "RECORDED_INPUT_CLEANUP_FAILED");
            results.push_back(std::move(row));
            std::ofstream saved(output / "results.json"); saved << results.dump(2);
            require(bool(saved), "RECORDED_INPUT_EVIDENCE_WRITE_FAILED");
            continue;
        }
        for (const auto &condition : std::vector<J>{J{{"mode", "auto_route_moving"}},
                J{{"mode", "supply_context"}, {"phase", "dungeon"}}}) {
            const auto result = ports.recognize(frame, {"recorded.not-dungeon", "1", {0,0,900,1600},
                recognition::CustomParameters{"WvdVision", condition}});
            require(result.outcome == contracts::RecognitionOutcome::NoHit,
                "RESCUE_OR_COMBAT_MUST_NOT_AUTHORIZE_DUNGEON_INPUT:" + result.error_code + ":" + result.evidence.dump());
        }
        const auto make = [&](games::tasks::CompiledWorkflow graph) {
            games::tasks::localize_task_assets(graph,catalogue,locale);
            return games::tasks::compile_native_program(graph,J::object(),"recorded-input");
        };
        if (attack_target) {
            const auto role = sample.at("role").get<std::string>();
            const auto graph = games::combat::take_turn(case_profile,{"spellskill/char/" + role + ".png"});
            const auto prepared = ports.operate("WvdCombat",graph.nodes.at("Prepare").at("operation_args"),
                frame,std::nullopt,"recorded.attack.prepare");
            require(prepared.state == runtime::OperationState::Done && business.summary().at("has_prepared_skill") == true,
                "RECORDED_ATTACK_ACTOR_NOT_PREPARED");
            using C = games::tasks::PipelineCompiler;
            const auto menu = games::vision::combat_phase("menu");
            const J actor{{"mode","prepared_actor"}};
            const auto original_scene = C::all({menu,actor,J{{"mode","support_selection"},{"expect","absent"}},
                C::absent(C::image("combat_skill_detail"))});
            C original("recorded.old_attack_point");
            original.route("Entry",{"Point"});
            original.fixed_click("Point",original_scene,menu,{450,300},{"Terminal"});
            original.allowed_area("Point",{220,180,620,720});
            const auto old = make(original.finish());
            const auto &old_input = std::get<workflow::Input>(old.definitions.at(old.root_definition).steps.at("Point").data);
            const auto old_scene = ports.recognize(frame,old_input.scene), old_target = ports.recognize(frame,old_input.target);
            const auto friendly_absent = ports.recognize(frame,{"recorded.friendly_absent","1",{0,0,900,1600},
                recognition::CustomParameters{"WvdVision",J{{"mode","support_selection"},{"expect","absent"}}}});
            require(old_scene.outcome == contracts::RecognitionOutcome::NoHit &&
                old_target.outcome == contracts::RecognitionOutcome::NoHit &&
                friendly_absent.outcome == contracts::RecognitionOutcome::NoHit,
                "ORIGINAL_ATTACK_DETAIL_SCOPE_FAILURE_NOT_REPRODUCED");
            const auto shared = make(library.compile_step("combat-select-target",
                {{"attack_mode",true},{"candidate_index",0},{"delay_ms",100}},locale));
            const auto &input = std::get<workflow::Input>(shared.definitions.at(shared.root_definition).steps.at("Author_target").data);
            const auto scene = ports.recognize(frame,input.scene), target = ports.recognize(frame,input.target);
            require(scene.outcome == contracts::RecognitionOutcome::Hit &&
                target.outcome == contracts::RecognitionOutcome::Hit && target.action_eligible && target.center,
                "RECORDED_SHARED_ATTACK_TARGET_MISSING:" + scene.error_code + ":" + target.error_code + ":" + target.evidence.dump());
            contracts::Command command; command.kind = contracts::ActionKind::Click;
            command.x = target.center->x; command.y = target.center->y;
            const auto submitted = ports.submit(command,scene,target,input.allowed_area,"recorded.attack.target");
            require(submitted.state == runtime::SubmissionState::Accepted && backend.commands.size() == 1 &&
                command.x >= 220 && command.x < 840 && command.y >= 180 && command.y < 900,
                "RECORDED_ATTACK_TARGET_NOT_DELIVERED_TO_ISOLATED_BACKEND");
            results.push_back({{"id",sample.at("id")},{"frame",image_path.string()},
                {"sha256",sample.at("sha256")},{"old_scene",observation(old_scene)},{"old_target",observation(old_target)},
                {"friendly_absent",observation(friendly_absent)},
                {"scene",observation(scene)},{"target",observation(target)},{"point",{command.x,command.y}},
                {"backend_calls",backend.commands.size()},{"proof","target eligibility/delivery only; not an actual game attack or cast"}});
            ports.stop(); require(ports.cleanup(),"RECORDED_ATTACK_CLEANUP_FAILED");
            std::ofstream saved(output/"results.json"); saved << results.dump(2);
            require(bool(saved),"RECORDED_INPUT_EVIDENCE_WRITE_FAILED");
            continue;
        }
        const auto death = make(games::recovery::dismiss_party_death());
        const auto turn = make(games::combat::take_turn(profile,{}));
        const auto boot = make(games::recovery::wait_boot_ready(true));
        J rows = J::array();
        const auto probe_input = [&](const workflow::FlowProgram &program, const std::string &id,
                                     bool expected) {
            const auto &input = std::get<workflow::Input>(program.definitions.at(program.root_definition).steps.at(id).data);
            const auto scene = ports.recognize(frame,input.scene), target = ports.recognize(frame,input.target);
            require(scene.outcome != contracts::RecognitionOutcome::Error && target.outcome != contracts::RecognitionOutcome::Error,
                    "RECORDED_INPUT_RECOGNITION_ERROR:" + id + ":" + scene.error_code + ":" + target.error_code);
            require((scene.outcome == contracts::RecognitionOutcome::Hit) == expected, "RECORDED_INPUT_SCENE_MISMATCH:" + id);
            rows.push_back({{"node",id},{"scene",observation(scene)},
                            {"target",observation(target)}});
            if (!expected) {
                contracts::Command command;
                command.kind = contracts::ActionKind::Click;
                command.x = input.command.value("x",450); command.y = input.command.value("y",800);
                if (input.use_target_center && target.center) {
                    command.x = target.center->x; command.y = target.center->y;
                }
                const auto submitted = ports.submit(command,scene,target,input.allowed_area,id);
                require(submitted.state == runtime::SubmissionState::Rejected && backend.commands.empty(), "RECORDED_INPUT_NEGATIVE_REACHED_BACKEND:" + id);
            }
        };
        probe_input(death,"Dismiss",rescue);
        probe_input(turn,"AutoOff",false); // Original specimens have white Auto or a blocking rescue/detail page.
        probe_input(boot,"CloseCharacter",false);
        if (rescue) {
            runtime::FlowExecutor executor(death,ports,10s);
            auto result = runtime::TickResult{runtime::TickState::Progress};
            const auto deadline = std::chrono::steady_clock::now()+5s;
            while (backend.commands.empty() && std::chrono::steady_clock::now()<deadline) {
                result = executor.tick();
                require(result.state == runtime::TickState::Progress || result.state == runtime::TickState::Waiting,
                        "RECORDED_RESCUE_STOPPED:" + result.code);
                std::this_thread::sleep_for(2ms);
            }
            require(backend.commands.size()==1 && backend.commands.front().kind==contracts::ActionKind::Click &&
                    backend.commands.front().x==450 && backend.commands.front().y==800,
                    "RECORDED_RESCUE_NOT_ORIGINAL_CENTER");
            rows.push_back({{"first_input",{450,800}},{"pending",executor.progress_snapshot()}});
        }
        ports.stop(); require(ports.cleanup(),"RECORDED_INPUT_CLEANUP_FAILED");
        results.push_back({{"id",sample.at("id")},{"frame",image_path.string()},{"sha256",sample.at("sha256")},
            {"locale",locale},{"rescue",rescue},{"backend_calls",backend.commands.size()},{"requests",rows}});
        std::ofstream saved(output / "results.json"); saved << results.dump(2);
        require(bool(saved),"RECORDED_INPUT_EVIDENCE_WRITE_FAILED");
    }
    std::cout << "PASS recorded Service/gate/executor composition; rescue first center only, no game input. Cases=" << results.size() << '\n';
    return 0;
}
}
