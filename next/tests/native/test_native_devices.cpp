#include "devices/android_probe.hpp"
#include "devices/scrcpy_codec.hpp"
#include "devices/lifecycle_execution.hpp"
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
    std::optional<wvd::devices::LifecycleObservation> observe_lifecycle() override {
        return wvd::devices::LifecycleObservation{target, running, connected, game, true,
            generation, std::chrono::steady_clock::now(), game, exited};
    }
    bool execute_lifecycle(wvd::devices::LifecycleOperation op,
        const wvd::devices::LifecycleTarget &, const std::function<bool()> &) override {
        ++called;
        if (op == wvd::devices::LifecycleOperation::RestartInstance) {
            exited = false; running = connected = true; ++generation;
        } else if (op == wvd::devices::LifecycleOperation::StartApplication) game = true;
        return true;
    }
};
template <typename Call>
void rejected(Call call, const char *reason) {
    try { call(); } catch (const std::exception &) { return; }
    throw std::runtime_error(reason);
}
}

int main() {
    try {
        namespace d = wvd::devices;
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
