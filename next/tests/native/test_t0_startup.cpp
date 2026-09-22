#include "app/application.hpp"
#include "devices/android_probe.hpp"
#include "devices/android_viewport.hpp"
#include "devices/lifecycle.hpp"
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace wvd::app {
struct ApplicationAssemblyTestAccess {
    static runtime::RunDefinition task(Application &application, bool vpn,
                                       const std::string &request_id) {
        auto stored = application.profile_store_->load();
        stored["values"]["AUTO_START_CLASH"] = vpn;
        if (stored.contains("default_values"))
            stored["default_values"]["AUTO_START_CLASH"] = vpn;
        devices::LifecycleTarget target{"t0-device", "t0-instance",
                                        "jp.co.drecom.wizardry.daphne",
                                        "com.github.metacubex.clash.meta", vpn};
        return application.assemble_task(
            {{"request_id", request_id}, {"task_id", "Scorpionesses"}}, stored, target);
    }
    static runtime::RunDefinition workflow(Application &application, bool vpn,
                                           bool selected_only,
                                           const std::string &request_id) {
        auto stored = application.profile_store_->load();
        stored["values"]["AUTO_START_CLASH"] = vpn;
        if (stored.contains("default_values"))
            stored["default_values"]["AUTO_START_CLASH"] = vpn;
        const std::string revision(64, 'a');
        nlohmann::json document{
            {"schema", 1},
            {"revision", revision},
            {"flow", {{"id", "t0_author"}, {"name", "T0 author"}, {"description", ""}}},
            {"entry", "combat"},
            {"nodes", nlohmann::json::array({
                {{"id", "combat"}, {"type", "business"}, {"name", "Combat"},
                 {"parameters", {{"binding", "combat"}}}},
                {{"id", "wait"}, {"type", "wait"}, {"name", "Wait"},
                 {"parameters", {{"duration_ms", 50}}}},
                {{"id", "done"}, {"type", "end"}, {"name", "Done"},
                 {"parameters", {{"outcome", "success"}}}},
                {{"id", "failed"}, {"type", "end"}, {"name", "Failed"},
                 {"parameters", {{"outcome", "failure"}, {"reason", "t0.author.failed"}}}}
            })},
            {"edges", nlohmann::json::array({
                {{"id", "e1"}, {"from", "combat"}, {"to", "wait"},
                 {"outcome", "success"}, {"order", 0}},
                {{"id", "e2"}, {"from", "combat"}, {"to", "failed"},
                 {"outcome", "failure"}, {"order", 0}},
                {{"id", "e3"}, {"from", "wait"}, {"to", "done"},
                 {"outcome", "success"}, {"order", 0}}
            })},
            {"layout", {{"nodes", nlohmann::json::array({
                 {{"node_id", "combat"}, {"x", 0}, {"y", 0}},
                 {{"node_id", "wait"}, {"x", 200}, {"y", 0}},
                 {{"node_id", "done"}, {"x", 400}, {"y", -100}},
                 {{"node_id", "failed"}, {"x", 400}, {"y", 100}}
             })}, {"viewport", {{"x", 0}, {"y", 0}, {"zoom", 1.0}}}}},
            {"execution", {{"time_limit_ms", 300000}}}
        };
        nlohmann::json request{{"request_id", request_id}, {"revision", revision},
                               {"mode", selected_only ? "selected_node" : "workflow"}};
        if (selected_only)
            request["node_id"] = "combat";
        devices::LifecycleTarget target{"t0-device", "t0-instance",
                                        "jp.co.drecom.wizardry.daphne",
                                        "com.github.metacubex.clash.meta", vpn};
        return application.assemble_workflow(request, stored, std::move(document), target);
    }
};
} // namespace wvd::app

