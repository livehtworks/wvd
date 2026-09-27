#include "games/wvd/tasks/author_workflow.hpp"
#include "games/wvd/tasks/native_program.hpp"
#include "games/wvd/tasks/public_flow_library.hpp"
#include "games/wvd/tasks/public_step_scope.hpp"
#include "games/wvd/tasks/bounty_visit.hpp"
#include "games/wvd/tasks/bounty_cycle.hpp"
#include "games/wvd/tasks/locale_assets.hpp"
#include "games/wvd/tasks/native_publisher.hpp"
#include "games/wvd/navigation/time_leap.hpp"
#include "games/wvd/navigation/map_route.hpp"
#include "games/wvd/navigation/harken_exit.hpp"
#include "games/wvd/navigation/world_travel.hpp"
#include "games/wvd/navigation/auto_route.hpp"
#include "games/wvd/combat/auto_combat.hpp"
#include "games/wvd/combat/encounter.hpp"
#include "games/wvd/combat/strategy.hpp"
#include "games/wvd/supply/inn.hpp"
#include "games/wvd/recovery/boot.hpp"
#include "games/wvd/vision/boot_probes.hpp"
#include "games/wvd/vision/native_recognizers.hpp"
#include "games/wvd/vision/native_asset_resolver.hpp"
#include "recognition/service.hpp"
#include <opencv2/imgcodecs.hpp>
#include "platform/windows/file_digest.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include "runtime/flow_executor.hpp"
#include "games/wvd/state.hpp"
#include "games/wvd/business_condition.hpp"
#include "games/wvd/native_operations.hpp"
#include "games/wvd/vision/dialogue_probes.hpp"
#include "games/wvd/vision/network_probes.hpp"
#include "games/wvd/vision/inn_leave_probes.hpp"
#include "storage/legacy_import.hpp"

