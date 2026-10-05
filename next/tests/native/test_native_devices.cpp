#include "devices/android_probe.hpp"
#include "devices/scrcpy_codec.hpp"
#include "devices/lifecycle_execution.hpp"
#include "devices/metadata_read_fault.hpp"
#include "devices/adb_failure.hpp"
#include "platform/windows/memory_diagnostics.hpp"
#include "platform/windows/mumu_binding.hpp"
#include <iostream>
#include <stdexcept>

namespace {
class RecoveryPort final : public wvd::devices::LifecyclePort {
  public:
    wvd::devices::LifecycleTarget target{"device", "2", "game", "", false};
    bool exited{}, running{}, connected{}, game{};
    std::uint64_t generation{1};
    int called{};
    int transient_failures{};
    int start_without_focus{};
    bool timeout_after_effect{}, permanent_failure{};
    std::optional<wvd::devices::LifecycleObservation> observe_lifecycle() override {
        return wvd::devices::LifecycleObservation{target, running, connected, game, true,
            generation, std::chrono::steady_clock::now(), game, exited};
    }
    bool execute_lifecycle(wvd::devices::LifecycleOperation op,
        const wvd::devices::LifecycleTarget &, const std::function<bool()> &) override {
        ++called;
        if (permanent_failure || transient_failures-- > 0) {
            if (timeout_after_effect) game = true;
            wvd::devices::AdbFailureInfo info;
            info.code = permanent_failure ? "ADB_UNAUTHORIZED" : "ADB_TIMEOUT";
            info.timed_out = !permanent_failure;
            info.retryable_transport = !permanent_failure;
            throw wvd::devices::AdbCommandFailure(info);
        }
        if (op == wvd::devices::LifecycleOperation::RestartInstance) {
            exited = false; running = connected = true; ++generation;
        } else if (op == wvd::devices::LifecycleOperation::StartApplication) {
            if (start_without_focus > 0) --start_without_focus;
            else game = true;
        }
        return true;
    }
};
template <typename Call>
void rejected(Call call, const char *reason) {
    try { call(); } catch (const std::exception &) { return; }
    throw std::runtime_error(reason);
}
}

