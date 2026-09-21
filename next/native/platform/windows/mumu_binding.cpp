#include "mumu_binding.hpp"
#include "metadata_query.hpp"
#include "maafw/buffers.hpp"
#include <fstream>

namespace wvd::platform {
namespace {
void check(bool value, const char *code) {
    if (!value)
        throw std::runtime_error(code);
}
} // namespace
nlohmann::json create_mumu_binding(const std::filesystem::path &manager, int index,
                                   std::string serial, std::stop_token cancellation) {
    check(manager.is_absolute() && manager.filename() == "MuMuManager.exe" &&
              std::filesystem::is_regular_file(manager),
          "MUMU_MANAGER_INVALID");
    check(index >= 0 && index <= 10000, "MUMU_INSTANCE_INVALID");
    check(!serial.empty() && serial.size() <= 128 &&
              serial.find_first_of("\r\n\t ") == std::string::npos,
          "MUMU_ADB_BINDING_INVALID");
    MetadataQuery query;
    std::stop_callback cancel(cancellation, [&] { query.cancel(); });
    auto metadata = query.run(manager, index);
    if (!metadata.at("success").get<bool>())
        throw std::runtime_error(metadata.at("error").get<std::string>() + ":" + metadata.dump());
    const auto &live = metadata.at("data");
    check(live.value("error_code", -1) == 0 &&
              live.at("index").get<std::string>() == std::to_string(index),
          "MUMU_INSTANCE_MISMATCH");
    if (live.value("is_android_started", false))
        check(live.contains("adb_port") &&
                  "127.0.0.1:" + std::to_string(live.at("adb_port").get<int>()) == serial,
              "CONFIG_MANAGER_ADB_MISMATCH");
    const auto adb = manager.parent_path() / "adb.exe";
    check(std::filesystem::is_regular_file(adb), "MUMU_ADB_PATH_MISMATCH");
    return {{"schema", 1},
            {"index", index},
            {"created_timestamp", live.at("created_timestamp")},
            {"manager", maafw::utf8(manager)},
            {"adb", maafw::utf8(adb)},
            {"install_root", maafw::utf8(manager.parent_path().parent_path())},
            {"serial", std::move(serial)},
            {"controller_audit", "APPLICATION_DEVICE_OWNER"},
            {"initial_manager", live},
            {"initially_running", live.value("is_process_started", false)}};
}
nlohmann::json verify_mumu_binding(const nlohmann::json &source,
                                   std::stop_token cancellation) {
    auto binding = source;
    check(binding.value("schema", 0) == 1, "DEVICE_BINDING_SCHEMA");
    const auto audit = binding.value("controller_audit", std::string{});
    check(audit == "NO_OTHER_CONTROLLER" || audit == "APPLICATION_DEVICE_OWNER",
          "CONTROLLER_OWNERSHIP_UNCONFIRMED");
    auto manager = maafw::path_from_utf8(binding.at("manager"));
    check(manager.is_absolute() && manager.filename() == "MuMuManager.exe" &&
              std::filesystem::is_regular_file(manager),
          "MUMU_MANAGER_INVALID");
    int index = binding.at("index");
    check(index >= 0 && index <= 10000, "MUMU_INSTANCE_INVALID");
    MetadataQuery query;
    std::stop_callback cancel(cancellation, [&] { query.cancel(); });
    auto metadata = query.run(manager, index);
    if (!metadata.at("success").get<bool>())
        throw std::runtime_error(metadata.at("error").get<std::string>() + ":" + metadata.dump());
    auto live = metadata.at("data");
    check(live.value("error_code", -1) == 0 &&
              live.at("index").get<std::string>() == std::to_string(index),
          "MUMU_INSTANCE_MISMATCH");
    check(live.value("is_android_started", false) && live.value("is_process_started", false),
          "MUMU_INSTANCE_NOT_RUNNING");
    check(live.contains("adb_port") && "127.0.0.1:" + std::to_string(live["adb_port"].get<int>()) ==
                                           binding.at("serial").get<std::string>(),
          "MUMU_ADB_BINDING_MISMATCH");
    check(live.at("created_timestamp") == binding.at("created_timestamp"),
          "MUMU_INSTANCE_REPLACED");
    auto adb = maafw::path_from_utf8(binding.at("adb"));
    check(std::filesystem::canonical(adb) ==
              std::filesystem::canonical(manager.parent_path() / "adb.exe"),
          "MUMU_ADB_PATH_MISMATCH");
    auto root = manager.parent_path().parent_path();
    check(std::filesystem::canonical(maafw::path_from_utf8(binding.at("install_root"))) ==
              std::filesystem::canonical(root),
          "MUMU_INSTALL_MISMATCH");
    binding["live_manager"] = live;
    binding["metadata_query"] = metadata;
    return binding;
}
nlohmann::json verify_mumu_binding(const std::filesystem::path &file,
                                   std::stop_token cancellation) {
    std::ifstream input(file);
    check(bool(input), "DEVICE_BINDING_REQUIRED");
    nlohmann::json binding;
    input >> binding;
    return verify_mumu_binding(binding, cancellation);
}
} // namespace wvd::platform
