#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>

// A post-call volatile load also prevents tail calls from erasing fixture frames.
__declspec(noinline) void *AllocKeep() { void *volatile value = HeapAlloc(GetProcessHeap(), 0, 4096); return value; }
__declspec(noinline) void *AllocRelease() { void *volatile value = HeapAlloc(GetProcessHeap(), 0, 2048); return value; }
__declspec(noinline) void *AllocStable() { void *volatile value = HeapAlloc(GetProcessHeap(), 0, 1024); return value; }
__declspec(noinline) void *AllocChurn() { void *volatile value = HeapAlloc(GetProcessHeap(), 0, 128); return value; }

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    const auto root = std::filesystem::absolute(argv[1]);
    if (!std::filesystem::is_directory(root)) return 3;
    void *keep[4]{}, *release[3]{}, *stable[2]{};
    std::ofstream(root / "ready") << GetCurrentProcessId();
    const auto deadline = GetTickCount64() + 120000;
    bool b0 = false, b1 = false;
    while (GetTickCount64() < deadline && !std::filesystem::exists(root / "stop.request")) {
        if (!b0 && std::filesystem::exists(root / "b0.request")) {
            for (auto &block : release) block = AllocRelease();
            for (auto &block : stable) block = AllocStable();
            for (auto block : release) if (!block || HeapSize(GetProcessHeap(), 0, block) != 2048) return 4;
            for (auto block : stable) if (!block || HeapSize(GetProcessHeap(), 0, block) != 1024) return 5;
            std::ofstream(root / "b0.ready") << "release=3*2048 stable=2*1024";
            b0 = true;
        }
        if (b0 && !b1 && std::filesystem::exists(root / "b1.request")) {
            for (auto &block : keep) block = AllocKeep();
            for (auto block : keep) if (!block || HeapSize(GetProcessHeap(), 0, block) != 4096) return 6;
            for (auto &block : release) { HeapFree(GetProcessHeap(), 0, block); block = nullptr; }
            for (int i = 0; i < 4096; ++i) { auto *block = AllocChurn(); if (!block) return 7; HeapFree(GetProcessHeap(), 0, block); }
            std::ofstream(root / "b1.ready") << "keep=4*4096 stable=2*1024 churn=0";
            b1 = true;
        }
        Sleep(20);
    }
    for (auto *block : keep) if (block) HeapFree(GetProcessHeap(), 0, block);
    for (auto *block : release) if (block) HeapFree(GetProcessHeap(), 0, block);
    for (auto *block : stable) if (block) HeapFree(GetProcessHeap(), 0, block);
    return 0;
}
