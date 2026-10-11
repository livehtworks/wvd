
#include "games/wvd/tasks/public_flow_library.hpp"
#include "games/wvd/tasks/public_step_scope.hpp"
#include "games/wvd/tasks/locale_assets.hpp"
#include "games/wvd/tasks/native_program.hpp"
#include "games/wvd/tasks/task_plan.hpp"
#include "games/wvd/tasks/dungeon_route.hpp"
#include "games/wvd/tasks/departure.hpp"
#include "games/wvd/tasks/bounty_visit.hpp"
#include "games/wvd/tasks/gold_income.hpp"
#include "games/wvd/tasks/giant.hpp"
#include "games/wvd/supply/dungeon_recover.hpp"
#include "games/wvd/recovery/boot.hpp"
#include "games/wvd/supply/inn.hpp"
#include "games/wvd/combat/auto_combat.hpp"
#include "games/wvd/combat/turn.hpp"
#include "games/wvd/navigation/time_leap.hpp"
#include "games/wvd/navigation/world_travel.hpp"
#include "games/wvd/navigation/dungeon_entry.hpp"
#include "games/wvd/navigation/harken_exit.hpp"
#include "games/wvd/vision/boot_probes.hpp"
#include "games/wvd/business_condition.hpp"
#include "runtime/flow_executor.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <thread>

