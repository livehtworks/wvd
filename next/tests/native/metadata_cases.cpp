#include "platform/windows/metadata_query.hpp"
#include "maafw/buffers.hpp"
#include <fstream>
#include <iostream>
#include <thread>
#include <windows.h>

int main(int argc, char **argv) {
    using namespace wvd;
    using namespace std::chrono_literals;
    try {
        if (argc != 3 && argc != 4)
            throw std::runtime_error("HELPER_AND_OUTPUT_REQUIRED");
        auto helper = maafw::path_from_utf8(argv[1]);
        if (argc == 4) {
            if (std::string(argv[3]) != "--creation-control")
                throw std::runtime_error("CONTROL_MODE_INVALID");
            // 独立进程的最小 Win32 对照，不创建 MetadataQuery，也不为正式用例预热。
            DWORD before{}, after{};
            GetProcessHandleCount(GetCurrentProcess(), &before);
            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            PROCESS_INFORMATION child{};
            auto command = L"\"" + helper.wstring() + L"\" info -v 0";
            if (!CreateProcessW(helper.c_str(), command.data(), nullptr, nullptr, FALSE,
                                CREATE_NO_WINDOW, nullptr, helper.parent_path().c_str(), &startup,
                                &child))
                throw std::runtime_error("CONTROL_START_FAILED");
            auto wait = WaitForSingleObject(child.hProcess, 2000);
            if (wait != WAIT_OBJECT_0) {
                TerminateProcess(child.hProcess, 1);
                WaitForSingleObject(child.hProcess, 2000);
            }
            CloseHandle(child.hThread);
            CloseHandle(child.hProcess);
            GetProcessHandleCount(GetCurrentProcess(), &after);
            std::ofstream(maafw::path_from_utf8(argv[2])) << nlohmann::json{
                {"before", before},
                {"after", after},
                {"delta", int(after) - int(before)},
                {"watchdog", wait != WAIT_OBJECT_0}}.dump(2);
            return 0;
        }
        nlohmann::json results = nlohmann::json::array();
        for (int mode : {0, 1, 2, 3, 4, 5, 6, 7, 0}) {
            DWORD handles_before{}, handles_after{};
            GetProcessHandleCount(GetCurrentProcess(), &handles_before);
            nlohmann::json result;
            {
                platform::MetadataQuery query;
                std::jthread cancellation;
                if (mode == 7)
                    cancellation = std::jthread([&] {
                        std::this_thread::sleep_for(50ms);
                        query.cancel();
                    });
                result = query.run(helper, mode == 7 ? 1 : mode, mode == 0 ? 2000ms : 500ms);
            }
            GetProcessHandleCount(GetCurrentProcess(), &handles_after);
            std::string expected = mode == 0   ? ""
                                   : mode == 4 ? "METADATA_NONZERO_EXIT"
                                   : mode == 5 ? "METADATA_OUTPUT_LIMIT"
                                   : mode == 6 ? "METADATA_JSON_INVALID"
                                   : mode == 7 ? "METADATA_CANCELLED"
                                               : "METADATA_TIMEOUT";
            result["mode"] = mode;
            result["handles_before"] = handles_before;
            result["handles_after"] = handles_after;
            result["pass"] = result.at("error") == expected && result.at("quiescent") == true &&
                             result.at("pending_io") == 0 &&
                             result.at("elapsed_ms").get<double>() < 2800;
            results.push_back(result);
        }
        std::ofstream(maafw::path_from_utf8(argv[2])) << results.dump(2);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what();
        return 1;
    }
}