namespace {
using J = nlohmann::json;
using O = wvd::devices::LifecycleOperation;
int assertions{};

void expect(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
    ++assertions;
}

void expect_throw(const std::function<void()> &operation, const char *message) {
    bool threw = false;
    try {
        operation();
    } catch (const std::runtime_error &) {
        threw = true;
    }
    expect(threw, message);
}

J read_json(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("T0_PIPELINE_MISSING");
    return J::parse(input);
}

void verify_android_probe() {
    using namespace wvd::devices::android;
    expect(process({1, ""}) == Presence::Absent, "pid absent is normal");
    expect(process({0, "12 29\n"}) == Presence::Present, "multiple pids");
    expect_throw([] { process({127, "pidof: not found"}); }, "missing command is error");
    expect_throw([] { process({1, "Permission denied"}); }, "permission is error");
    expect_throw([] { process({0, "garbage 42"}); }, "digits in errors are not pids");
    expect_throw([] { process({0, "0"}); }, "pid zero rejected");
    const auto absent = parse_shell_reply("\nWVD_RC_A_1\n", "WVD_RC_A_");
    expect(absent.exit_code == 1 && absent.output.empty(), "preserve business rc1");
    const auto denied = parse_shell_reply("Permission denied\nWVD_RC_A_13\n", "WVD_RC_A_");
    expect(denied.exit_code == 13 && denied.output == "Permission denied",
           "preserve command error");
    expect_throw([] { parse_shell_reply("transport disconnected", "WVD_RC_A_"); },
                 "truncated transport rejected");
    expect_throw([] { parse_shell_reply("\nWVD_RC_A_0\nWVD_RC_A_0\n", "WVD_RC_A_"); },
                 "duplicate trailer rejected");
    expect_throw([] { parse_shell_reply("\nWVD_RC_A_0junk\n", "WVD_RC_A_"); },
                 "invalid status rejected");
    expect(focus("mCurrentFocus=null").empty(), "transition focus is unknown");
    expect(focus("mCurrentFocus=Window{abc u0 jp.co.game/.Main}") == "jp.co.game",
           "focused package parsed");
    expect(focus("mCurrentFocus=Window{abc u0 jp.co.game/.Main}\n"
                 "mCurrentFocus=Window{def u0 android/.Lock}")
               .empty(),
           "ambiguous focus rejected");
    expect(focus("mFocusedApp=ActivityRecord{jp.co.game/.Main}").empty(),
           "mFocusedApp is not accepted");
    expect(!installed({0, "package:com.clash.extra\n"}, "com.clash"),
           "package prefix rejected");
    expect(installed({0, "package:com.clash\n"}, "com.clash"), "exact package accepted");
    expect(!tun_up({1, "Device \"tun0\" does not exist."}), "missing tunnel is negative");
    expect(tun_up({0, "4: tun0: <POINTOPOINT>\n5: tun1: <POINTOPOINT,UP,LOWER_UP>\n"}),
           "live tunnel accepted");
    expect_throw([] { tun_up({2, "Permission denied"}); }, "tunnel command error preserved");
    expect(!live_vpn({0, "history: Transports: VPN\n"}), "historical VPN rejected");
    expect(live_vpn({0, "NetworkAgentInfo{ type: VPN[], state: CONNECTED/CONNECTED\n"
                        " Transports: VPN\n\n"}),
           "live VPN accepted");
    expect(!live_vpn({0, "NetworkAgentInfo{ type: VPN[], state: DISCONNECTED/DISCONNECTED\n"
                         " Transports: VPN\n\n"}),
           "disconnected VPN rejected");
    const std::string button =
        "<node package=\"com.android.vpndialogs\" enabled=\"true\" clickable=\"true\" "
        "resource-id=\"android:id/button1\" text=\"OK\" bounds=\"[500,900][700,1000]\" />";
    const auto target = unique_ui_target(button, "com.android.vpndialogs", {"OK"}, true);
    expect(target && target->x == 600 && target->y == 950, "exact VPN consent bounds");
    expect(!unique_ui_target(button, "jp.co.game", {"OK"}, true), "wrong package rejected");
    expect(!unique_ui_target(button, "com.android.vpndialogs", {"Cancel"}, true),
           "wrong text rejected");
    const std::string duplicate =
        "<node package=\"com.android.vpndialogs\" enabled=\"true\" clickable=\"true\" "
        "resource-id=\"android:id/button1\" text=\"OK\" bounds=\"[0,0][100,100]\" />";
    expect_throw([&] { unique_ui_target(button + duplicate, "com.android.vpndialogs", {"OK"}, true); },
                 "ambiguous VPN consent rejected");
    const ShellReply clash_main{
        0, "topResumedActivity=ActivityRecord{123 u0 com.github.metacubex.clash.meta/"
           "com.github.kr328.clash.MainActivityAlias t7}\n"};
    const auto clash_portrait = clash_main_start_target(
        clash_main, "com.github.metacubex.clash.meta", 900, 1600);
    expect(clash_portrait && clash_portrait->x == 450 && clash_portrait->y == 243,
           "verified Clash portrait target");
    const auto clash_landscape = clash_main_start_target(
        clash_main, "com.github.metacubex.clash.meta", 1600, 900);
    expect(clash_landscape && clash_landscape->x == 800 && clash_landscape->y == 243,
           "verified Clash landscape target");
    expect(!clash_main_start_target(clash_main, "com.github.metacubex.clash.meta", 1080, 1920),
           "unverified viewport rejected");
    const ShellReply wrong_activity{
        0, "topResumedActivity=ActivityRecord{123 u0 com.github.metacubex.clash.meta/"
           "com.github.kr328.clash.SettingsActivity t7}\n"};
    expect(!clash_main_start_target(
               wrong_activity, "com.github.metacubex.clash.meta", 1600, 900),
           "non-main Clash activity rejected");
    const ShellReply ambiguous_activity{
        0, clash_main.output +
               "topResumedActivity=ActivityRecord{456 u0 com.github.metacubex.clash.meta/"
               "com.github.kr328.clash.MainActivity t8}\n"};
    expect(!clash_main_start_target(
               ambiguous_activity, "com.github.metacubex.clash.meta", 1600, 900),
           "ambiguous resumed activity rejected");
    expect_throw(
        [&] { clash_main_start_target({1, "Permission denied"},
                                      "com.github.metacubex.clash.meta", 1600, 900); },
        "activity query error preserved");
}

void verify_android_viewport() {
    using wvd::devices::android::input_viewport;
    const std::string portrait =
        "  Viewport INTERNAL: displayId=0, orientation=0, logicalFrame=[0, 0, 900, 1600]\n";
    const auto parsed = input_viewport(portrait + portrait);
    expect(parsed && parsed->size == wvd::contracts::Size{900, 1600} &&
               parsed->rotation == 0,
           "identical default viewports are not accepted");
    expect(!input_viewport(
               portrait +
               "  Viewport INTERNAL: displayId=0, orientation=1, logicalFrame=[0, 0, 1600, 900]\n"),
           "conflicting default viewports are accepted");
    expect(!input_viewport(
               "Viewport INTERNAL: displayId=1, orientation=0, logicalFrame=[0, 0, 900, 1600]\n"),
           "non-default display is accepted");
    expect(!input_viewport(
               "Viewport INTERNAL: displayId=0, orientation=0, logicalFrame=[1, 0, 901, 1600]\n"),
           "non-zero viewport origin is accepted");
    expect(!input_viewport("Input Reader State: no viewport records\n"),
           "unknown viewport format is accepted");
}

void verify_initial_plan_contract() {
    auto check = [](bool vpn, unsigned attempt, std::vector<O> operations, bool expected) {
        wvd::devices::LifecyclePlan plan;
        plan.target.vpn_required = vpn;
        plan.attempt = attempt;
        plan.operations = std::move(operations);
        expect(wvd::devices::initial_lifecycle_plan(plan) == expected,
               "initial lifecycle classification mismatch");
    };
    check(false, 1, {O::StartApplication}, true);
    check(true, 1, {O::EnsureVpn, O::StartApplication}, true);
    check(true, 1, {O::EnsureVpn}, true);
    check(false, 1, {O::EnsureVpn}, false);
    check(true, 1, {O::StartApplication}, false);
    check(false, 1, {O::StopApplication, O::StartApplication}, false);
    check(true, 1, {O::RestartInstance, O::EnsureVpn, O::StartApplication}, false);
    check(true, 2, {O::EnsureVpn, O::StartApplication}, false);
    check(false, 1, {}, false);
}

std::size_t verify_definition(const wvd::runtime::RunDefinition &definition,
                              const std::vector<O> &expected_operations,
                              bool verify_bounty_entry = false) {
    expect(definition.initial.lifecycle.has_value(), "initial lifecycle missing");
    expect(definition.initial.lifecycle->operations == expected_operations,
           "initial lifecycle operations mismatch");
    expect(std::find(expected_operations.begin(), expected_operations.end(), O::StopApplication) ==
               expected_operations.end(),
           "normal startup must not stop game");
    expect(std::find(expected_operations.begin(), expected_operations.end(), O::RestartInstance) ==
               expected_operations.end(),
           "normal startup must not restart emulator");
    for (const auto &unit : definition.continuation_units)
        expect(!unit.lifecycle.has_value(), "continuation lifecycle must be empty");

    const auto pipeline = read_json(definition.initial.bundle.root / "pipeline/workflow.json");
    expect(pipeline.is_object(), "published pipeline invalid");
    expect(definition.policy.max_frame_age.count() == 0,
           "business observations still inherit a global input TTL");

    std::size_t guarded = 0, observers = 0;
    for (const auto &[name, node] : pipeline.items()) {
        const auto action = node.value("custom_action", "");
        if (action == "GuardedAction") {
            ++guarded;
            const auto &parameters = node.at("custom_action_param");
            expect(parameters.at("input_contract") == 2,
                   "input node retained the monolithic contract");
            expect(node.at("post_delay") == 0,
                   "input node still sleeps after transition confirmation");
            expect(node.at("next").is_array() && node.at("next").size() == 1,
                   "input node does not have one observation successor");
            const auto observer = node.at("next").front().get<std::string>();
            expect(observer == "__wvd_observe__" + name && pipeline.contains(observer),
                   "input observation node name or edge is invalid");
            const auto &wait = pipeline.at(observer);
            const auto &wait_parameters = wait.at("custom_action_param");
            expect(wait.at("custom_action") == "AwaitTransition" &&
                       wait_parameters.at("input_contract") == 2 &&
                       wait_parameters.at("source_node") == name,
                   "observation node is not bound to its source input");
            expect(wait_parameters.at("postcondition") == parameters.at("postcondition"),
                   "observation condition differs from submitted intent");
            expect(wait_parameters.at("observation_budget_ms").get<std::int64_t>() >
                       wait_parameters.at("initial_delay_ms").get<std::int64_t>(),
                   "observation budget does not include its initial delay");
        } else if (action == "AwaitTransition") {
            ++observers;
        }
    }
    expect(guarded > 0 && guarded == observers,
           "published input and observation node counts differ");
    expect(pipeline.contains("Boot_Entry"), "Boot entry missing from published graph");
    expect(pipeline.contains("Boot_Poll"), "Boot unknown-frame poll missing");
    expect(pipeline.at("Boot_Poll").at("next") == J::array({"Boot_Entry"}),
           "Boot poll does not return to the production entry");
    expect(pipeline.at("Boot_Poll").at("custom_action") == "CancelableWait" &&
               pipeline.at("Boot_Poll").at("custom_action_param").at("duration_ms") == 500,
           "Boot poll is not a bounded cancellable wait");
    expect(pipeline.at("Boot_Entry").at("custom_action") == "BeginObservationPhase" &&
               pipeline.at("Boot_Entry").at("custom_action_param").at("budget_ms") == 120000,
           "Boot phase does not retain one 120 second deadline");
    expect(pipeline.at("Boot_ObservationPhaseEnd").at("custom_action") ==
               "EndObservationPhase",
           "Boot phase does not close before returning to the task");
    const auto &attention =
        pipeline.at("Boot_Attention").at("custom_action_param").at("postcondition");
    expect(attention.dump().find("boot_attention") != std::string::npos &&
               attention.dump().find("boot_post") == std::string::npos,
           "Boot attention still accepts the unchanged prompt as progress");
    const auto download = pipeline.at("Boot_DownloadEn").dump() +
                          pipeline.at("Boot_DownloadZhHant").dump();
    expect(download.find("startdownload") != std::string::npos &&
               download.find("startdownload_zh_hant") != std::string::npos,
           "Boot download does not accept both English and Traditional Chinese prompts");

    if (verify_bounty_entry) {
        expect(pipeline.contains("Task_Start"), "bounty start node missing");
        const auto start_condition =
            pipeline.at("Task_Start").at("custom_recognition_param").dump();
        expect(start_condition.find("Inn") != std::string::npos &&
                   start_condition.find("cursedWheelTitle") != std::string::npos &&
                   start_condition.find("blocking_screen") != std::string::npos &&
                   start_condition.find("combat_active") != std::string::npos,
               "bounty start cannot safely begin from a confirmed city");
        expect(pipeline.contains("Task_TimeLeap_OpenFromCity"),
               "time leap has no bounded city entry branch");
        const auto time_leap_download = pipeline.at("Task_TimeLeap_DownloadEn").dump() +
                                        pipeline.at("Task_TimeLeap_DownloadZhHant").dump();
        expect(time_leap_download.find("startdownload") != std::string::npos &&
                   time_leap_download.find("startdownload_zh_hant") != std::string::npos,
               "time leap download does not accept both language variants");
        const auto &open_from_city = pipeline.at("Task_TimeLeap_OpenFromCity");
        expect(open_from_city.at("max_hit") == 25 &&
                   open_from_city.at("next") ==
                       J::array({"__wvd_observe__Task_TimeLeap_OpenFromCity"}),
               "city entry branch is not bounded or transition-lowered");
    }
    auto verify_unit = [&](const wvd::runtime::SessionDefinition &unit) {
        expect(pipeline.contains(unit.entry), "session entry missing from published graph");
        expect(pipeline.contains(unit.terminal_node), "session terminal missing from published graph");
        expect(!unit.checkpoint_node.empty() && pipeline.contains(unit.checkpoint_node),
               "session checkpoint missing from published graph");
        expect(unit.bundle.revision == definition.initial.bundle.revision,
               "session bundle revision mismatch");
    };
    verify_unit(definition.initial);
    for (const auto &unit : definition.continuation_units)
        verify_unit(unit);
    expect(std::any_of(definition.initial.bundle.files.begin(), definition.initial.bundle.files.end(),
                       [](const auto &file) {
                           return file.relative_path == "image/boot_attention_zh.png";
                       }),
           "Traditional Chinese attention resource missing");
    expect(std::any_of(definition.initial.bundle.files.begin(), definition.initial.bundle.files.end(),
                       [](const auto &file) {
                           return file.relative_path == "image/startdownload_zh_hant.png";
                       }),
           "Traditional Chinese download resource missing");
    return pipeline.size();
}

void verify_author_definition(const wvd::runtime::RunDefinition &definition,
                              bool selected_only) {
    verify_definition(definition, {O::EnsureVpn, O::StartApplication});
    const auto pipeline = read_json(definition.initial.bundle.root / "pipeline/workflow.json");
    const auto &call = pipeline.at("Task_Author_combat");
    expect(call.at("custom_action") == "RunChild", "author combat is not full child");
    expect(call.at("custom_action_param").at("entry") ==
               "Task_Author_combat_Business_Entry",
           "author combat child entry mismatch");
    expect(pipeline.contains("Task_Author_combat_Business_Turn0"),
           "production combat graph missing");
    expect(pipeline.contains("Task_Author_combat_Business_Actor_Entry"),
           "production combat turn graph missing");
    expect(pipeline.contains("Task_Author_wait") != selected_only,
           "selected-node debug replayed or removed the wrong node");
}
} // namespace