namespace closure {
using namespace wvd;
using namespace std::chrono_literals;
using J = nlohmann::json;
using C = games::tasks::PipelineCompiler;
void check(bool value, const std::string &code) { if (!value) throw std::runtime_error(code); }
J read(const char *path) { std::ifstream in(path); return J::parse(in); }

// 只替换帧与叶子识别结果。业务图、执行器、业务条件和住宿账目使用生产实现。
struct Ports final : runtime::FlowPorts {
    games::WvdRunState business;
    games::NativeOperations operations;
    contracts::FrameIdentity current;
    std::set<std::string> images;
    std::map<std::string, bool> conditions;
    std::function<void(const std::string &)> after_input;
    std::vector<std::string> inputs;
    std::uint64_t epoch{}, captures{}, recognitions{};
    bool stop{}, combat{}, blocker{}, ready{}, network{};
    Ports() : business(storage::LegacyConfigImporter(read("packs/wvd/parameters/legacy-config-fields.json"))
            .parse({{"GENERAL", J::object()}}).values,
            {"closure-transitions", 1, std::make_shared<contracts::SteadyClock>()}),
        operations(business, {[this] { return capture(); },
            [this](const auto &frame, const auto &request) { return recognize(frame, request); },
            [this] { return current; }, [this] { return stop; }, [](auto &, auto &) {}, [](auto &) {}}) {
        business.enter_segment(contracts::SegmentBoundary::Initial, 1, 0);
    }
    contracts::FrameEnvelope capture() override {
        contracts::FrameEnvelope frame;
        frame.identity.device_id = "closure"; frame.identity.game_id = "wvd";
        frame.identity.pack_revision = "closure"; frame.identity.viewport_id = "900x1600";
        frame.identity.generation = 1; frame.identity.connection_generation = 1;
        frame.identity.action_epoch = epoch; frame.identity.frame_id = ++captures;
        frame.identity.raw_size = frame.identity.recognition_size = {900, 1600};
        frame.identity.captured_at = std::chrono::steady_clock::now(); current = frame.identity;
        return frame;
    }
    bool evaluate(const J &p) {
        if (const auto it = conditions.find(p.dump()); it != conditions.end()) return it->second;
        const auto mode = p.value("mode", "");
        if (mode == "any" || mode == "all" || mode == "not") {
            bool all = true, any = false;
            for (const auto &child : p.at("conditions")) { const bool hit = evaluate(child); all &= hit; any |= hit; }
            return mode == "any" ? any : mode == "all" ? all : !any;
        }
        if (mode == "business") return games::business_condition(business.summary(), p);
        if (mode == "template") return images.contains(p.at("image").get<std::string>());
        if (mode == "combat_active") return combat;
        if (mode == "blocking_screen") return blocker || network;
        if (mode == "boot_ready" || mode == "boot_post") return ready;
        if (mode == "input_clear") return !blocker && !network;
        if (mode == "region_quiet") return true;
        return false;
    }
    contracts::Observation recognize(const contracts::FrameEnvelope &frame, const recognition::Request &request) override {
        ++recognitions;
        contracts::Observation result; result.basis = frame.identity;
        const auto &parameters = std::get<recognition::CustomParameters>(request.parameters).parameters;
        result.outcome = evaluate(parameters) ? contracts::RecognitionOutcome::Hit : contracts::RecognitionOutcome::NoHit;
        result.box = contracts::Box{400, 800, 50, 50}; result.center = contracts::Point{425, 825};
        result.action_eligible = parameters.value("mode", "") != "business";
        return result;
    }
    runtime::Submission submit(const contracts::Command &, const contracts::Observation &, const contracts::Observation &,
                               contracts::Box, const std::string &path) override {
        inputs.push_back(path); ++epoch;
        if (after_input) after_input(path);
        return {runtime::SubmissionState::Accepted, epoch, std::chrono::steady_clock::now(), {}};
    }
    runtime::OperationResult operate(const std::string &binding, const J &p,
        const std::optional<contracts::FrameEnvelope> &frame, const std::optional<contracts::Observation> &observation,
        const std::string &path) override {
        if (binding == "BeginObservationPhase" || binding == "EndObservationPhase") return {runtime::OperationState::Done};
        return operations.execute(binding, p, frame, observation, path);
    }
    bool cancelled() const override { return stop; }
    void scene(const std::string &name) {
        images.clear(); conditions.clear(); combat = blocker = ready = network = false;
        const bool city = name == "city";
        conditions[games::vision::royal_city().dump()] = city;
        conditions[games::vision::city_screen().dump()] = city;
        conditions[games::vision::inn_button().dump()] = city;
        conditions[games::vision::edge_of_town_button().dump()] = city;
        conditions[games::vision::ordinary_story_page().dump()] = name == "story";
        conditions[games::vision::story_advance_arrow().dump()] = name == "story";
        conditions[games::vision::character_page().dump()] = false;
        conditions[games::vision::network_retry_prompt().dump()] = name == "network";
        conditions[games::vision::network_prompt_zh_hant().dump()] = name == "network";
        conditions[games::vision::network_retry_button_zh_hant().dump()] = name == "network";
        ready = city || name == "outskirts" || name == "harken" || name == "select";
        network = name == "network";
        if (name == "select") images = {"cursedWheelTitle", "BeautifulOre"};
        if (name == "leap" || name == "remnant") images = {"cursedWheelTitle", "leap"};
        if (name == "map" || name == "stale-map") images = {"mapFlag"};
        if (name == "map") images.insert("AutoMove");
        if (name == "moving" || name == "ended") images = {"dungFlag"};
        if (name == "popup") { combat = true; images = {"combat_skill_detail"}; }
        if (name == "harken") images = {"harken_floor_move_zh_hant", "harken_floor_return_zh_hant"};
        if (name == "outskirts") images = {"outskirts_return_to_town_zh_hant"};
        if (name == "confirmation") images = {"inn_confirm_zh_hant"};
        if (name == "stayed") {
            images = {"Stay"};
            conditions[games::vision::inn_leave_zh().dump()] = true;
            conditions[games::vision::inn_leave_settled().dump()] = true;
        }
    }
};
struct Driver {
    workflow::FlowProgram program;
    Ports ports;
    runtime::FlowExecutor executor;
    runtime::TickResult last{runtime::TickState::Progress};
    std::vector<std::string> trace;
    explicit Driver(const games::tasks::CompiledWorkflow &graph)
      : program(games::tasks::compile_native_program(graph, J::object(), "closure-transitions")),
        executor(program, ports, 30s) {}
    bool terminal() const { return last.state != runtime::TickState::Progress && last.state != runtime::TickState::Waiting; }
    void until(const std::function<bool()> &done, std::chrono::milliseconds budget = 12s) {
        const auto deadline = std::chrono::steady_clock::now() + budget;
        while (!done() && !terminal() && std::chrono::steady_clock::now() < deadline) {
            trace.push_back(executor.current_step_id()); last = executor.tick();
            if (last.state == runtime::TickState::Waiting) std::this_thread::sleep_until(
                std::min(last.wake_at, std::chrono::steady_clock::now() + 30ms));
        }
        check(done(), "TRANSITION_ASSERTION:" + last.code + ":" + executor.current_step_id());
    }
    void observe(const std::string &scene) {
        ports.scene(scene); const auto before = ports.recognitions;
        until([&] { return ports.recognitions > before; });
    }
    void finish() { until([&] { return terminal(); }); }
    void evidence(const char *id) {
        const auto *root = std::getenv("WVD_CLOSURE_ROOT"); check(root && *root, "WVD_CLOSURE_ROOT_REQUIRED");
        std::ofstream(std::filesystem::path(root) / (std::string(id) + ".json")) << J{
            {"scope", "formal factory + FlowExecutor; controlled semantic observations; no real images/device"},
            {"inputs", ports.inputs}, {"trace", trace}, {"progress", executor.progress_snapshot()},
            {"business", ports.business.summary()}, {"terminal_code", last.code}, {"terminal_state", int(last.state)}}.dump(2);
        std::cout << id << " PASS\n";
    }
};
int transitions() {
    const auto leap = games::navigation::time_leap_without_causality("BeautifulOre", "cursedwheel_dhi", true);
    for (const bool network : {false, true}) {
        Driver d(network ? games::recovery::with_boot_recovery(leap, true) : leap);
        d.ports.scene("select");
        d.ports.after_input = [&](const std::string &path) {
            if (path.find("QuickSelect") != std::string::npos) d.ports.scene("leap");
            else if (path.find("QuickLeap") != std::string::npos) d.ports.scene(network ? "network" : "remnant");
            else if (path.find("Retry") != std::string::npos) d.ports.scene("city");
            else throw std::runtime_error("LEAP_UNEXPECTED_INPUT:" + path);
        };
        d.until([&] { return d.ports.inputs.size() == 2; });
        if (!network) {
            const auto pending = d.executor.progress_snapshot().at("pending_inputs");
            d.observe("remnant"); check(!d.terminal(), "LEAP_REMNANT_COMPLETED");
            d.observe("black"); check(!d.terminal() && d.ports.inputs.size() == 2, "LEAP_BLACK_REPLAY");
            check(d.executor.progress_snapshot().at("pending_inputs") == pending, "LEAP_PENDING_REPLACED");
            d.ports.scene("city");
        }
        d.finish(); check(d.last.state == runtime::TickState::Completed && d.ports.inputs.size() == (network ? 3 : 2), "LEAP_REPLAY_OR_INCOMPLETE");
        d.evidence(network ? "TRANS-02" : "TRANS-01");
    }
    {
        Driver d(leap); d.observe("city"); check(!d.terminal() && d.ports.inputs.empty(), "LEAP_PRE_SUBMIT_FALSE_COMPLETION");
        d.ports.stop = true; d.finish(); d.evidence("TRANS-03");
    }
    for (bool accepts : {true, false}) {
        games::MapTarget target{}; target.target = "position"; target.position = games::TaskPoint{505, 760};
        target.swipes.push_back(std::nullopt); target.harken_arrival = accepts;
        Driver d(games::navigation::reach_map_target(target, std::nullopt)); d.ports.scene("map");
        d.ports.after_input = [&](const std::string &path) {
            if (path.find("Select0") != std::string::npos) return;
            if (path.find("AutoMove") != std::string::npos) d.ports.scene("stale-map");
            else if (path.find("CloseStaleMap") != std::string::npos) d.ports.scene("moving");
            else throw std::runtime_error("MAP_UNEXPECTED_INPUT:" + path);
        };
        d.until([&] { return d.executor.current_step_id() == "Moving"; });
        d.observe("harken");
        if (accepts) { d.finish(); check(d.last.state == runtime::TickState::Completed, "LATE_HARKEN_MISSED"); }
        else { check(!d.terminal(), "UNDECLARED_HARKEN_COMPLETED"); d.ports.stop = true; d.finish(); }
        check(d.ports.inputs.size() == 3, "MAP_EXTRA_INPUT"); d.evidence(accepts ? "TRANS-04" : "TRANS-04-negative");
    }
    {
        Driver d(games::combat::enable_auto()); d.ports.scene("popup");
        d.ports.after_input = [&](const auto &) { d.ports.scene("ended"); };
        d.finish(); check(d.ports.inputs.size() == 1 && d.last.code == "combat.auto_not_confirmed_before_battle_end", "COMBAT_EXTRA_INPUT_OR_FALSE_AUTO"); d.evidence("TRANS-05");
    }
    {
        Driver d(games::supply::rest_at_inn(false, true));
        d.ports.business.confirm_event("inn.prepare", "inn_payment_prepared", 1, 1);
        d.ports.business.inn_payment_submitted(false);
        d.observe("black"); check(!d.terminal() && d.ports.business.summary().at("inn_payment_pending") == true, "INN_LOADING_LOST_PENDING");
        d.ports.scene("stayed"); d.ports.after_input = [&](const auto &) { d.ports.scene("city"); };
        d.finish(); check(d.last.state == runtime::TickState::Completed && d.ports.inputs.size() == 1 &&
            d.ports.business.summary().at("inn_rest_completed") == true, "INN_PENDING_RECOVERY_FAILED"); d.evidence("TRANS-06");
    }
    {
        Driver d(games::supply::rest_at_inn(false, true)); d.observe("city");
        check(d.ports.business.summary().at("inn_rest_completed") == false && d.ports.inputs.empty(), "INN_CITY_FALSE_PAID");
        d.ports.stop = true; d.finish(); d.evidence("TRANS-07");
    }
    const auto assets = read("resources/authoring/semantic-assets.json"), flows = read("resources/authoring/public-flows.json");
    J documents = J::object(); for (const auto &doc : flows) documents[doc.at("flow").at("id").get<std::string>()] = doc;
    const games::tasks::PublicFlowLibrary library(documents, assets);
    {
        Driver d(games::tasks::leave_bounty_board(library, "zh-Hant"));
        d.ports.scene("black");
        for (const auto *id : {"guild.bounties.page", "guild.list.back"})
            d.ports.conditions[library.resource_condition(id, "zh-Hant", std::string(id).ends_with("back") ?
                authoring::ResourceUse::Position : authoring::ResourceUse::Observation).dump()] = true;
        d.ports.after_input = [&](const std::string &path) {
            if (path.find("ListBack") != std::string::npos) {
                d.ports.scene("black"); d.ports.conditions[library.resource_condition("guild.bounty.reveal.close", "zh-Hant", authoring::ResourceUse::Position).dump()] = true;
            } else if (path.find("CloseReveal") != std::string::npos) d.ports.scene("story");
            else if (path.find("Story") != std::string::npos) d.ports.scene("city");
            else throw std::runtime_error("GUILD_UNEXPECTED_INPUT:" + path);
        };
        d.finish(); check(d.last.state == runtime::TickState::Completed && d.ports.inputs.size() == 3, "GUILD_LATE_REVEAL_EXIT"); d.evidence("TRANS-08");
    }
    for (const auto *scene : {"outskirts", "harken"}) {
        Driver d(games::recovery::wait_boot_ready(true)); d.ports.scene(scene);
        d.ports.blocker = true;
        const auto before = d.ports.recognitions; d.until([&] { return d.ports.recognitions > before; });
        check(!d.terminal() && d.ports.inputs.empty(), "BOOT_OVERLAY_FALSE_READY");
        d.ports.blocker = false; d.finish(); check(d.last.state == runtime::TickState::Completed && d.ports.inputs.empty(), "BOOT_STABLE_HANDOFF_MISSED");
        d.evidence((std::string("TRANS-09-") + scene).c_str());
    }
    return 0;
}
}

