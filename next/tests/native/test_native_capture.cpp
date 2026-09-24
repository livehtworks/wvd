#include "devices/mumu_capture.hpp"
#include <windows.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void verify_reaped(const std::filesystem::path &marker) {
    std::ifstream input(marker);
    DWORD pid{};
    if (!(input >> pid) || !pid) throw std::runtime_error("CAPTURE_STALL_PID_MISSING");
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (process) {
        const auto state = WaitForSingleObject(process, 0);
        CloseHandle(process);
        if (state != WAIT_OBJECT_0)
            throw std::runtime_error("CAPTURE_STALL_CHILD_NOT_REAPED");
    }
}
}

int wmain(int argc, wchar_t **argv) {
    try {
        if (argc != 2) throw std::runtime_error("CAPTURE_HELPER_PATH_REQUIRED");
        const auto helper = std::filesystem::absolute(argv[1]);
        const auto root = std::filesystem::temp_directory_path() /
            (L"wvd-native-capture-stall-" + std::to_wstring(GetCurrentProcessId()));
        std::filesystem::create_directories(root);
        const auto marker = root / L"capture-stall.pid";
        wvd::devices::MumuCaptureClient client(helper, root, helper, 0, L"offline.test");
        const auto started = std::chrono::steady_clock::now();
        for (std::uint64_t generation = 1; generation <= 2; ++generation) {
            bool failed = false;
            try { (void)client.capture(std::chrono::milliseconds{500}); }
            catch (const std::runtime_error &error) {
                failed = std::string(error.what()) == "MUMU_CAPTURE_TIMEOUT";
            }
            if (!failed || client.generation() != generation)
                throw std::runtime_error("CAPTURE_STALL_NOT_ISOLATED");
            verify_reaped(marker);
        }
        client.close();
        if (std::chrono::steady_clock::now() - started > std::chrono::seconds{5})
            throw std::runtime_error("CAPTURE_STALL_RECOVERY_TOO_SLOW");
        std::filesystem::remove(marker);
        std::filesystem::remove(root);
        std::cout << "owned CaptureHost stall, reap and generation replacement passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
