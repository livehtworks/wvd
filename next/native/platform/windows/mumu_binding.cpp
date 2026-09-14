#include "mumu_binding.hpp"
#include "metadata_query.hpp"
#include "maafw/buffers.hpp"
#include <algorithm>
#include <fstream>
#include <set>
#include <windows.h>

#include <tlhelp32.h>

namespace wvd::platform {
namespace {
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~Handle() {
        if (value && value != INVALID_HANDLE_VALUE)
            CloseHandle(value);
    }
};
void check(bool value, const char *code) {
    if (!value)
        throw std::runtime_error(code);
}
void reject_controller(const char *code, const PROCESSENTRY32W &entry) {
    nlohmann::json evidence{
        {"pid", entry.th32ProcessID}, {"image", "unknown"}, {"created", nullptr}};
    Handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID)};
    if (process.value) {
        std::wstring image(32768, L'\0');
        DWORD size = static_cast<DWORD>(image.size());
        if (QueryFullProcessImageNameW(process.value, 0, image.data(), &size)) {
            image.resize(size);
            evidence["image"] = maafw::utf8(std::filesystem::path(image));
        }
        FILETIME created{}, ended{}, kernel{}, user{};
        if (GetProcessTimes(process.value, &created, &ended, &kernel, &user))
            evidence["created"] =
                (std::uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    }
    throw std::runtime_error(std::string(code) + ":" + evidence.dump());
}
void check_controllers() {
    Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)};
    check(snapshot.value != INVALID_HANDLE_VALUE, "PROCESS_ENUMERATION_FAILED");
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    check(Process32FirstW(snapshot.value, &entry), "PROCESS_ENUMERATION_FAILED");
    std::vector<PROCESSENTRY32W> processes;
    do {
        processes.push_back(entry);
    } while (Process32NextW(snapshot.value, &entry));
    std::set<DWORD> ancestors;
    auto pid = GetCurrentProcessId();
    for (int depth = 0; depth < 64 && pid && !ancestors.contains(pid); ++depth) {
        ancestors.insert(pid);
        auto found = std::find_if(processes.begin(), processes.end(),
                                  [&](const auto &p) { return p.th32ProcessID == pid; });
        if (found == processes.end())
            break;
        pid = found->th32ParentProcessID;
    }
    for (const auto &entry : processes) {
        const auto name = std::wstring(entry.szExeFile);
        if (_wcsicmp(name.c_str(), L"wvd.exe") == 0 || _wcsicmp(name.c_str(), L"scrcpy.exe") == 0 ||
            _wcsicmp(name.c_str(), L"pythonw.exe") == 0)
            reject_controller("OTHER_CONTROLLER_PRESENT", entry);
        if (_wcsicmp(name.c_str(), L"python.exe") == 0 && !ancestors.contains(entry.th32ProcessID))
            reject_controller("PYTHON_CONTROLLER_OWNERSHIP_UNCONFIRMED", entry);
    }
}
} // namespace
nlohmann::json verify_mumu_binding(const std::filesystem::path &file,
                                   std::stop_token cancellation) {
    std::ifstream input(file);
    check(bool(input), "DEVICE_BINDING_REQUIRED");
    nlohmann::json binding;
    input >> binding;
    check(binding.value("schema", 0) == 1, "DEVICE_BINDING_SCHEMA");
    check(binding.value("controller_audit", std::string{}) == "NO_OTHER_CONTROLLER",
          "CONTROLLER_OWNERSHIP_UNCONFIRMED");
    check_controllers();
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
} // namespace wvd::platform
