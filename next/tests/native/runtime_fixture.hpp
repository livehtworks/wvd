#pragma once
#include "maafw/buffers.hpp"
#include "runtime/run_coordinator.hpp"
#include <fstream>

namespace fixture {
using namespace wvd;
using J = nlohmann::json;
using namespace std::chrono_literals;
inline void require(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}
template <class F> void until(F &&predicate, std::chrono::milliseconds timeout = 5000ms) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() > deadline)
            throw std::runtime_error("CHECKPOINT_TIMEOUT");
        std::this_thread::sleep_for(5ms);
    }
}
inline std::vector<std::uint8_t> bytes(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    require(bool(file), "fixture missing");
    return {std::istreambuf_iterator<char>(file), {}};
}
class OfflineDevice final : public devices::DeviceBackend {
  public:
    std::string identity = "m2-offline", viewport = "portrait", application = "fixture.app";
    contracts::Size size{900, 1600};
    std::vector<std::uint8_t> before, after;
    std::atomic<bool> real{false}, block_connect{false}, block_input{false}, unblock{false},
        reject_release{false}, change_frame{true}, changed{false}, connect_ok{true};
    std::atomic<int> connections{}, captures{}, calls{}, release_calls{};
    mutable std::mutex mutex;
    std::vector<contracts::Command> sent;
    bool offline() const override {
        return !real;
    }
    bool connect() override {
        ++connections;
        while (block_connect && !unblock)
            std::this_thread::sleep_for(5ms);
        return connect_ok;
    }
    devices::RawFrame capture() override {
        ++captures;
        return {changed ? after : before, size, identity, viewport, application};
    }
    bool execute(const contracts::Command &command) override {
        ++calls;
        {
            std::lock_guard lock(mutex);
            sent.push_back(command);
        }
        while (block_input && !unblock)
            std::this_thread::sleep_for(5ms);
        if (command.kind == contracts::ActionKind::TouchUp ||
            command.kind == contracts::ActionKind::KeyUp) {
            ++release_calls;
            if (reject_release)
                return false;
        }
        if (change_frame)
            changed = true;
        return true;
    }
};
struct Unblock {
    std::shared_ptr<OfflineDevice> device;
    ~Unblock() {
        device->unblock = true;
        device->reject_release = false;
    }
};
struct Setup {
    maafw::Bundle bundle;
    std::optional<maafw::Bundle> alternate;
    std::shared_ptr<OfflineDevice> device = std::make_shared<OfflineDevice>();
    std::filesystem::path output;
    contracts::InputPolicy policy;
    explicit Setup(const J &config) {
        bundle.root = maafw::path_from_utf8(config.at("bundle"));
        bundle.revision = "runtime-fixture";
        for (const auto &file : config.at("files"))
            bundle.files.push_back({file.at("path"), file.at("sha256")});
        if (config.contains("alternate")) {
            alternate = maafw::Bundle{
                maafw::path_from_utf8(config["alternate"]["root"]), "runtime-fixture-2", {}};
            for (const auto &file : config["alternate"]["files"])
                alternate->files.push_back({file.at("path"), file.at("sha256")});
        }
        device->before = bytes(maafw::path_from_utf8(config.at("before")));
        device->after = bytes(maafw::path_from_utf8(config.at("after")));
        device->identity = config.at("device_id");
        if (config.value("large", false))
            device->size = {1080, 1920};
        output = maafw::path_from_utf8(config.at("output"));
        policy = {device->identity,
                  "offline",
                  device->application,
                  bundle.revision,
                  device->viewport,
                  {900, 1600},
                  {},
                  {},
                  {"battle"},
                  2000ms};
        for (int i = 0; i <= int(contracts::ActionKind::Inactive); ++i) {
            policy.capabilities.insert(static_cast<contracts::ActionKind>(i));
            policy.permissions.insert(static_cast<contracts::ActionKind>(i));
        }
    }
    runtime::RunDefinition definition(const std::string &entry = "Normal") {
        runtime::RunDefinition d;
        d.request_id = "request-1";
        d.policy = policy;
        d.initial = {bundle, entry, "Terminal", {}, 5000ms, 150ms};
        d.initial.actions["TestWait"] = [](maafw::Context &context, const J &) {
            while (!context.cancelled())
                std::this_thread::sleep_for(5ms);
            return false;
        };
        d.initial.actions["TestFalse"] = [](maafw::Context &, const J &) { return false; };
        d.initial.actions["TestThrow"] = [](maafw::Context &, const J &) -> bool {
            throw std::runtime_error("TEST_CALLBACK_EXCEPTION");
        };
        return d;
    }
};
inline maafw::RecognitionRequest target() {
    return {"target", "1", {0, 0, 900, 900}, maafw::TemplateParameters{"target.png", 0.99}};
}
inline maafw::RecognitionRequest scene() {
    return {"scene", "1", {0, 0, 900, 900}, maafw::TemplateParameters{"scene.png", 0.99}};
}
inline bool contains(const J &events, const std::string &type) {
    for (const auto &event : events.at("events"))
        if (event.at("type") == type)
            return true;
    return false;
}
inline void check_cleanup(const J &events) {
    std::vector<std::string> order;
    for (const auto &event : events.at("events")) {
        auto type = event.at("type").get<std::string>();
        if (type.starts_with("objects."))
            order.push_back(type);
    }
    require(order == std::vector<std::string>{"objects.tasker_destroyed",
                                              "objects.controller_destroyed",
                                              "objects.resource_destroyed"},
            "cleanup order mismatch");
}
J runtime_case(const std::string &name, Setup &setup);
J gate_case(const std::string &name, Setup &setup);
J storage_case(const std::string &name, Setup &setup);
} // namespace fixture