int main(int argc, char **argv) {
    try {
        namespace d = wvd::devices;
        if (argc == 2 && std::string(argv[1]) == "--closure-metadata") {
            using J = nlohmann::json;
            const J timeout{{"success", false}, {"primary_error", "METADATA_TIMEOUT"},
                {"error", "METADATA_TIMEOUT"}, {"quiescent", true}, {"handles_released", true},
                {"helper_exited", true}, {"pending_io", 0}, {"elapsed_ms", 23.75}};
            bool retryable{};
            try { d::require_metadata_read(timeout, std::chrono::milliseconds{17}); }
            catch (const wvd::contracts::ObservationUnavailable &e) {
                retryable = e.fault().kind == wvd::contracts::ReadFaultKind::Timeout &&
                    e.fault().timeout.count() == 17 && e.fault().elapsed.count() == 23 &&
                    e.fault().details.at("elapsed_ms") == 23.75 &&
                    J::parse(e.fault().details.at("report").get<std::string>()) == timeout;
            }
            if (!retryable) throw std::runtime_error("META-01");
            const auto blocked = [&](J report) {
                try { d::require_metadata_read(report, std::chrono::milliseconds{17}); }
                catch (const wvd::contracts::ObservationUnavailable &) { throw std::runtime_error("META-02_RETRY_FORBIDDEN"); }
                catch (const std::runtime_error &e) {
                    if (std::string(e.what()).find(report.value("error", "")) == std::string::npos)
                        throw std::runtime_error("META-02_ORIGINAL_ERROR_LOST");
                    return;
                }
                throw std::runtime_error("META-02_NOT_BLOCKED");
            };
            for (const auto *field : {"quiescent", "handles_released", "helper_exited", "pending_io"}) {
                auto report = timeout; report.erase(field); blocked(report);
            }
            for (const auto *error : {"METADATA_CLEANUP_PENDING", "MUMU_INSTANCE_MISMATCH", "MUMU_ADB_BINDING_MISMATCH",
                "METADATA_JSON_INVALID", "METADATA_COMMAND_INVALID", "METADATA_CANCELLED", "OTHER_ERROR"}) {
                auto report = timeout; report["error"] = error;
                if (std::string(error) != "METADATA_CLEANUP_PENDING") report["primary_error"] = error;
                blocked(report);
            }
            std::cout << "META-01 META-02 PASS: classified report only; no helper or device launched\n";
            return 0;
        }
        // 只保护本次修复的准入边界；不把假端口结果当成真实MuMu恢复验收。
        const nlohmann::json crashed{{"error_code", 900}, {"is_process_started", false},
            {"is_android_started", false}};
        if (!wvd::platform::mumu_metadata_usable(crashed) ||
            wvd::platform::mumu_metadata_usable({{"error_code", 900}}) ||
            !wvd::platform::mumu_metadata_usable({{"error_code", 900}, {"is_process_started", true},
                {"is_android_started", false}}) ||
            wvd::platform::mumu_metadata_usable({{"error_code", 901}, {"is_process_started", true}}))
            throw std::runtime_error("CRASH_METADATA_BOUNDARY_INVALID");
        RecoveryPort recovery;
        d::LifecyclePlan plan{recovery.target,
            {d::LifecycleOperation::RestartInstance, d::LifecycleOperation::StartApplication}, 1};
        const auto event = [](const auto &, const auto &) {};
        if (d::execute_lifecycle_plan(plan, recovery, [] { return false; }, event) !=
            d::LifecycleEnd::RetryRequired || recovery.called)
            throw std::runtime_error("UNPROVEN_INSTANCE_EXIT_RESTARTED");
        recovery.exited = true;
        if (d::execute_lifecycle_plan(plan, recovery, [] { return true; }, event) !=
            d::LifecycleEnd::Cancelled || recovery.called)
            throw std::runtime_error("CANCELLED_RECOVERY_STARTED");
        if (d::execute_lifecycle_plan(plan, recovery, [] { return false; }, event) !=
            d::LifecycleEnd::ReadyForBoot || recovery.called != 2 || !recovery.game)
            throw std::runtime_error("CONFIRMED_CRASH_RECOVERY_FAILED");
        for (const bool already_applied : {false, true}) {
            RecoveryPort slow;
            slow.running = slow.connected = true;
            slow.transient_failures = 1;
            slow.timeout_after_effect = already_applied;
            const d::LifecyclePlan start{slow.target, {d::LifecycleOperation::StartApplication}, 1};
            if (d::execute_lifecycle_plan(start, slow, [] { return false; }, event) !=
                    d::LifecycleEnd::ReadyForBoot || slow.called != (already_applied ? 1 : 2))
                throw std::runtime_error("LIFECYCLE_TIMEOUT_NOT_REOBSERVED");
            slow.game = false;
            slow.permanent_failure = true;
            rejected([&] { d::execute_lifecycle_plan(start, slow, [] { return false; }, event); },
                "PERMANENT_LIFECYCLE_FAILURE_RETRIED");
        }
        const auto owners = wvd::platform::sample_memory_owners();
        {
            RecoveryPort delayed;
            delayed.running = delayed.connected = true;
            delayed.start_without_focus = 1;
            d::LifecyclePlan start{delayed.target, {d::LifecycleOperation::StartApplication}, 1};
            start.step_timeout = std::chrono::seconds(5);
            unsigned retries = 0;
            const auto events = [&](const std::string &name, const nlohmann::json &) {
                if (name == "lifecycle.backend_retried") ++retries;
            };
            if (d::execute_lifecycle_plan(start, delayed, [] { return false; }, events) !=
                    d::LifecycleEnd::ReadyForBoot || delayed.called != 2 || retries != 1)
                throw std::runtime_error("START_ACK_WITHOUT_FOCUS_NOT_RETRIED");
        }
        if (!owners.available || !owners.count || owners.count > 8 || owners.examined > 4096)
            throw std::runtime_error("MEMORY_OWNERS_SNAPSHOT_INVALID");
        for (unsigned i = 1; i < owners.count; ++i)
            if (owners.top[i - 1].private_bytes < owners.top[i].private_bytes)
                throw std::runtime_error("MEMORY_OWNERS_NOT_SORTED");
        wvd::contracts::Command click;
        click.kind = wvd::contracts::ActionKind::Click;
        click.x = 899;
        click.y = 1599;
        const auto messages = d::scrcpy::encode(click, 900, 1600);
        if (messages.size() != 2 || messages[0].size() != 32 ||
            messages[0][0] != 2 || messages[0][1] != 0 ||
            messages[0][12] != 3 || messages[0][13] != 131 ||
            messages[0][16] != 6 || messages[0][17] != 63 ||
            messages[0][18] != 3 || messages[0][19] != 132 ||
            messages[0][20] != 6 || messages[0][21] != 64 ||
            messages[0][22] != 255 || messages[0][23] != 255 ||
            messages[1][1] != 1 || messages[1][22] != 0 || messages[1][23] != 0)
            throw std::runtime_error("SCRCPY_CLICK_WIRE_INVALID");
        click.click_pair_interval_ms = 100;
        const auto pair = d::scrcpy::encode(click, 900, 1600);
        if (pair.size() != 4 || pair[0] != messages[0] || pair[1] != messages[1] ||
            pair[2] != messages[0] || pair[3] != messages[1])
            throw std::runtime_error("SCRCPY_CLICK_PAIR_WIRE_INVALID");
        click.click_pair_interval_ms = 500;
        rejected([&] { (void)d::scrcpy::encode(click, 900, 1600); }, "SCRCPY_OLD_500MS_PAIR_ACCEPTED");
        click.click_pair_interval_ms = 1;
        rejected([&] { (void)d::scrcpy::encode(click, 900, 1600); }, "SCRCPY_UNBOUNDED_PAIR_ACCEPTED");
        click.click_pair_interval_ms = 0;
        click.x = 900;
        rejected([&] { (void)d::scrcpy::encode(click, 900, 1600); },
            "SCRCPY_INVALID_COORDINATE_ACCEPTED");
        wvd::contracts::Command swipe;
        swipe.kind = wvd::contracts::ActionKind::Swipe;
        swipe.x = 100; swipe.y = 200; swipe.x2 = 500; swipe.y2 = 600;
        swipe.duration = 100;
        const auto motion = d::scrcpy::encode(swipe, 900, 1600);
        if (motion.size() != 5 || motion.front()[1] != 0 ||
            motion[1][1] != 2 || motion.back()[1] != 1)
            throw std::runtime_error("SCRCPY_SWIPE_WIRE_INVALID");
        wvd::contracts::Command back;
        back.kind = wvd::contracts::ActionKind::ClickKey;
        back.key = 4;
        const auto keys = d::scrcpy::encode(back, 900, 1600);
        if (keys.size() != 2 || keys[0].size() != 14 || keys[0][5] != 4 ||
            keys[0][1] != 0 || keys[1][1] != 1)
            throw std::runtime_error("SCRCPY_KEY_WIRE_INVALID");
        const auto reply = d::android::parse_shell_reply("123\nWVD_RC_0", "WVD_RC_");
        if (reply.exit_code != 0 || reply.output != "123" ||
            d::android::process(reply) != d::android::Presence::Present ||
            d::android::process({1, ""}) != d::android::Presence::Absent)
            throw std::runtime_error("ADB_REPLY_SEMANTICS_INVALID");
        rejected([&] { (void)d::android::parse_shell_reply("123", "WVD_RC_"); },
            "ADB_MISSING_TRAILER_ACCEPTED");
        rejected([&] { (void)d::android::process({0, ""}); },
            "ADB_EMPTY_SUCCESS_ACCEPTED");
        std::cout << "scrcpy wire and ADB result boundaries passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
