#include <windows.h>
#include <filesystem>
#include <fstream>

int wmain(int argc, wchar_t **argv) {
    if (argc != 5) return 2;
    const auto marker = std::filesystem::path(argv[1]) / L"capture-stall.pid";
    std::ofstream output(marker, std::ios::trunc);
    output << GetCurrentProcessId() << '\n';
    output.close();
    Sleep(INFINITE);
    return 0;
}
