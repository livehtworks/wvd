#include "mumu_binding.hpp"
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
std::string manager_info(const std::filesystem::path &manager, int index) {
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), nullptr, TRUE};
    Handle read, write;
    check(CreatePipe(&read.value, &write.value, &attributes, 0), "MUMU_METADATA_PIPE_FAILED");
    check(SetHandleInformation(read.value, HANDLE_FLAG_INHERIT, 0), "MUMU_METADATA_PIPE_FLAGS");
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdOutput = write.value;
    startup.hStdError = write.value;
    PROCESS_INFORMATION process{};
    auto command = L"\"" + manager.wstring() + L"\" info -v " + std::to_wstring(index);
    check(CreateProcessW(manager.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                         nullptr, manager.parent_path().c_str(), &startup, &process),
          "MUMU_METADATA_START_FAILED");
    Handle child{process.hProcess}, thread{process.hThread};
    CloseHandle(write.value);
    write.value = nullptr;
    std::string output;
    char block[4096];
    DWORD count{};
    while (ReadFile(read.value, block, sizeof(block), &count, nullptr) && count) {
        check(output.size() + count < 2 * 1024 * 1024, "MUMU_METADATA_TOO_LARGE");
        output.append(block, count);
    }
    // 元数据进程不是取消替代物；不强杀后把验证计为成功。
    WaitForSingleObject(child.value, INFINITE);
    DWORD exit{};
    GetExitCodeProcess(child.value, &exit);
    check(exit == 0, "MUMU_METADATA_FAILED");
    return output;
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
            throw std::runtime_error("OTHER_CONTROLLER_PRESENT");
        if (_wcsicmp(name.c_str(), L"python.exe") == 0 && !ancestors.contains(entry.th32ProcessID))
            throw std::runtime_error("PYTHON_CONTROLLER_OWNERSHIP_UNCONFIRMED");
    }
}
} // namespace
nlohmann::json verify_mumu_binding(const std::filesystem::path &file) {
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
    auto live = nlohmann::json::parse(manager_info(manager, index));
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
    return binding;
}
} // namespace wvd::platform