// Real production factories and FlowExecutor, with controlled frame/recognition
// and submission ports. This is not Service, NativeInputGate, a device or game.
// The legacy macro is only for the external before/after proof against V1.
namespace shared_page_actions_contract_test {
using J = nlohmann::json;
using C = wvd::games::tasks::PipelineCompiler;
using Graph = wvd::games::tasks::CompiledWorkflow;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
namespace tasks = wvd::games::tasks;
namespace recovery = wvd::games::recovery;
namespace navigation = wvd::games::navigation;
namespace vision = wvd::games::vision;

inline J read_json(const std::filesystem::path &path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("SHARED_ACTION_TEST_INPUT_MISSING:" + path.string());
    return J::parse(input);
}
inline void write_json(const std::filesystem::path &path, const J &value) {
    std::ofstream output(path); output << value.dump(2) << '\n';
    if (!output) throw std::runtime_error("SHARED_ACTION_TEST_OUTPUT_FAILED");
}
inline Graph world(const wvd::games::WorldDestination &destination,
                   navigation::WorldArrival arrival, bool allowed) {
#ifdef WVD_SHARED_ACTIONS_LEGACY_BASELINE
    (void)allowed; return navigation::travel_world(destination, arrival);
#else
    return navigation::travel_world(destination, arrival, allowed);
#endif
}
inline Graph city_to_city(const wvd::games::WorldDestination &destination, bool allowed) {
#ifdef WVD_SHARED_ACTIONS_LEGACY_BASELINE
    (void)allowed; return navigation::travel_city_to_city(destination);
#else
    return navigation::travel_city_to_city(destination, allowed);
#endif
}
inline Graph entry(const wvd::games::WvdTaskPlan &plan, bool allowed) {
#ifdef WVD_SHARED_ACTIONS_LEGACY_BASELINE
    (void)allowed; return navigation::enter_dungeon(plan);
#else
    return navigation::enter_dungeon(plan, false, allowed);
#endif
}
inline Graph departure(const wvd::games::WvdTaskPlan &plan, const J &profile, bool allowed) {
#ifdef WVD_SHARED_ACTIONS_LEGACY_BASELINE
    (void)allowed; return tasks::prepare_departure(plan, profile);
#else
    return tasks::prepare_departure(plan, profile, false, allowed);
#endif
}
inline J snapshot(const Graph &graph) {
    J budgets = J::object();
    for (const auto &[name, budget] : graph.definition_budgets) budgets[name] = budget.count();
    return {{"kind", graph.kind}, {"entry", graph.entry}, {"terminal", graph.terminal},
        {"time_limit_ms", graph.time_limit.count()},
        {"declared_budget_ms", graph.declared_budget ? J(graph.declared_budget->count()) : J(nullptr)},
        {"definition_budgets_ms", budgets}, {"definition_checks", graph.definition_checks},
        {"event_responsibilities", graph.event_responsibilities},
        {"event_responsibility_nodes", graph.event_responsibility_nodes},
        {"event_scopes", graph.event_scopes}, {"nodes", graph.nodes}, {"images", graph.images},
        {"handoffs", graph.handoffs}, {"authoring", graph.authoring}};
}

struct Surface { bool download{}, arrived{}, combat{}, reveal{}, close{}; };
struct Ports final : wvd::runtime::FlowPorts {
    std::string locale;
    Surface surface;
    std::uint64_t frame_id{}, epoch{};
    J commands = J::array();
    J reveal_button;
    bool gold_case{};
    explicit Ports(std::string lang, Surface initial, J reveal)
        : locale(std::move(lang)), surface(initial), reveal_button(std::move(reveal)) {}
    bool matches(const J &probe, const J &reference) const {
        auto left = probe, right = reference;
        left.erase("locale_only"); right.erase("locale_only");
        return left == right;
    }
    bool evaluate(const J &p) const {
        if (p.contains("locale_only") && p.at("locale_only").get<std::string>() != locale) return false;
        if (matches(p, vision::royal_city())) return surface.arrived;
        if (matches(p, reveal_button)) return surface.reveal;
        const auto mode = p.value("mode", "");
        if (mode == "business" && gold_case) return wvd::games::business_condition(business_summary(),p);
        if (mode == "all" || mode == "any" || mode == "not") {
            bool all = true, any = false;
            for (const auto &child : p.at("conditions")) {
                const auto hit = evaluate(child); all &= hit; any |= hit;
            }
            return mode == "all" ? all : mode == "any" ? any : !any;
        }
        if (mode == "combat_active") return surface.combat;
        if (mode == "input_clear") {
            for (const auto &probe : vision::input_blockers(p.value("phase", ""), locale))
                if (evaluate(probe)) return false;
            return true;
        }
        if (mode == "template") {
            const auto image = p.value("image", "");
            if (image == (locale == "en" ? "startdownload" : "startdownload_zh_hant")) return surface.download;
            if (image == "close" || image == "shared_close_cross") return surface.close;
        }
        return false;
    }
    wvd::contracts::FrameEnvelope capture() override {
        wvd::contracts::FrameEnvelope frame; auto &id = frame.identity;
        id.device_id = "shared-actions-memory-device"; id.game_id = "wvd";
        id.pack_revision = "shared-actions-contract"; id.viewport_id = "900x1600";
        id.instance_id = "shared-actions-instance"; id.instance_created_identity = "created";
        id.generation = id.connection_generation = 1; id.action_epoch = epoch; id.frame_id = ++frame_id;
        id.raw_size = id.recognition_size = {900, 1600};
        id.captured_at = id.capture_finished_at = Clock::now(); return frame;
    }
    wvd::contracts::Observation recognize(const wvd::contracts::FrameEnvelope &frame,
                                          const wvd::recognition::Request &request) override {
        const auto &p = std::get<wvd::recognition::CustomParameters>(request.parameters).parameters;
        wvd::contracts::Observation result; result.basis = frame.identity;
        result.outcome = evaluate(p) ? wvd::contracts::RecognitionOutcome::Hit : wvd::contracts::RecognitionOutcome::NoHit;
        result.action_eligible = true; result.box = wvd::contracts::Box{360, 1190, 180, 70};
        result.center = wvd::contracts::Point{450, 1225}; return result;
    }
    wvd::runtime::Submission submit(const wvd::contracts::Command &command,
        const wvd::contracts::Observation &scene, const wvd::contracts::Observation &target,
        wvd::contracts::Box, const std::string &path) override {
        if (scene.basis != target.basis) throw std::runtime_error("SHARED_ACTION_TEST_MIXED_FRAME");
        commands.push_back({{"path", path}, {"frame_id", scene.basis.frame_id},
            {"x", command.x}, {"y", command.y}, {"action_epoch", epoch}});
        ++epoch;
        if (surface.download) { surface.download = false; surface.arrived = true; }
        return {wvd::runtime::SubmissionState::Accepted, epoch, Clock::now(), {}};
    }
    wvd::runtime::OperationResult operate(const std::string &binding, const J &parameters,
        const std::optional<wvd::contracts::FrameEnvelope> &,
        const std::optional<wvd::contracts::Observation> &, const std::string &) override {
        if (binding == "BusinessPredicate")
            return {wvd::games::business_condition(business_summary(), parameters)
                ? wvd::runtime::OperationState::Done : wvd::runtime::OperationState::Waiting};
        return {wvd::runtime::OperationState::Done};
    }
    bool cancelled() const override { return false; }
    J business_summary() const {
        return gold_case ? J{{"gold_income",{{"pending",false},{"active",true},{"phase",0},{"unit_matches",true}}}}
                         : J::object();
    }
};

inline bool terminal(wvd::runtime::TickState state) {
    return state != wvd::runtime::TickState::Progress && state != wvd::runtime::TickState::Waiting;
}
inline std::string state_name(wvd::runtime::TickState state) {
    switch (state) {
    case wvd::runtime::TickState::Progress: return "Progress";
    case wvd::runtime::TickState::Waiting: return "Waiting";
    case wvd::runtime::TickState::Completed: return "Completed";
    case wvd::runtime::TickState::Failed: return "Failed";
    case wvd::runtime::TickState::BusinessFailed: return "BusinessFailed";
    case wvd::runtime::TickState::ExternalBlocked: return "ExternalBlocked";
    case wvd::runtime::TickState::Cancelled: return "Cancelled";
    }
    return "Unknown";
}
inline Graph nested_policy(bool locally_allowed, bool flattened = false, bool after_child = false) {
    const auto prompt = C::any({vision::download_button_zh_hant(), vision::download_button_en()});
    // Use the actual travel factory. The driver waits for the compiled Task
    // definition on the actual call stack before exposing the download page.
    // A parent's own active scope retains its separate allowing policy.
    const auto child = world({"City_RoyalCityLuknalia", {}, {550, 1}},
        navigation::WorldArrival::City, locally_allowed);
    C parent("audit.download_policy.parent", 10s);
    // Match the actual recovery wrapper: parent events must remain active
    // after Entry has advanced to Call or into an appended business body.
    parent.check_policy("business", J::array(), 1000, 1000, false);
    const J next = after_child ? J{"ParentWait"} : J{"Terminal"};
    if (flattened) {
        const auto task = parent.append("Task", child, next);
        parent.route("Entry", {task});
    } else {
        parent.route("Entry", {"Call"});
        const auto task = parent.define_child("Task", child);
        parent.call_child("Call", task, next);
    }
    if (after_child) {
        parent.route("ParentWait", {"ParentDone"});
        parent.observe("ParentDone", C::all({vision::royal_city(), C::absent(prompt)}), {"Terminal"});
    }
    const auto allowing = parent.define_child("ParentDownload", recovery::handle_download_prompt(true));
    parent.event_scope("Entry", J::array({J{{"id", "wvd-download"}, {"class", "exception"},
        {"priority", 900}, {"source_node", "Entry"}, {"entry", allowing}, {"detect", prompt},
        {"resume", {{"mode", "reobserve"}}}}}));
    return parent.finish();
}
inline Graph nested_gold_policy(bool allowed) {
    C parent("audit.gold_download.parent",10s);
    parent.check_policy("business",J::array(),1000,1000,false);
    parent.route("Entry",{"Call"});
    const auto task = parent.define_child("Task",tasks::gold_income_cycle({"7000G","quest",J::object()},allowed));
    parent.call_child("Call",task,{"Terminal"});
    const auto download = parent.define_child("ParentDownload",recovery::handle_download_prompt(true));
    parent.event_scope("Entry",J::array({J{{"id","wvd-download"},{"class","exception"},{"priority",900},
        {"source_node","Entry"},{"entry",download},
        {"detect",C::any({vision::download_button_en(),vision::download_button_zh_hant()})},
        {"resume",{{"mode","reobserve"}}}}}));
    return parent.finish();
}
inline J default_scope_contracts() {
#ifdef WVD_SHARED_ACTIONS_LEGACY_BASELINE
    return {{"supported", false}, {"reason", "API did not exist in V1"}};
#else
    C graph("audit.default_body_scope", 10s);
    const auto prompt = C::any({vision::download_button_zh_hant(), vision::download_button_en()});
    graph.route("Entry", {"Explicit"});
    graph.route("Explicit", {"Disabled"});
    graph.route("Disabled", {"Work"});
    graph.click("Work", vision::download_button_en(), vision::download_button_en(), C::absent(prompt), {"Call"});
    C child("audit.default_scope_child", 5s);
    child.route("Entry", {"Terminal", "Yield"});
    child.handoff("Yield", "extra");
    const auto child_entry = graph.define_child("Child", child.finish());
    graph.call_child("Call", child_entry, {"Terminal"}, {{"extra", {"HandoffReturn"}}});
    graph.route("HandoffReturn", {"Terminal"});
    const auto deny = graph.define_child("Deny", recovery::handle_download_prompt(false));
    J rule{{"id", "wvd-download"}, {"class", "exception"}, {"priority", 900},
        {"source_node", "Entry"}, {"entry", deny}, {"detect", prompt}, {"resume", {{"mode", "reobserve"}}}};
    auto narrower = rule; narrower["source_node"] = "Explicit"; narrower["priority"] = 700;
    auto other = rule; other["id"] = "retained-event"; other["source_node"] = "Explicit"; other["priority"] = 50;
    const J explicit_scope{{"rules", J::array({narrower, other})}, {"disabled", {"retained-disabled"}}};
    other["source_node"] = "Disabled";
    const J disabled_scope{{"rules", J::array({other})}, {"disabled", {"wvd-download", "retained-disabled"}}};
    graph.event_scope("Explicit", explicit_scope); graph.event_scope("Disabled", disabled_scope);
    graph.default_body_event(rule);
    const auto compiled = graph.finish();
    const auto program = tasks::compile_native_program(compiled, J::object(), "default-scope-contract");
    const auto &root = program.definitions.at(program.root_definition);
    const auto &work = root.steps.at("Work"), &await = root.steps.at("Work@await");
    const auto has_rule = [](const auto &step) {
        return std::any_of(step.event_policy.begin(), step.event_policy.end(),
            [](const auto &event) { return event.id == "wvd-download"; });
    };
    J checks{{"explicit_scope_preserved", compiled.event_scopes.at("Explicit") == explicit_scope},
        {"disabled_scope_preserved", compiled.event_scopes.at("Disabled") == disabled_scope},
        {"call_site_covered", has_rule(root.steps.at("Call"))},
        {"handoff_return_covered", has_rule(root.steps.at("HandoffReturn"))},
        {"child_definition_not_overwritten", !has_rule(program.definitions.at(child_entry).steps.at(child_entry))},
        {"input_and_await_covered", has_rule(work) && has_rule(await) &&
            work.event_policy.front().handler_definition == await.event_policy.front().handler_definition},
        {"shared_failure_disabled", root.steps.at("RecoveryRequired").disabled_events.contains("wvd-download") &&
            program.definitions.at(deny).steps.at("RecoveryRequired").disabled_events.contains("wvd-download")},
        {"shared_failure_not_handler_backedge", !has_rule(root.steps.at("RecoveryRequired")) &&
            !has_rule(program.definitions.at(deny).steps.at("RecoveryRequired"))},
        {"source_node_rewritten", compiled.event_scopes.at("Work").front().at("source_node") == "Work"},
        {"failure_reason_preserved", compiled.nodes.at("RecoveryRequired").at("operation_args").at("reason") ==
            "audit.default_body_scope.budget_exhausted"}};
    // Restore the unsafe edge in a test-only native copy: the real validator
    // must reject this exact shared-failure -> own-handler recursion.
    auto unsafe = program;
    auto &shared = unsafe.definitions.at(deny).steps.at("RecoveryRequired");
    shared.disabled_events.erase("wvd-download"); shared.event_policy.push_back(work.event_policy.front());
    bool cycle_rejected = false;
    try { unsafe.validate(); }
    catch (const std::exception &error) { cycle_rejected = std::string(error.what()) == "FLOW_CALL_CYCLE"; }
    checks["restored_handler_backedge_rejected"] = cycle_rejected;
    bool passed = true;
    for (const auto &value : checks) passed &= value.get<bool>();
    return {{"pass", passed}, {"checks", checks}, {"definitions", program.definitions.size()},
        {"root_steps", root.steps.size()}, {"revision", program.revision}};
#endif
}

inline J responsibility_contracts(const J &profile, const J &catalogue, const std::string &locale,
                                  const tasks::PublicFlowLibrary &library) {
    const tasks::PublicStepScope public_steps([&](const std::string &id, const J &arguments) {
        return library.compile_step(id,arguments,locale);
    });
    const auto duty_child = wvd::games::supply::recover_in_dungeon();
    C caller("audit.rescue_duties", 10s);
    caller.route("Entry", {"Heal"});
    const auto heal = caller.define_child("Heal", duty_child);
    caller.call_child("Heal", heal, {"Terminal"}, {{"encounter",{"Terminal"}}, {"blocked",{"Terminal"}}});
    auto wrapped = recovery::with_boot_recovery(caller.finish(), true);
    tasks::localize_task_assets(wrapped, catalogue, locale);
    const auto native = tasks::compile_native_program(wrapped, J::object(), "duties");
    const auto audit = tasks::audit_event_responsibilities(wrapped, native);
    J checks{{"complete_product_duties", audit.at("complete")}};
    auto broken = wrapped;
    auto &inherit = broken.definition_checks.at("Task_Heal_Entry").at("inherit");
    inherit.erase(std::remove(inherit.begin(), inherit.end(), J("wvd-party-death")), inherit.end());
    bool rejected = false;
    try { (void)tasks::compile_native_program(broken, J::object(), "broken-duties"); }
    catch (const std::exception &error) {
        const std::string reason = error.what();
        rejected = reason.starts_with("NATIVE_EVENT_RESPONSIBILITY_MISSING:") &&
            reason.find("Task_Heal") != std::string::npos && reason.find("wvd-party-death") != std::string::npos;
    }
    checks["cut_inner_edge_rejected"] = rejected;
    auto late_disabled = native;
    late_disabled.definitions.at("Task_Heal_Entry").steps.at("Task_Heal_Begin").disabled_events.insert("wvd-party-death");
    bool late_rejected = false;
    try { (void)tasks::audit_event_responsibilities(wrapped,late_disabled); }
    catch (const std::exception &error) {
        late_rejected = std::string(error.what()).find("Task_Heal_Begin:wvd-party-death") != std::string::npos;
    }
    checks["later_body_disable_rejected"] = late_rejected;
    const auto standalone = tasks::compile_native_program(duty_child, J::object(), "standalone");
    checks["standalone_not_false_pass"] = !tasks::audit_event_responsibilities(duty_child, standalone).at("complete").get<bool>();
    C inline_caller("audit.inline_rescue_duties", 10s);
    const auto inline_heal = inline_caller.append("Heal", duty_child, {"Terminal"});
    inline_caller.route("Entry", {inline_heal});
    auto inline_wrapped = recovery::with_boot_recovery(inline_caller.finish(), true);
    tasks::localize_task_assets(inline_wrapped, catalogue, locale);
    const auto inline_native = tasks::compile_native_program(inline_wrapped, J::object(), "inline-duties");
    const auto inline_audit = tasks::audit_event_responsibilities(inline_wrapped, inline_native);
    checks["append_is_parent_owner"] = std::any_of(inline_audit.at("inline_owners").begin(),
        inline_audit.at("inline_owners").end(), [](const J &row) {
            return row.at("entry") == "Task_Heal_Entry" && row.at("owner") == "Entry" && row.at("inline") == true;
        });
    C outer("audit.nested_provider",180s);
    outer.route("Entry",{"Middle"});
    const auto middle = outer.define_child("Middle",wrapped);
    outer.call_child("Middle",middle,{"Terminal"});
    const auto nested = outer.finish();
    const auto nested_program = tasks::compile_native_program(nested,J::object(),"nested-provider");
    checks["nested_provider_in_scope"] = tasks::audit_event_responsibilities(nested,nested_program).at("complete");
    auto giant = recovery::with_boot_recovery(tasks::giant_iteration({"gaintKiller", "quest", J::object()}, profile, {}, true), true);
    tasks::localize_task_assets(giant, catalogue, locale);
    const auto giant_program = tasks::compile_native_program(giant, J::object(), "giant-duties");
    auto giant_audit = tasks::audit_event_responsibilities(giant, giant_program);
    checks["giant_root_duties"] = giant_audit.at("complete");
    bool pass = true;
    for (const auto &value : checks) pass &= value.get<bool>();
    return {{"pass", pass}, {"checks", checks}, {"call_audit", audit}, {"inline_audit", inline_audit},
        {"giant_audit", std::move(giant_audit)}};
}

struct Running {
    std::string name, locale;
    wvd::workflow::FlowProgram program;
    Ports ports;
    wvd::runtime::FlowExecutor executor;
    wvd::runtime::TickResult last{wvd::runtime::TickState::Progress};
    unsigned expected_inputs{};
    bool expected_complete{}, expected_denial{}, stop_after_first{};
    bool inject_after_child{}, inject_in_flattened{}, inject_after_return{}, prompt_injected{};
    std::optional<Clock::time_point> child_entered_at;
    J injection_progress = nullptr, event_trace = J::array();
    std::string last_event;
    Running(std::string id, std::string lang, const wvd::workflow::FlowProgram &compiled, Surface surface, J reveal,
            unsigned inputs, bool complete, bool denial, bool first)
        : name(std::move(id)), locale(std::move(lang)),
          program(compiled),
          ports(locale, surface, std::move(reveal)), executor(program, ports, 10s),
          expected_inputs(inputs), expected_complete(complete), expected_denial(denial), stop_after_first(first) {}
};

int run(const std::filesystem::path &next_root, const std::filesystem::path &output,
        bool expect_fixed) {
    if (std::filesystem::exists(output)) throw std::runtime_error("SHARED_ACTION_TEST_NEW_OUTPUT_REQUIRED");
    std::filesystem::create_directories(output);
    const auto catalogue = read_json(next_root / "resources/authoring/semantic-assets.json");
    J documents = J::object();
    for (const auto &doc : read_json(next_root / "resources/authoring/public-flows.json"))
        documents[doc.at("flow").at("id").get<std::string>()] = doc;
    const tasks::PublicFlowLibrary library(documents, catalogue);
    J profile = J::object();
    const auto default_schema = read_json(next_root / "packs/wvd/parameters/legacy-config-fields.json");
    for (const auto &field : default_schema.at("fields"))
        profile[field.at("name").get<std::string>()] = field.at("default");
    const J skill{{"role_var", ""}, {"skill_var", "Top-Left Skill"}, {"skill_lvl", 1}, {"target_var", "左上角色"}};
    profile["STRATEGY"] = J::array({{{"group_name", "shared-action-test"}, {"skill_settings", J::array({skill})}}});
    profile["DEFAULT_OVERALL_STRATEGY"] = "shared-action-test";
    profile["TASK_SPECIFIC_CONFIG"] = false; profile["TASK_POINT_STRATEGY"] = J::object();
    const wvd::games::WorldDestination destination{"City_RoyalCityLuknalia", {}, {550, 1}};
    const auto plan = wvd::games::WvdTaskPlan::parse({"shared_action_fixture", "quest",
        J{{"_EOT", J::array({J::array({"press", "intoWorldMap", J::array({"City_RoyalCityLuknalia", nullptr}), 1})})},
          {"_RTT", J::array({"City_RoyalCityLuknalia"})},
          {"_TARGETINFOLIST", J::array({J::array({"position", "左上", J::array({560, 982})})})}}});
    J snapshots = J::object(), program_summaries = J::object(), scope_coverage = J::object(), failures = J::array(), graph_errors = J::array();
    std::vector<std::unique_ptr<Running>> running;
    std::vector<std::function<std::unique_ptr<Running>()>> prepared_cases;
    const auto check = [&](bool okay, const std::string &name) { if (!okay) failures.push_back(name); };
    for (const auto *locale_value : {"en", "zh-Hant"}) {
        const std::string locale(locale_value);
        const tasks::PublicStepScope scope([&](const std::string &id, const J &args) {
            return library.compile_step(id, args, locale);
        });
        std::map<std::string, Graph> graphs;
        std::map<std::string, wvd::workflow::FlowProgram> programs;
        const auto emit = [&](const std::string &id, const std::function<Graph()> &factory) {
            try {
                auto graph = factory(); tasks::localize_task_assets(graph, catalogue, locale); graph.validate();
                auto native = tasks::compile_native_program(graph, J::object(), "shared-actions-contract");
                std::size_t step_count{}; J events = J::object();
                for (const auto &[definition_id, definition] : native.definitions) {
                    step_count += definition.steps.size();
                    const auto record = [&](const auto &event, const std::string &step) {
                        if (event.id != "wvd-download") return;
                        events[definition_id][step] = {{"id", event.id},
                            {"handler_definition", event.handler_definition}, {"priority", event.priority}};
                    };
                    for (const auto &event : definition.events) record(event, "@definition");
                    for (const auto &[step_id, step] : definition.steps) {
                        for (const auto &event : step.event_policy) record(event, step_id);
                        if (step.disabled_events.contains("wvd-download"))
                            events[definition_id][step_id + "@disabled"] = true;
                    }
                }
                program_summaries[locale][id] = {{"revision", native.revision},
                    {"root_definition", native.root_definition}, {"definitions", native.definitions.size()},
                    {"steps", step_count}, {"download_event_owners", std::move(events)}};
                programs.emplace(id, std::move(native));
                snapshots[locale][id] = snapshot(graph); graphs.emplace(id, std::move(graph));
            } catch (const std::exception &error) {
                graph_errors.push_back({{"locale", locale}, {"graph", id}, {"error", error.what()}});
                if (!(id == "dungeon_deny" && !expect_fixed && std::string(error.what()) == "COMPILE_EVENT_SCOPE_INVALID"))
                    failures.push_back(locale + "/" + id + ":" + error.what());
            }
        };
        emit("boot", [] { return recovery::wait_boot_ready(true); });
        emit("common", [] { return recovery::clear_common_screens(true); });
        emit("download_allow", [] { return recovery::handle_download_prompt(true); });
        emit("download_deny", [] { return recovery::handle_download_prompt(false); });
        emit("inn", [] { return wvd::games::supply::rest_at_inn(false, true); });
        emit("inn_standalone", [] { return wvd::games::supply::rest_at_inn(false, false); });
        emit("single_auto", [] { return wvd::games::combat::single_actor_auto(); });
        emit("enable_auto", [] { return wvd::games::combat::enable_auto(); });
        emit("turn", [&] { return wvd::games::combat::take_turn(profile, {}); });
        emit("time_leap_allow", [] { return navigation::time_leap_without_causality("BeautifulOre", "cursedwheel_dhi", true); });
        emit("time_leap_deny", [] { return navigation::time_leap_without_causality("BeautifulOre", "cursedwheel_dhi", false); });
        emit("world_allow", [&] { return world(destination, navigation::WorldArrival::City, true); });
        emit("world_deny", [&] { return world(destination, navigation::WorldArrival::City, false); });
        emit("world_dungeon_allow", [&] { return world(destination, navigation::WorldArrival::DungeonEntrance, true); });
        emit("world_dungeon_deny", [&] { return world(destination, navigation::WorldArrival::DungeonEntrance, false); });
        emit("city_allow", [&] { return city_to_city(destination, true); });
        emit("city_deny", [&] { return city_to_city(destination, false); });
        emit("entry_allow", [&] { return entry(plan, true); });
        emit("entry_deny", [&] { return entry(plan, false); });
        emit("departure_allow", [&] { return departure(plan, profile, true); });
        emit("departure_deny", [&] { return departure(plan, profile, false); });
        emit("dungeon_allow", [&] { return tasks::traverse_dungeon(plan, profile, {}, true); });
        emit("dungeon_deny", [&] { return tasks::traverse_dungeon(plan, profile, {}, false); });
        emit("gold_allow", [&] { return tasks::gold_income_cycle({"7000G", "quest", J::object()}, true); });
        emit("gold_deny", [&] { return tasks::gold_income_cycle({"7000G", "quest", J::object()}, false); });
        emit("nested_gold_allow", [] { return nested_gold_policy(true); });
        emit("nested_gold_deny", [] { return nested_gold_policy(false); });
        emit("leave_board", [&] { return tasks::leave_bounty_board(library, locale); });
        emit("harken_exit", [] { return navigation::leave_harken(); });
        emit("public_wheel", [&] { return library.compile_step("wheel-open", J::object(), locale); });
        if (locale == "zh-Hant")
            emit("public_guild", [&] { return library.compile_step("guild-open-bounties", J::object(), locale); });
        emit("wrapped_handlers", [] {
            C graph("audit.shared-handler-caller", 10s); graph.route("Entry", {"Terminal"});
            return recovery::with_boot_recovery(graph.finish(), true);
        });
        emit("nested_deny", [] { return nested_policy(false); });
        emit("nested_allow", [] { return nested_policy(true); });
        emit("nested_flatten_deny", [] { return nested_policy(false, true); });
        emit("nested_flatten_allow", [] { return nested_policy(true, true); });
        emit("parent_after_child", [] { return nested_policy(false, false, true); });
        emit("parent_after_flatten", [] { return nested_policy(false, true, true); });
        if (expect_fixed) {
            for (const auto *id : {"world_deny", "world_dungeon_deny", "city_deny", "entry_deny", "departure_deny", "dungeon_deny", "gold_deny", "time_leap_deny"}) {
                if (!graphs.contains(id)) continue;
                unsigned download_inputs{};
                for (const auto &[name, node] : graphs.at(id).nodes.items()) {
                    if (node.value("binding", "") != "Input") continue;
                    const auto &p = node.at("operation_args").at("target_recognition").at("parameters");
                    if (p.value("image", "").starts_with("startdownload")) ++download_inputs;
                }
                check(download_inputs == 0, locale + "/" + id + ":DOWNLOAD_INPUT_REMAINS");
                const auto &native = programs.at(id);
                const auto &root = native.definitions.at(native.root_definition);
                J unprotected = J::array(), handler_inputs = J::array(); unsigned covered{};
                const auto is_download = [](const auto &event) { return event.id == "wvd-download"; };
                const bool definition_rule = std::any_of(root.events.begin(), root.events.end(), is_download);
                for (const auto &[step_id, step] : root.steps) {
                    if (definition_rule || step.disabled_events.contains("wvd-download") ||
                        std::any_of(step.event_policy.begin(), step.event_policy.end(), is_download)) ++covered;
                    else unprotected.push_back(step_id);
                }
                for (const auto &[definition_id, definition] : native.definitions) {
                    const auto verify_handler = [&](const auto &event) {
                        if (event.id != "wvd-download" || event.handler_definition.empty()) return;
                        for (const auto &[step_id, step] : native.definitions.at(event.handler_definition).steps)
                            if (std::holds_alternative<wvd::workflow::Input>(step.data))
                                handler_inputs.push_back(event.handler_definition + "/" + step_id);
                    };
                    for (const auto &event : definition.events) verify_handler(event);
                    for (const auto &[step_id, step] : definition.steps)
                        for (const auto &event : step.event_policy) verify_handler(event);
                }
                check(unprotected.empty(), locale + "/" + id + ":BODY_SCOPE_GAP");
                check(handler_inputs.empty(), locale + "/" + id + ":DENIAL_HANDLER_HAS_INPUT");
                scope_coverage[locale][id] = {{"root_steps", root.steps.size()}, {"covered_root_steps", covered},
                    {"unprotected_root_steps", unprotected}, {"denial_handler_inputs", handler_inputs},
                    {"download_inputs", download_inputs}};
            }
        }
        const auto reveal = library.resource_condition("guild.bounty.reveal.close", "zh-Hant", wvd::authoring::ResourceUse::Position);
        const auto add = [&](const std::string &id, const std::string &graph, Surface surface,
                             unsigned inputs, bool complete, bool denial, bool first = false) {
            if (!graphs.contains(graph)) { failures.push_back(locale + "/" + id + ":GRAPH_MISSING"); return; }
            prepared_cases.push_back([id, locale, program = programs.at(graph), surface,
                                       reveal, inputs, complete, denial, first] {
                auto test = std::make_unique<Running>(id, locale, program, surface, reveal, inputs, complete, denial, first);
                test->ports.gold_case = id.starts_with("gold_parent_");
                test->inject_after_child = id == "parent_allow_child_deny" || id == "parent_allow_child_allow" ||
                    id == "parent_allow_flatten_deny" || id == "parent_allow_flatten_allow" ||
                    id == "gold_parent_allow_child_deny" || id == "gold_parent_allow_child_allow";
                test->inject_in_flattened = id == "parent_allow_flatten_deny" || id == "parent_allow_flatten_allow";
                test->inject_after_return = id == "parent_allowed_after_child" || id == "parent_allowed_after_flatten";
                return test;
            });
        };
        add("world_allowed_download", "world_allow", {true, false, false, false, false}, 1, true, false);
        add("world_forbidden_download", "world_deny", {true, false, false, false, false}, expect_fixed ? 0U : 1U, !expect_fixed, expect_fixed);
        add("world_forbidden_no_prompt", "world_deny", {false, true, false, false, false}, 0, true, false);
        add("gold_parent_allow_child_deny","nested_gold_deny",{},0,false,true);
        add("gold_parent_allow_child_allow","nested_gold_allow",{},1,false,false,true);
        add("parent_allow_child_deny", "nested_deny", {}, expect_fixed ? 0U : 1U, !expect_fixed, expect_fixed);
        add("parent_allow_child_allow", "nested_allow", {}, 1, true, false);
        add("parent_allow_child_deny_no_prompt", "nested_deny", {false, true, false, false, false}, 0, true, false);
        add("parent_allow_flatten_deny", "nested_flatten_deny", {}, expect_fixed ? 0U : 1U, !expect_fixed, expect_fixed);
        add("parent_allow_flatten_allow", "nested_flatten_allow", {}, 1, true, false);
        add("parent_allow_flatten_deny_no_prompt", "nested_flatten_deny", {false, true, false, false, false}, 0, true, false);
        add("parent_allowed_after_child", "parent_after_child", {false, true, false, false, false}, 1, true, false);
        add("parent_allowed_after_flatten", "parent_after_flatten", {false, true, false, false, false}, 1, true, false);
        add("single_auto_x_only", "single_auto", {false, false, false, false, true}, 0, false, false);
        add("enable_auto_x_only", "enable_auto", {false, false, false, false, true}, 0, false, false);
        if (locale == "zh-Hant") {
            add("leave_board_combat_close", "leave_board", {false, false, true, true, false}, expect_fixed ? 0U : 1U, false, false, true);
            add("leave_board_valid_close", "leave_board", {false, false, false, true, false}, 1, false, false, true);
            add("public_guild_combat_close", "public_guild", {false, false, true, true, false}, expect_fixed ? 0U : 1U, false, false, true);
            add("public_guild_valid_close", "public_guild", {false, false, false, true, false}, 1, false, false, true);
        }
    }
    write_json(output / "factory-snapshots.json", snapshots);
    write_json(output / "program-summaries.json", program_summaries);
    write_json(output / "scope-coverage.json", scope_coverage);
    const auto default_scope_checks = default_scope_contracts();
    write_json(output / "default-scope-contracts.json", default_scope_checks);
    if (expect_fixed) check(default_scope_checks.value("pass", false), "DEFAULT_SCOPE_CONTRACT");
    for (const auto *locale : {"en", "zh-Hant"}) {
        const auto duties = responsibility_contracts(profile, catalogue, locale, library);
        write_json(output / (std::string("responsibility-") + locale + ".json"), duties);
        check(duties.at("pass").get<bool>(), std::string(locale) + "/EVENT_RESPONSIBILITIES");
    }
    // No executor/invocation clock starts during factory compilation or the
    // large evidence export. All runtime cases begin only after that work.
    for (const auto &prepare : prepared_cases) running.push_back(prepare());
    const auto deadline = Clock::now() + 4000ms;
    do {
        bool live = false;
        for (auto &test : running) {
            if (terminal(test->last.state) || (test->stop_after_first && !test->ports.commands.empty())) continue;
            if (test->inject_after_child && !test->prompt_injected) {
                const auto progress = test->executor.progress_snapshot();
                const auto &stack = progress.at("call_stack");
                const bool child_scope = test->inject_in_flattened
                    ? stack.size() == 1 && stack.back().at("definition") == "Entry"
                    : stack.size() >= 2 && stack.back().at("definition") == "Task_Entry";
                const auto waiting_step = test->ports.gold_case ? "Task_Phase0" : "Task_Locate";
                if (child_scope && stack.back().at("node_id") == waiting_step) {
                    if (!test->child_entered_at) test->child_entered_at = Clock::now();
                    if (Clock::now() - *test->child_entered_at >= 1100ms) {
                        test->injection_progress = progress;
                        test->ports.surface.download = true;
                        test->prompt_injected = true;
                    }
                }
            }
            if (test->inject_after_return && !test->prompt_injected) {
                const auto progress = test->executor.progress_snapshot();
                const auto &stack = progress.at("call_stack");
                if (stack.size() == 1 && stack.back().at("node_id") == "ParentWait") {
                    test->injection_progress = progress;
                    test->ports.surface.download = true; test->prompt_injected = true;
                }
            }
            test->last = test->executor.tick(); live = true;
            if (test->inject_after_child || test->inject_after_return) {
                const auto progress = test->executor.progress_snapshot();
                if (!progress.at("active_event").is_null()) {
                    const auto key = progress.at("active_event").dump();
                    if (test->last_event != key) {
                        test->event_trace.push_back({{"active_event", progress.at("active_event")},
                            {"call_stack", progress.at("call_stack")}});
                        test->last_event = key;
                    }
                }
            }
        }
        if (!live) break;
        std::this_thread::sleep_for(2ms);
    } while (Clock::now() < deadline);
    J results = J::array();
    for (const auto &test : running) {
        bool okay = test->ports.commands.size() == test->expected_inputs;
        if (test->expected_complete) okay &= test->last.state == wvd::runtime::TickState::Completed;
        if (test->expected_denial) okay &= test->last.code.find("boot.download_permission_missing") != std::string::npos;
        if (test->inject_after_child || test->inject_after_return) okay &= test->prompt_injected;
        const auto handled_by = [&](const std::string &definition) {
            return std::any_of(test->event_trace.begin(), test->event_trace.end(), [&](const auto &entry) {
                return !entry.at("call_stack").empty() && entry.at("call_stack").back().at("definition") == definition;
            });
        };
        if (expect_fixed && test->inject_after_child && test->expected_denial)
            okay &= handled_by("Task_DownloadPermission_Entry") && !handled_by("ParentDownload_Entry");
        if (test->inject_after_return) okay &= handled_by("ParentDownload_Entry");
        check(okay, test->locale + "/" + test->name);
        results.push_back({{"locale", test->locale}, {"case", test->name}, {"pass", okay},
            {"state", state_name(test->last.state)}, {"code", test->last.code},
            {"inputs", test->ports.commands}, {"expected_input_count", test->expected_inputs},
            {"expected_complete", test->expected_complete}, {"expected_denial", test->expected_denial},
            {"prompt_injected_in_child", test->inject_after_child && test->prompt_injected},
            {"prompt_injected_after_return", test->inject_after_return && test->prompt_injected},
            {"injection_progress", test->injection_progress},
            {"event_trace", test->event_trace},
            {"pending_input", test->executor.has_unresolved_input()},
            {"progress", test->executor.progress_snapshot()}});
    }
    write_json(output / "runtime-results.json", results);
    write_json(output / "summary.json", {{"pass", failures.empty()}, {"expect_fixed", expect_fixed},
        {"factory_snapshots", snapshots.at("en").size() + snapshots.at("zh-Hant").size()},
        {"runtime_cases", results.size()}, {"graph_errors", graph_errors}, {"failures", failures},
        {"default_scope_contract", default_scope_checks},
        {"not_executed", {"Service", "NativeInputGate", "Windows product", "device", "game"}},
        {"fixture_profile", profile}, {"fixture_plan", plan.inspect()}});
    if (!failures.empty()) { std::cerr << failures.dump() << '\n'; return 1; }
    std::cout << "PASS shared page actions, download-policy call scopes and guarded guild close; "
              << results.size() << " controlled runtime cases\n";
    return 0;
}
} // namespace shared_page_actions_contract_test
