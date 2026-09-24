#include <array>
#include <string>
#include <windows.h>

// 本轮输出 helper 不加载 Maa、ADB，也不接触用户文件。模式仅由固定 info -v 数字选择。
int wmain(int argc, wchar_t **argv) {
    if (argc != 4 || std::wstring(argv[1]) != L"info" || std::wstring(argv[2]) != L"-v")
        return 99;
    int mode = std::stoi(argv[3]);
    auto write = [](HANDLE h, const std::string &data) {
        DWORD count{};
        return WriteFile(h, data.data(), static_cast<DWORD>(data.size()), &count, nullptr) &&
               count == data.size();
    };
    if (mode == 0) {
        write(GetStdHandle(STD_ERROR_HANDLE), "separate stderr");
        return write(GetStdHandle(STD_OUTPUT_HANDLE), "{\"index\":\"0\",\"error_code\":0}") ? 0
                                                                                            : 98;
    }
    if (mode == 1) {
        Sleep(60000);
        return 0;
    }
    if (mode == 2) {
        for (int i = 0; i < 10000; ++i) {
            if (!write(GetStdHandle(STD_OUTPUT_HANDLE), "stream\n"))
                return 0;
            Sleep(1);
        }
    }
    if (mode == 3) {
        std::wstring path(32768, L'\0');
        path.resize(GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size())));
        auto command = L"\"" + path + L"\" info -v 1";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        PROCESS_INFORMATION child{};
        if (!CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                            nullptr, nullptr, &startup, &child))
            return 97;
        CloseHandle(child.hProcess);
        CloseHandle(child.hThread);
        return 0;
    }
    if (mode == 4)
        return 23;
    if (mode == 5) {
        std::string block(8192, 'x');
        for (int i = 0; i < 400; ++i)
            if (!write(GetStdHandle(STD_OUTPUT_HANDLE), block))
                return 0;
    }
    if (mode == 6)
        write(GetStdHandle(STD_OUTPUT_HANDLE), "not json");
    return 0;
}
