#include "../../core/platform.hpp"
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <chrono>
#include <cstdio>
namespace platform {
nlohmann::json memory() {
 PROCESS_MEMORY_COUNTERS_EX m{}; m.cb=sizeof(m);
 if(!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&m),sizeof(m))) throw std::runtime_error("memory query");
 DWORD handles=0; if(!GetProcessHandleCount(GetCurrentProcess(), &handles)) throw std::runtime_error("handle query");
 unsigned threads=0;
 HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
 if(snap==INVALID_HANDLE_VALUE) throw std::runtime_error("thread snapshot");
 THREADENTRY32 e{}; e.dwSize=sizeof(e);
 if(Thread32First(snap,&e)) do {if(e.th32OwnerProcessID==GetCurrentProcessId()) ++threads;} while(Thread32Next(snap,&e));
 CloseHandle(snap);
 return {{"private_bytes",m.PrivateUsage},{"working_set",m.WorkingSetSize},{"handles",handles},{"threads",threads}};
}
static std::string utf8(const wchar_t* p) {
 int n=WideCharToMultiByte(CP_UTF8,0,p,-1,nullptr,0,nullptr,nullptr);
 std::string v(n,0); WideCharToMultiByte(CP_UTF8,0,p,-1,v.data(),n,nullptr,nullptr); v.pop_back(); return v;
}
nlohmann::json identity() {
 wchar_t path[32768]{};
 GetModuleFileNameW(GetModuleHandleW(L"MaaFramework.dll"),path,32768);
 return {{"pid",GetCurrentProcessId()},{"tid",GetCurrentThreadId()},{"maa_library_path",utf8(path)}};
}
std::string utc(){
 SYSTEMTIME t{};GetSystemTime(&t); char b[40]{};
 std::snprintf(b,sizeof(b),"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds);return b;
}
}