int main(int argc, char **argv) {
    try {
        if (argc != 4)
            throw std::runtime_error("usage: test_t0_startup DATA_ROOT PACK_ROOT QUEST_CATALOG");
        verify_android_probe();
        verify_android_viewport();
        verify_initial_plan_contract();
        wvd::app::Application application({std::filesystem::absolute(argv[1]),
                                           std::filesystem::absolute(argv[2]), {},
                                           std::filesystem::absolute(argv[3])});
        const auto without_vpn = wvd::app::ApplicationAssemblyTestAccess::task(
            application, false, "t0-scorpion-vpn-off");
        const auto nodes_without_vpn =
            verify_definition(without_vpn, {O::StartApplication}, true);
        const auto with_vpn = wvd::app::ApplicationAssemblyTestAccess::task(
            application, true, "t0-scorpion-vpn-on");
        const auto nodes_with_vpn = verify_definition(
            with_vpn, {O::EnsureVpn, O::StartApplication}, true);
        expect(nodes_without_vpn == nodes_with_vpn, "VPN setting changed published graph");
        verify_author_definition(wvd::app::ApplicationAssemblyTestAccess::workflow(
                                     application, true, false, "t0-author-full"),
                                 false);
        verify_author_definition(wvd::app::ApplicationAssemblyTestAccess::workflow(
                                     application, true, true, "t0-author-selected"),
                                 true);
        std::cout << "T0_STARTUP_ASSERTIONS " << assertions << " NODES " << nodes_with_vpn << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "T0_STARTUP_FAILED " << error.what() << '\n';
        return 1;
    }
}