int main(int argc, char **argv) {
    try {
        using J = nlohmann::json;
        if (argc == 2 && std::string(argv[1]) == "--closure-transitions") return closure::transitions();
        if (argc == 2 && std::string(argv[1]) == "--transition-contracts") {
            // 本轮只核对正式工厂的结果条件及可达分支，不连接设备、不执行旧全量矩阵。
            const auto require = [](bool ok, const char *message) {
                if (!ok) throw std::runtime_error(message);
            };
            const auto has = [](const J &values, const std::string &value) {
                return std::find(values.begin(), values.end(), J(value)) != values.end();
            };
            const auto validate = [](const wvd::games::tasks::CompiledWorkflow &flow) {
                wvd::games::tasks::compile_native_program(flow, J::object(), "transition-contracts").validate();
            };
            const auto leap = wvd::games::navigation::time_leap_without_causality("BeautifulOre", "cursedwheel_dhi", true);
            validate(leap);
            for (const auto *name : {"QuickLeapEn", "QuickLeapZhHant", "LeapEn", "LeapZhHant"}) {
                const auto &node = leap.nodes.at(name);
                require(node.at("next") == J{"Done"} &&
                    node.at("operation_args").at("postcondition").at("parameters") == leap.nodes.at("Done").at("observation_args") &&
                    !node.at("operation_args").contains("retry"), "LEAP_MUST_WAIT_FOR_RETURN_WITHOUT_REPLAY");
            }
            require(!has(leap.nodes.at("LeapRoute").at("next"), "Done"), "LEAP_PRE_SUBMIT_MUST_NOT_COMPLETE");
            const auto inn = wvd::games::supply::rest_at_inn(false, true);
            validate(inn);
            validate(wvd::games::supply::rest_at_inn(true, false));
            require(inn.nodes.at("SelectConfirm").at("next").front() == "PendingPaid", "INN_PENDING_RESULT_MUST_REMAIN_REACHABLE");
            require(inn.nodes.at("PendingPaid").at("observation_args").dump().find("/inn_payment/submitted") != std::string::npos,
                "INN_PENDING_MUST_REQUIRE_ACTUAL_SUBMISSION");
            const auto combat = wvd::games::combat::enable_auto();
            validate(combat);
            require(combat.nodes.at("BackPopup").at("next") == combat.nodes.at("Entry").at("next") &&
                has(combat.nodes.at("Unknown0").at("next"), "BattleEnded") &&
                has(combat.nodes.at("Unknown1").at("next"), "BattleEnded"), "AUTO_COMBAT_TRANSITION_EXIT_MISSING");
            wvd::games::WorldDestination destination{"City_RoyalCityLuknalia", std::nullopt};
            const auto world = wvd::games::navigation::travel_world(destination, wvd::games::navigation::WorldArrival::City);
            validate(world);
            require(has(world.nodes.at("Locate").at("next"), "Story"), "WORLD_LOCATE_STORY_EXIT_MISSING");
            for (int i = 0; i < 5; ++i)
                require(has(world.nodes.at("Click" + std::to_string(i)).at("next"), "Story"), "WORLD_CLICK_STORY_EXIT_MISSING");
            wvd::games::MapTarget target{};
            target.target = "position";
            target.position = wvd::games::TaskPoint{505, 760};
            target.swipes.push_back(std::nullopt);
            target.harken_arrival = true;
            const auto map = wvd::games::navigation::reach_map_target(target, std::nullopt);
            validate(map);
            require(has(map.nodes.at("Moving").at("next"), "HarkenArrived") &&
                has(map.nodes.at("CloseStaleMap").at("next"), "HarkenArrived"), "MAP_LATE_HARKEN_EXIT_MISSING");
            const auto boot = wvd::games::vision::boot_probes(false);
            require(std::find(boot.begin(), boot.end(), wvd::games::vision::outskirts_return_button()) != boot.end(),
                "BOOT_OUTSKIRTS_HANDOFF_MISSING");
            std::ifstream assets_file("resources/authoring/semantic-assets.json"), flows_file("resources/authoring/public-flows.json");
            const auto assets = J::parse(assets_file), flows = J::parse(flows_file);
            J documents = J::object();
            for (const auto &doc : flows) documents[doc.at("flow").at("id").get<std::string>()] = doc;
            const wvd::games::tasks::PublicFlowLibrary library(documents, assets);
            const wvd::games::tasks::PublicStepScope public_steps([&](const std::string &id, const J &arguments) {
                return library.compile_step(id, arguments, "zh-Hant");
            });
            validate(wvd::games::navigation::auto_route("dungFlag"));
            for (const auto *locale : {"zh-Hant", "en"}) {
                validate(wvd::games::tasks::leave_bounty_board(library, locale));
                validate(wvd::games::tasks::visit_bounty_board(wvd::games::tasks::BountyVisit::Report,
                    library, documents.at("guild-open-bounty-page"), locale));
            }
            for (const auto &node : documents.at("wheel-open").at("nodes"))
                if (node.at("id") == "Download")
                    require(node.at("parameters").at("postcondition").at("mode") == "not", "DOWNLOAD_SOURCE_IS_NOT_COMPLETION");
            std::cout << "transition contracts: leap, world, map, auto-combat, inn, bounty, boot; no device input\n";
            return 0;
        }
        if (argc == 4 && std::string(argv[1]) == "--bounty") {
            // 只检查本次真实悬赏/委托帧和当前公会编译链，不执行旧全量矩阵。
            const auto read_json = [](const char *path) { std::ifstream in(path); return J::parse(in); };
            const auto catalogue = read_json("resources/authoring/semantic-assets.json");
            const auto documents = read_json("resources/authoring/public-flows.json");
            J library_documents = J::object();
            for (const auto &doc : documents) library_documents[doc.at("flow").at("id").get<std::string>()] = doc;
            const wvd::games::tasks::PublicFlowLibrary library(library_documents, catalogue);
            const auto board = library_documents.at("guild-open-bounty-page");
            const auto report = wvd::games::tasks::visit_bounty_board(
                wvd::games::tasks::BountyVisit::Report, library, board, "zh-Hant");
            wvd::games::tasks::compile_native_program(report, J::object(), "bounty-focused").validate();
            const auto reveal = wvd::games::tasks::visit_bounty_board(
                wvd::games::tasks::BountyVisit::Reveal, library, board, "zh-Hant");
            wvd::games::tasks::compile_native_program(reveal, J::object(), "bounty-reveal-focused").validate();
            if (!report.nodes.contains("RecheckEmpty") || report.nodes.at("PrepareReport")
                    .at("observation_args").dump().find("guild_bounty_card_header") == std::string::npos)
                throw std::runtime_error("BOUNTY_LOADED_PAGE_GUARD_MISSING");
            const auto page = library.resource_condition("guild.bounties.page", "zh-Hant",
                wvd::authoring::ResourceUse::Observation);
            const auto probe_root = std::filesystem::absolute(".local") /
                ("bounty-probe-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            std::filesystem::create_directories(probe_root / "image");
            wvd::recognition::Bundle bundle{probe_root, "bounty-focused", {}};
            for (const auto *name : {"guild_bounties_page_zh_hant", "guild_bounty_selected_zh_hant",
                                     "guild_bounty_card_header", "guild_commissions_page_zh_hant"}) {
                const auto member = std::string("image/") + name + ".png";
                std::filesystem::copy_file(std::filesystem::path("packs/wvd") / member, bundle.root / member);
                bundle.files.push_back({member, wvd::platform::file_sha256(bundle.root / member)});
            }
            wvd::recognition::Service service(bundle, wvd::games::vision::native_handlers(J::object(), "zh-Hant"));
            wvd::contracts::FrameEnvelope frame;
            frame.identity.device_id = "recorded-bounty";
            frame.identity.game_id = "wvd";
            frame.identity.pack_revision = bundle.revision;
            frame.identity.viewport_id = "900x1600";
            frame.identity.generation = 1;
            frame.identity.raw_size = frame.identity.recognition_size = {900, 1600};
            const auto evaluate = [&](const cv::Mat &image, wvd::contracts::RecognitionOutcome expected) {
                if (image.empty() || image.cols != 900 || image.rows != 1600) throw std::runtime_error("BOUNTY_FRAME_INVALID");
                ++frame.identity.frame_id;
                frame.identity.captured_at = frame.identity.capture_finished_at = std::chrono::steady_clock::now();
                frame.raw_bgr = std::make_shared<const std::vector<std::uint8_t>>(
                    image.data, image.data + image.total() * image.elemSize());
                wvd::recognition::Request request{"bounty.page", "1", {0, 0, 900, 1600},
                    wvd::recognition::CustomParameters{"WvdVision", page}};
                const auto result = service.evaluate(frame, frame.identity, request);
                if (result.outcome != expected) throw std::runtime_error("BOUNTY_PAGE_RESULT:" + result.error_code + ":" + result.evidence.dump());
            };
            const auto bounty = cv::imread(argv[2]);
            evaluate(bounty, wvd::contracts::RecognitionOutcome::Hit);
            evaluate(cv::imread(argv[3]), wvd::contracts::RecognitionOutcome::NoHit);
            // 显式构造“仅标题存在、列表未加载”的负例，不能冒充现场复现。
            cv::Mat header_only = cv::Mat::zeros(bounty.size(), bounty.type());
            bounty(cv::Rect(0, 0, 900, 200)).copyTo(header_only(cv::Rect(0, 0, 900, 200)));
            evaluate(header_only, wvd::contracts::RecognitionOutcome::NoHit);
            std::cout << "bounty: real wanted Hit, real commissions NoHit, title-only NoHit; report/reveal graphs valid\n";
            return 0;
        }
        if (argc == 3 && std::string(argv[1]) == "--temporal") {
            // 只复核本次时序探针的正式Service契约，不运行历史作者图矩阵。
            const auto image = cv::imread(argv[2], cv::IMREAD_COLOR);
            if (image.empty() || image.cols != 900 || image.rows != 1600)
                throw std::runtime_error("TEMPORAL_FRAME_INVALID");
            const auto root = std::filesystem::absolute(".local") /
                ("temporal-probe-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            const std::string member = "image/inn_leave_zh_hant.png";
            std::filesystem::create_directories(root / "image");
            std::filesystem::copy_file(std::filesystem::path("packs/wvd") / member, root / member);
            wvd::recognition::Service service({root, "temporal", {{member, wvd::platform::file_sha256(root / member)}}},
                wvd::games::vision::native_handlers(J::object(), "zh-Hant"));
            wvd::contracts::FrameEnvelope frame;
            frame.identity.device_id = "recorded-temporal";
            frame.identity.game_id = "wvd";
            frame.identity.pack_revision = "temporal";
            frame.identity.viewport_id = "900x1600";
            frame.identity.generation = frame.identity.frame_id = 1;
            frame.identity.raw_size = frame.identity.recognition_size = {900, 1600};
            frame.identity.captured_at = frame.identity.capture_finished_at = std::chrono::steady_clock::now();
            frame.raw_bgr = std::make_shared<const std::vector<std::uint8_t>>(
                image.data, image.data + image.total() * image.elemSize());
            using O = wvd::contracts::RecognitionOutcome;
            const auto check_probe = [&](const J &p, O expected) {
                wvd::recognition::Request request{"temporal", "1", {0, 0, 900, 1600},
                    wvd::recognition::CustomParameters{"WvdVision", p}};
                const auto observed = service.evaluate(frame, frame.identity, request);
                if (observed.outcome != expected)
                    throw std::runtime_error("TEMPORAL_CONTRACT:" + observed.error_code + ":" + p.dump());
            };
            J progress{{"mode", "region_changed"}, {"channel", "combat"},
                {"roi", {15, 40, 145, 800}}, {"reset", true}};
            check_probe(progress, O::Hit);
            progress.erase("reset");
            check_probe(progress, O::NoHit);
            const J quiet{{"mode", "region_quiet"}, {"roi", {695, 930, 100, 65}}, {"settle_ms", 250}};
            check_probe(quiet, O::NoHit);
            std::this_thread::sleep_for(std::chrono::milliseconds{300});
            check_probe(quiet, O::NoHit); // 同一旧帧等待不能变成静止证据。
            ++frame.identity.frame_id;
            frame.identity.captured_at = frame.identity.capture_finished_at = std::chrono::steady_clock::now();
            check_probe(quiet, O::Hit);
            check_probe(quiet, O::Hit); // 输入前复核保持一致。
            std::cout << "temporal: baseline box, fresh-frame settling and same-frame recheck passed\n";
            return 0;
        }
        for (const auto &point : {wvd::games::TaskPoint{505, 760},
                                  wvd::games::TaskPoint{506, 821}}) {
            wvd::games::MapTarget target;
            target.target = "position";
            target.hint = wvd::games::MapTarget::Hint::Position;
            target.position = point;
            target.swipes = {wvd::games::TaskSwipe{{100, 1200}, {700, 250}}};
            target.harken_arrival = point[1] == 821;
            const auto map = wvd::games::navigation::reach_map_target(target, "B2FTemple");
            const auto &move = map.nodes.at("AutoMove").at("operation_args");
            const auto recognition = move.at("target_recognition").dump();
            if (!move.at("use_target_center").get<bool>() ||
                move.at("command").contains("x") ||
                recognition.find("AutoMove") == std::string::npos ||
                recognition.find("\"threshold\":0.8") == std::string::npos ||
                recognition.find("\"roi\":[" + std::to_string(point[0] - 116) + "," +
                                 std::to_string(point[1] - 200)) == std::string::npos ||
                move.at("postcondition").dump().find("combat_active") == std::string::npos ||
                map.nodes.contains("HarkenArrived") != target.harken_arrival ||
                (target.harken_arrival && move.at("postcondition").dump().find("harken_floor_return_zh_hant") == std::string::npos))
                throw std::runtime_error("MAP_AUTOMOVE_POPUP_GUARD_INVALID");
        }
        const wvd::games::WvdQuestDefinition scorpion{"Scorpionesses", "quest", J::object()};
        const auto scorpion_route = wvd::games::tasks::scorpion_plan(scorpion, false, "zh-Hant");
        const auto &scorpion_exit = scorpion_route.route().back();
        if (scorpion_route.route().size() != 2 || scorpion_exit.target != "dungFlag" ||
            scorpion_exit.position || !scorpion_exit.harken_arrival)
            throw std::runtime_error("SCORPION_EXIT_MUST_USE_SHORTCUT");
        const J route_profile{{"WHO_WILL_OPEN_IT", 0}, {"QUICK_DISARM_CHEST", false},
            {"MAX_TRY_LIMIT", 10}, {"BYPASS_THE_WALL", false},
            {"TASK_POINT_STRATEGY", J::object()}, {"STRATEGY", J::array()}};
        const auto scorpion_dungeon = wvd::games::tasks::traverse_dungeon(
            scorpion_route, route_profile, {}, false);
        if (!scorpion_dungeon.nodes.contains("Route1_Choose") ||
            scorpion_dungeon.nodes.contains("Route1_Search0") ||
            scorpion_dungeon.nodes.at("Route1_Choose").at("operation_args")
                .at("target_recognition").dump().find("dungFlag") == std::string::npos ||
            scorpion_dungeon.nodes.at("Confirm1").at("observation_args").dump()
                .find("harken_floor_return_zh_hant") == std::string::npos)
            throw std::runtime_error("SCORPION_EXIT_GRAPH_NOT_SHORTCUT_TO_HARKEN");
        scorpion_dungeon.validate();
        const auto harken_exit = wvd::games::navigation::leave_harken();
        const auto &return_post = harken_exit.nodes.at("Outskirts").at("operation_args").at("postcondition");
        if (harken_exit.nodes.at("Entry").at("next").front() != "Story" ||
            return_post.dump().find("story_auto_control") == std::string::npos ||
            return_post.dump().find("chest_reward_advance") == std::string::npos ||
            !harken_exit.nodes.at("Story").at("operation_args").at("use_target_center").get<bool>() ||
            harken_exit.nodes.at("Story").at("next").back() != "Entry")
            throw std::runtime_error("HARKEN_RETURN_STORY_HANDOFF_MISSING");
        harken_exit.validate();
        wvd::games::tasks::PipelineCompiler network_parent("network-parent");
        network_parent.fixed_click("Entry", {{"mode", "template"}, {"image", "ruins"}},
            {{"mode", "template"}, {"image", "cursedWheel"}}, {450, 600}, {"Terminal"});
        const auto with_network = wvd::games::recovery::with_boot_recovery(network_parent.finish(), false);
        const auto network_program = wvd::games::tasks::compile_native_program(with_network, J::object(), "network-test");
        const auto &parent_await = network_program.definitions.at("Entry").steps.at("Task_Entry@await");
        if (parent_await.event_policy.size() != 3 || parent_await.event_policy.front().id != "wvd-network-retry" ||
            parent_await.event_policy.front().category != wvd::workflow::EventClass::Exception ||
            parent_await.event_policy.front().resume != wvd::workflow::ResumeMode::Reobserve ||
            parent_await.event_policy.back().category != wvd::workflow::EventClass::Special)
            throw std::runtime_error("NETWORK_EVENT_AWAIT_NOT_CONNECTED");
        if (argc == 2 && std::string(argv[1]) == "--dispatch") {
            std::cout << "deferred network observer compiled\n";
            auto turn_profile = route_profile;
            turn_profile["STRATEGY"] = J::array({J{{"group_name", "技能检查"}, {"skill_settings", J::array({
                J{{"role_var", ""}, {"skill_var", "左上技能"}, {"skill_lvl", 5}, {"target_var", "next"}}
            })}}});
            const auto turn = wvd::games::combat::take_turn(turn_profile, {});
            turn.validate();
            std::cout << "normal combat turn compiled\n";
            if (turn.nodes.at("Prepare").at("observation_args").dump().find("blocking_screen") != std::string::npos ||
                !turn.nodes.at("Interrupt").value("unexpected_only", false) ||
                turn.nodes.at("Prepare").at("check_group") != "combat")
                throw std::runtime_error("NORMAL_TURN_STILL_SCANS_BLOCKING_SCREENS");
            for (const auto &[name, node] : turn.nodes.items()) {
                if (node.value("unexpected_only", false) || node.value("binding", "") == "RequireRecovery") continue;
                for (const auto *field : {"observation_args", "operation_args"})
                    if (node.contains(field) && node.at(field).dump().find("blocking_screen") != std::string::npos)
                        throw std::runtime_error("NORMAL_SKILL_PHASE_STILL_SCANS_EXCEPTIONS:" + name);
            }
            const auto compiled = wvd::games::tasks::compile_native_program(
                wvd::games::recovery::with_boot_recovery(scorpion_dungeon, false), J::object(), "dispatch-test");
            compiled.validate();
            for (const auto &[id, definition] : compiled.definitions)
                for (const auto &[name, node] : definition.steps)
                    if (name.starts_with("Task_") && (node.check_group == "combat" || node.check_group == "chest")) {
                        for (const auto &rule : node.event_policy)
                            if (rule.id.starts_with("wvd-") && rule.category == wvd::workflow::EventClass::Overlay)
                                throw std::runtime_error("BUSINESS_DOMAIN_HAS_PROACTIVE_EXCEPTION_SCAN:" + name);
                    }
            std::cout << "production compiler: combat/chest groups, deferred handlers and await scopes passed\n";
            return 0;
        }
        const bool network_sample = argc == 3 && std::string(argv[1]) == "--network";
        const bool skill_sample = argc == 3 && std::string(argv[1]) == "--skill";
        wvd::games::tasks::PipelineCompiler skill_probes("skill-probes");
        skill_probes.observe("Entry", {{"mode", "any"}, {"conditions", {
            {{"mode", "skill_level"}, {"level", 1}},
            {{"mode", "skill_level"}, {"level", 3}, {"selected", true}},
            {{"mode", "skill_level"}, {"level", 5}, {"selected", true}}}}}, {"Terminal"});
        const auto skill_samples = skill_probes.finish();
        // 可选现场帧验证只读正式资源包，不需要设备输入或改写用户数据。
        if (argc == 2 || network_sample || skill_sample) {
            const auto image = cv::imread(argv[network_sample || skill_sample ? 2 : 1], cv::IMREAD_COLOR);
            if (image.empty() || image.cols != 900 || image.rows != 1600)
                throw std::runtime_error("HARKEN_STORY_SAMPLE_INVALID");
            std::ifstream input("packs/wvd/manifest.json");
            J manifest;
            input >> manifest;
            wvd::recognition::Bundle bundle{std::filesystem::absolute("packs/wvd"),
                manifest.at("revision"), {}};
            for (const auto &member : manifest.at("files"))
                bundle.files.push_back({member.at("path"), member.at("sha256")});
            const auto sample_root = std::filesystem::absolute(".local") /
                ("test-harken-story-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            wvd::recognition::Bundle sample_bundle{sample_root, bundle.revision, {}};
            std::set<std::string> copied;
            for (const auto &name : skill_sample ? skill_samples.images : network_sample ? with_network.images : harken_exit.images) {
                const auto selected = wvd::games::vision::resolve_image_source(
                    bundle, manifest.value("aliases", J::object()), name);
                if (!copied.insert(selected.relative_path).second) continue;
                const auto target = sample_root / selected.relative_path;
                std::filesystem::create_directories(target.parent_path());
                std::filesystem::copy_file(bundle.root / selected.relative_path, target);
                sample_bundle.files.push_back({selected.relative_path, wvd::platform::file_sha256(target)});
            }
            auto service = std::make_unique<wvd::recognition::Service>(sample_bundle,
                wvd::games::vision::native_handlers(manifest.value("aliases", J::object()), "zh-Hant"));
            wvd::contracts::FrameEnvelope frame;
            frame.identity.device_id = "recorded-harken-story";
            frame.identity.game_id = "wvd";
            frame.identity.pack_revision = bundle.revision;
            frame.identity.viewport_id = "900x1600";
            frame.identity.generation = 1;
            frame.identity.frame_id = 1;
            frame.identity.raw_size = {900, 1600};
            frame.identity.recognition_size = frame.identity.raw_size;
            frame.identity.captured_at = std::chrono::steady_clock::now();
            frame.identity.capture_finished_at = frame.identity.captured_at;
            frame.raw_bgr = std::make_shared<const std::vector<std::uint8_t>>(
                image.data, image.data + image.total() * image.elemSize());
            const auto checks = skill_sample ? std::vector<std::pair<J, wvd::contracts::RecognitionOutcome>>{
                {J{{"mode", "skill_level"}, {"level", 1}}, wvd::contracts::RecognitionOutcome::Hit},
                {J{{"mode", "skill_level"}, {"level", 5}}, wvd::contracts::RecognitionOutcome::Hit},
                {J{{"mode", "skill_level"}, {"level", 3}, {"selected", true}}, wvd::contracts::RecognitionOutcome::Hit},
                {J{{"mode", "skill_level"}, {"level", 5}, {"selected", true}}, wvd::contracts::RecognitionOutcome::NoHit}}
                : network_sample ? std::vector<std::pair<J, wvd::contracts::RecognitionOutcome>>{
                {wvd::games::vision::network_prompt_zh_hant(), wvd::contracts::RecognitionOutcome::Hit},
                {wvd::games::vision::network_retry_prompt(), wvd::contracts::RecognitionOutcome::Hit},
                {wvd::games::vision::network_retry_button_zh_hant(), wvd::contracts::RecognitionOutcome::Hit}}
                : std::vector<std::pair<J, wvd::contracts::RecognitionOutcome>>{
                {harken_exit.nodes.at("Story").at("observation_args"), wvd::contracts::RecognitionOutcome::Hit},
                {harken_exit.nodes.at("City").at("observation_args"), wvd::contracts::RecognitionOutcome::NoHit},
                {return_post.at("parameters"), wvd::contracts::RecognitionOutcome::Hit}};
            for (const auto &[condition, expected] : checks) {
                const wvd::recognition::Request request{"custom", "recorded-harken-story", {0, 0, 900, 1600},
                    wvd::recognition::CustomParameters{"WvdVision", condition}};
                const auto observed = service->evaluate(frame, frame.identity, request);
                if (observed.outcome != expected)
                    throw std::runtime_error("HARKEN_STORY_SAMPLE_MISMATCH:" + observed.error_code);
            }
            service.reset();
            std::cout << (skill_sample ? "recorded skill detail: level 1/5 Hit, selected 3 Hit, selected 5 NoHit\n"
                          : network_sample ? "recorded network popup: prompt Hit, retry Hit, await event connected\n"
                                        : "recorded harken story: story Hit, city NoHit, return post Hit\n");
            if (sample_root.parent_path() != std::filesystem::absolute(".local"))
                throw std::runtime_error("HARKEN_STORY_TEST_PATH_INVALID");
            std::filesystem::remove_all(sample_root);
        }
        J special_profile{{"LANGUAGE", "zh_CN"}, {"TASK_SPECIFIC_CONFIG", false},
            {"DEFAULT_OVERALL_STRATEGY", "普通方案"},
            {"STRATEGY", J::array({J{{"group_name", "普通方案"}, {"skill_settings", J::array()}},
                                   J{{"group_name", "特殊方案"}, {"skill_settings", J::array()}}})},
            {"TASK_POINT_STRATEGY", {{"special_combat", {{"skull", true}, {"portrait", true},
                {"portrait_image", "combat_scorpion_portrait"}, {"normal_strategy", "普通方案"},
                {"special_strategy", "特殊方案"}}}}}};
        wvd::games::CombatStrategy selected(special_profile);
        selected.reload(0);
        selected.begin_encounter(false);
        if (selected.summary().at("current").at("group_name") != "普通方案")
            throw std::runtime_error("SPECIAL_COMBAT_NORMAL_STRATEGY_INVALID");
        selected.begin_encounter(true);
        if (selected.summary().at("current").at("group_name") != "特殊方案")
            throw std::runtime_error("SPECIAL_COMBAT_BOSS_STRATEGY_INVALID");
        const auto both = wvd::games::combat::fight_encounter(special_profile, {});
        const auto special_condition = both.nodes.at("Special").at("observation_args").dump();
        if (!both.nodes.contains("WaitingForMenu") ||
            special_condition.find("combat_special_skull") == std::string::npos ||
            special_condition.find("combat_scorpion_portrait") == std::string::npos ||
            special_condition.find("\"mode\":\"any\"") == std::string::npos)
            throw std::runtime_error("SPECIAL_COMBAT_OR_BRANCH_INVALID");
        special_profile["TASK_POINT_STRATEGY"]["special_combat"]["portrait"] = false;
        const auto skull_only = wvd::games::combat::fight_encounter(special_profile, {});
        if (skull_only.nodes.at("Special").at("observation_args").dump().find("combat_scorpion_portrait") != std::string::npos)
            throw std::runtime_error("SPECIAL_COMBAT_SKULL_ONLY_INVALID");
        special_profile["TASK_POINT_STRATEGY"]["special_combat"]["portrait"] = true;
        special_profile["TASK_POINT_STRATEGY"]["special_combat"]["skull"] = false;
        const auto portrait_only = wvd::games::combat::fight_encounter(special_profile, {});
        if (portrait_only.nodes.at("Special").at("observation_args").dump().find("combat_special_skull") != std::string::npos)
            throw std::runtime_error("SPECIAL_COMBAT_PORTRAIT_ONLY_INVALID");
        special_profile["TASK_POINT_STRATEGY"]["special_combat"]["portrait"] = false;
        const auto legacy = wvd::games::combat::fight_encounter(special_profile, {});
        if (legacy.nodes.contains("Special") || !legacy.nodes.contains("Observed"))
            throw std::runtime_error("SPECIAL_COMBAT_DISABLED_CHANGED_ENTRY");
        J document{{"schema", 1},
            {"flow", {{"id", "slot-six"}, {"name", "Slot six"}, {"description", ""}}},
            {"entry", "Slot"},
            {"nodes", J::array({
                J{{"id", "Slot"}, {"type", "slot"}, {"name", "Slot"},
                  {"parameters", {{"name", "extra"}, {"calls", J::array({
                      J{{"flow_id", "shared"}}, J{{"flow_id", "shared"}}})}}},
                  {"repeat_limit", 6}},
                J{{"id", "Done"}, {"type", "end"}, {"name", "Done"},
                  {"parameters", {{"outcome", "success"}}}}})},
            {"edges", J::array({J{{"id", "slot_done"}, {"from", "Slot"},
                {"to", "Done"}, {"outcome", "success"}, {"order", 0}}})},
            {"layout", {{"nodes", J::array({J{{"node_id", "Slot"}, {"x", 40}, {"y", 100}},
                J{{"node_id", "Done"}, {"x", 300}, {"y", 100}}})},
                {"viewport", {{"x", 0}, {"y", 0}, {"zoom", 1}}}}},
            {"execution", {{"time_limit_ms", 30000}}}};
        int resolutions = 0;
        const auto compiled = wvd::games::tasks::compile_author_workflow(
            document, {}, [&](const J &) {
                ++resolutions;
                wvd::games::tasks::PipelineCompiler child("test.shared");
                child.route("Entry", {"Terminal"});
                return wvd::games::tasks::AuthorWorkflowCompilation{child.finish()};
            });
        auto program = wvd::games::tasks::compile_native_program(
            compiled.workflow, compiled.source_paths, "slot-six-native");
        program.validate();
        const auto &root = program.definitions.at(program.root_definition);
        if (resolutions != 2 || root.steps.at("Author_Slot").max_hit != 6 ||
            root.steps.at("Author_Slot_Call0").max_hit != 6 ||
            root.steps.at("Author_Slot_Call1").max_hit != 6 ||
            program.definitions.size() != 3)
            throw std::runtime_error("NATIVE_SLOT_CALL_SCOPE_INVALID");
        const auto &first = std::get<wvd::workflow::Call>(
            root.steps.at("Author_Slot_Call0").data);
        const auto &second = std::get<wvd::workflow::Call>(
            root.steps.at("Author_Slot_Call1").data);
        if (first.definition == second.definition ||
            !program.definitions.contains(first.definition) ||
            !program.definitions.contains(second.definition))
            throw std::runtime_error("NATIVE_SLOT_CALL_SHARED_COUNTERS");
        std::ifstream flows("resources/authoring/public-flows.json");
        std::ifstream resources("resources/authoring/semantic-assets.json");
        if (!flows || !resources) throw std::runtime_error("AUTHOR_RESOURCE_SOURCE_MISSING");
        J documents = J::object(), entries, catalogue;
        flows >> entries;
        resources >> catalogue;
        wvd::authoring::SemanticAssets semantic(catalogue);
        const auto open = wvd::games::vision::chest_open_probes();
        const auto stages = wvd::games::vision::chest_stage_probes();
        const auto boot = wvd::games::vision::boot_probes(false);
        if (open.at(1) != semantic.condition("chest.open.option", "zh-Hant") ||
            stages.back() != semantic.condition("chest.reward.page", "") ||
            std::find(boot.begin(), boot.end(), semantic.condition("guild.commissions.page", "zh-Hant")) == boot.end() ||
            std::find(boot.begin(), boot.end(), semantic.condition("guild.bounties.page", "zh-Hant")) == boot.end())
            throw std::runtime_error("NATIVE_SEMANTIC_PROBES_DIVERGED");
        for (const auto &entry : entries)
            documents[entry.at("flow").at("id").get<std::string>()] = entry;
        const auto &wheel = documents.at("wheel-select-target");
        const auto &wheel_nodes = wheel.at("nodes");
        const auto &wheel_edges = wheel.at("edges");
        auto author_node = [&](const std::string &id) -> J {
            for (const auto &node : wheel_nodes)
                if (node.at("id") == id) return node;
            throw std::runtime_error("WHEEL_AUTHOR_NODE_MISSING:" + id);
        };
        auto author_edge = [&](const std::string &from, const std::string &to) {
            for (const auto &edge : wheel_edges)
                if (edge.at("from") == from && edge.at("to") == to) return true;
            return false;
        };
        if (!author_edge("Entry", "QuickSelect") || !author_edge("Entry", "FindChapter") ||
            !author_edge("Scroll0", "FindTarget") || author_edge("Scroll0", "Scroll1") ||
            author_node("NextFromAbyss").at("parameters").at("postcondition").at("id") != "wheel.chapter.waterway" ||
            author_node("NextFromWaterway").at("parameters").at("postcondition").at("id") != "wheel.chapter.fortress" ||
            author_node("NextFromFortress").at("parameters").at("postcondition").at("id") != "wheel.chapter.dhi")
            throw std::runtime_error("WHEEL_AUTHOR_DIRECTION_OR_TARGET_RECHECK_INVALID");
        const auto ore_leap = wvd::games::navigation::time_leap_without_causality(
            "BeautifulOre", "cursedwheel_dhi", true);
        const auto fortress_leap = wvd::games::navigation::time_leap_without_causality(
            "GhostsOfYore", "cursedwheel_impregnableFortress", true);
        if (ore_leap.nodes.contains("Reset0") || ore_leap.nodes.contains("Scroll1") ||
            ore_leap.nodes.at("MoveFrom0").at("operation_args").at("target_recognition").dump().find("cursedWheelTapRight") == std::string::npos ||
            fortress_leap.nodes.at("MoveFrom3").at("operation_args").at("target_recognition").dump().find("cursedWheelTapLeft") == std::string::npos ||
            ore_leap.nodes.at("QuickLeapZhHant").at("next") != J{"Done"} ||
            ore_leap.nodes.at("LeapZhHant").at("next") != J{"Done"} ||
            ore_leap.nodes.at("Scroll0").at("next").at(0) != "FindTarget" ||
            ore_leap.nodes.at("Scroll0").at("max_hit") != 6)
            throw std::runtime_error("WHEEL_NATIVE_DIRECTION_OR_TARGET_RECHECK_INVALID");
        wvd::games::tasks::PublicFlowLibrary library(documents, catalogue);
        const auto &board = documents.at("guild-open-bounty-page");
        auto reveal = wvd::games::tasks::visit_bounty_board(
            wvd::games::tasks::BountyVisit::Reveal, library, board, "zh-Hant");
        auto reveal_program = wvd::games::tasks::compile_native_program(
            reveal, reveal.authoring.value("source_paths", J::object()), "bounty-reveal");
        reveal_program.validate();
        if (!reveal.authoring.contains("public_definitions") ||
            !reveal.authoring.at("public_definitions").contains("guild-open-bounty-page") ||
            !reveal.nodes.contains("CloseReveal") ||
            reveal.nodes.at("CloseReveal").at("operation_args").at("target_recognition")
                .at("parameters").at("image") != "guild_reveal_close_zh_hant")
            throw std::runtime_error("BOUNTY_PUBLIC_DEFINITION_NOT_FROZEN");
        wvd::games::tasks::PipelineCompiler map_graph("test.worldmap");
        map_graph.observe("Entry", wvd::games::tasks::PipelineCompiler::image("worldmapflag"), {"Terminal"});
        auto map_workflow = map_graph.finish();
        wvd::games::tasks::localize_task_assets(map_workflow, catalogue, "zh-Hant");
        const auto map_program = wvd::games::tasks::compile_native_program(
            map_workflow, J::object(), "zh-worldmap");
        map_program.validate();
        bool map_composite = false;
        for (const auto &[id, node] : map_workflow.nodes.items()) {
            (void)id;
            if (node.value("observation", "") == "Registered" &&
                node.value("recognizer", "") == "WvdVision")
                map_composite = node.at("observation_args").value("mode", "") == "all";
        }
        if (!map_composite) throw std::runtime_error("ZH_WORLDMAP_COMPOSITE_MISSING");
        wvd::games::tasks::PipelineCompiler entry_graph("test.zh-abyss-entry");
        entry_graph.observe("Entry", wvd::games::tasks::PipelineCompiler::any({
            wvd::games::tasks::PipelineCompiler::image("outskirts_abyss_zh_hant"),
            wvd::games::tasks::PipelineCompiler::image("outskirts_abyss_b2f_zh_hant"),
            wvd::games::tasks::PipelineCompiler::image("map_abyss_b2f_zh_hant"),
            wvd::games::tasks::PipelineCompiler::image("AutoMove"),
            wvd::games::tasks::PipelineCompiler::image("mapFlag")}), {"Terminal"});
        auto entry_workflow = entry_graph.finish();
        wvd::games::tasks::localize_task_assets(entry_workflow, catalogue, "zh-Hant");
        const auto entry_conditions = entry_workflow.nodes.at("Entry").at("observation_args").dump();
        if (entry_conditions.find("outskirts_abyss_zh_hant") == std::string::npos ||
            entry_conditions.find("outskirts_abyss_b2f_zh_hant") == std::string::npos ||
            entry_conditions.find("map_abyss_b2f_zh_hant") == std::string::npos ||
            entry_conditions.find("map_auto_move_zh_hant") == std::string::npos ||
            entry_conditions.find("dungeon_map_close_zh_hant") == std::string::npos)
            throw std::runtime_error("ZH_ABYSS_ENTRY_ASSETS_MISSING");
        wvd::games::tasks::PipelineCompiler combat_graph("test.combat-locale");
        combat_graph.observe("Entry", wvd::games::tasks::PipelineCompiler::any({
            wvd::games::tasks::PipelineCompiler::image("combat_skill_detail"),
            wvd::games::tasks::PipelineCompiler::image("combat_skill_confirm")}), {"Terminal"});
        auto zh_combat = combat_graph.finish();
        auto en_combat = zh_combat;
        wvd::games::tasks::localize_task_assets(zh_combat, catalogue, "zh-Hant");
        wvd::games::tasks::localize_task_assets(en_combat, catalogue, "en");
        const auto zh_conditions = zh_combat.nodes.at("Entry").at("observation_args").dump();
        const auto en_conditions = en_combat.nodes.at("Entry").at("observation_args").dump();
        if (zh_conditions.find("combat_skill_detail_zh_hant") == std::string::npos ||
            zh_conditions.find("combat_skill_confirm_zh_hant") == std::string::npos ||
            en_conditions.find("spellskill/skillDetail") == std::string::npos ||
            en_conditions.find("\"image\":\"OK\"") == std::string::npos)
            throw std::runtime_error("COMBAT_LOCALE_ASSETS_MISSING");
        wvd::games::tasks::PipelineCompiler healing_graph("test.healing-locale");
        healing_graph.observe("Entry", wvd::games::tasks::PipelineCompiler::any({
            wvd::games::tasks::PipelineCompiler::image("trait"),
            wvd::games::tasks::PipelineCompiler::image("recover")}), {"Terminal"});
        auto zh_healing = healing_graph.finish();
        auto en_healing = zh_healing;
        wvd::games::tasks::localize_task_assets(zh_healing, catalogue, "zh-Hant");
        wvd::games::tasks::localize_task_assets(en_healing, catalogue, "en");
        const auto zh_healing_conditions = zh_healing.nodes.at("Entry").at("observation_args").dump();
        const auto en_healing_conditions = en_healing.nodes.at("Entry").at("observation_args").dump();
        if (zh_healing_conditions.find("character_panel_zh_hant") == std::string::npos ||
            zh_healing_conditions.find("recovery_panel_zh_hant") == std::string::npos ||
            en_healing_conditions.find("\"image\":\"trait\"") == std::string::npos ||
            en_healing_conditions.find("\"image\":\"recover\"") == std::string::npos)
            throw std::runtime_error("HEALING_LOCALE_ASSETS_MISSING");
        const auto common = wvd::games::recovery::clear_common_screens(false);
        const auto common_ready = common.nodes.at("Ready").at("observation_args").dump();
        if (common_ready.find("harken_floor_move_zh_hant") == std::string::npos ||
            common_ready.find("harken_floor_return_zh_hant") == std::string::npos)
            throw std::runtime_error("HARKEN_COMMON_HANDOFF_MISSING");
        const auto original = std::filesystem::absolute("packs/wvd");
        const auto test_root = std::filesystem::absolute(".local") /
            ("test-worldmap-publication-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        const auto source = test_root / "source";
        const auto published_path = test_root / "published";
        std::filesystem::create_directories(source / "image");
        wvd::recognition::Bundle map_bundle{source, "test-worldmap", {}};
        for (const auto &name : map_workflow.images) {
            const auto relative = std::string("image/") + name;
            std::filesystem::create_directories((source / relative).parent_path());
            std::filesystem::copy_file(original / relative, source / relative);
            map_bundle.files.push_back({relative,
                wvd::platform::file_sha256(source / relative)});
        }
        {
            auto publication = wvd::games::tasks::publish_native(
                map_workflow, map_bundle, published_path, J::object());
            if (publication.program.revision.empty() ||
                !publication.identity.at("image_sources").contains("worldmap_close_zh_hant.png") ||
                !publication.identity.at("image_sources").contains("worldmap_zoom_plus_zh_hant.png"))
                throw std::runtime_error("ZH_WORLDMAP_PUBLICATION_INVALID");
        }
        if (std::filesystem::weakly_canonical(test_root.parent_path()) !=
            std::filesystem::weakly_canonical(std::filesystem::absolute(".local")))
            throw std::runtime_error("ZH_WORLDMAP_TEST_PATH_INVALID");
        std::filesystem::remove_all(test_root);
        auto report = wvd::games::tasks::visit_bounty_board(
            wvd::games::tasks::BountyVisit::Report, library, board, "en");
        auto report_program = wvd::games::tasks::compile_native_program(
            report, J::object(), "bounty-report-en");
        report_program.validate();
        if (!report.nodes.contains("Report"))
            throw std::runtime_error("EN_BOUNTY_REPORT_LOST");
        auto zh_report = wvd::games::tasks::visit_bounty_board(
            wvd::games::tasks::BountyVisit::Report, library, board, "zh-Hant");
        auto zh_report_program = wvd::games::tasks::compile_native_program(
            zh_report, J::object(), "bounty-report-zh-Hant");
        zh_report_program.validate();
        if (!zh_report.nodes.contains("CloseReceipt") ||
            !zh_report.nodes.contains("ReportConfirmed") ||
            !zh_report.nodes.contains("NoMoreReports") ||
            zh_report.nodes.at("Report").at("next").front() != "CloseReceipt" ||
            zh_report.nodes.at("ReportConfirmed").at("next").front() != "CheckReports" ||
            !zh_report.nodes.contains("RecheckEmpty") ||
            zh_report.nodes.at("PrepareReport").at("observation_args").dump()
                .find("guild_bounty_card_header") == std::string::npos)
            throw std::runtime_error("ZH_BOUNTY_REPORT_RECEIPT_FLOW_MISSING");
        const auto inn = wvd::games::supply::rest_at_inn(false, true);
        const auto inn_program = wvd::games::tasks::compile_native_program(inn, J::object(), "inn-zh-Hant");
        inn_program.validate();
        if (!inn.nodes.contains("ConfirmZh") || !inn.nodes.contains("ConfirmEn") ||
            inn.nodes.at("ConfirmZh").at("operation_args").at("target_recognition")
                .at("parameters").at("image") != "inn_confirm_zh_hant" ||
            inn.nodes.at("PreparePayment").at("next").at(0) != "SelectConfirm")
            throw std::runtime_error("ZH_INN_PAYMENT_CONFIRM_MISSING");
        std::cout << "native author slot six and independent calls passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
