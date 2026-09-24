#include "process.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>
#include <thread>
#include <windows.h>

namespace wvd::platform {
namespace {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

struct Handle {
    HANDLE value{};
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle() = default;
    explicit Handle(HANDLE handle) : value(handle) {}
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
};

std::wstring quote(const std::wstring &value) {
    std::wstring result = L"\"";
    unsigned backslashes = 0;
    for (const auto c : value) {
        if (c == L'\\') { ++backslashes; continue; }
        if (c == L'"') {
            result.append(backslashes * 2 + 1, L'\\');
            result.push_back(L'"');
        } else {
            result.append(backslashes, L'\\');
            result.push_back(c);
        }
        backslashes = 0;
    }
    result.append(backslashes * 2, L'\\');
    result.push_back(L'"');
    return result;
}

struct Pipe {
    Handle reader;
    Handle writer;
    std::vector<std::uint8_t> bytes;
    void create() {
        SECURITY_ATTRIBUTES attributes{sizeof(attributes), nullptr, TRUE};
        HANDLE read{}, write{};
        if (!CreatePipe(&read, &write, &attributes, 0))
            throw std::runtime_error("PROCESS_PIPE_CREATE_FAILED");
        reader.value = read;
        writer.value = write;
        if (!SetHandleInformation(reader.value, HANDLE_FLAG_INHERIT, 0))
            throw std::runtime_error("PROCESS_PIPE_INHERIT_FAILED");
    }
    bool drain(std::size_t limit) {
        bool activity = false;
        for (;;) {
            DWORD available{};
            if (!PeekNamedPipe(reader.value, nullptr, 0, nullptr, &available, nullptr)) {
                if (GetLastError() == ERROR_BROKEN_PIPE ||
                    GetLastError() == ERROR_PIPE_NOT_CONNECTED ||
                    GetLastError() == ERROR_NO_DATA) return activity;
                throw std::runtime_error("PROCESS_PIPE_PEEK_FAILED");
            }
            if (!available) return activity;
            if (bytes.size() + available > limit)
                throw std::runtime_error("PROCESS_OUTPUT_LIMIT");
            std::array<std::uint8_t, 8192> block{};
            DWORD count{};
            if (!ReadFile(reader.value, block.data(),
                          std::min<DWORD>(available, static_cast<DWORD>(block.size())),
                          &count, nullptr))
                throw std::runtime_error("PROCESS_PIPE_READ_FAILED");
            bytes.insert(bytes.end(), block.begin(), block.begin() + count);
            activity = true;
        }
    }
};

struct AttributeList {
    std::vector<std::uint8_t> buffer;
    LPPROC_THREAD_ATTRIBUTE_LIST list{};
    ~AttributeList() { if (list) DeleteProcThreadAttributeList(list); }
    void create(std::array<HANDLE, 3> &handles) {
        SIZE_T bytes{};
        InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
        buffer.resize(bytes);
        list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(buffer.data());
        if (!InitializeProcThreadAttributeList(list, 1, 0, &bytes)) {
            list = nullptr;
            throw std::runtime_error("PROCESS_ATTRIBUTE_CREATE_FAILED");
        }
        if (!UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                       handles.data(), sizeof(handles), nullptr, nullptr))
            throw std::runtime_error("PROCESS_HANDLE_LIST_FAILED");
    }
};

} // namespace

ProcessResult run_process(const std::filesystem::path &executable,
                          const std::vector<std::wstring> &arguments,
                          std::chrono::milliseconds timeout, std::stop_token stop,
                          std::size_t output_limit, bool own_child_tree) {
    if (!executable.is_absolute() || !std::filesystem::is_regular_file(executable) ||
        timeout.count() < 1 || timeout > std::chrono::minutes{5} ||
        output_limit == 0 || output_limit > 64 * 1024 * 1024)
        throw std::runtime_error("PROCESS_ARGUMENTS_INVALID");
    std::wstring command = quote(executable.wstring());
    for (const auto &argument : arguments) {
        if (argument.find(L'\0') != std::wstring::npos)
            throw std::runtime_error("PROCESS_ARGUMENT_NUL");
        command += L" " + quote(argument);
    }
    Pipe out, err;
    out.create();
    err.create();
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), nullptr, TRUE};
    Handle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             &attributes, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!input.value || input.value == INVALID_HANDLE_VALUE)
        throw std::runtime_error("PROCESS_STDIN_CREATE_FAILED");
    std::array<HANDLE, 3> handles{input.value, out.writer.value, err.writer.value};
    AttributeList attributes_list;
    attributes_list.create(handles);
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = input.value;
    startup.StartupInfo.hStdOutput = out.writer.value;
    startup.StartupInfo.hStdError = err.writer.value;
    startup.lpAttributeList = attributes_list.list;
    Handle job;
    if (own_child_tree) {
        job.value = CreateJobObjectW(nullptr, nullptr);
        if (!job.value) throw std::runtime_error("PROCESS_JOB_CREATE_FAILED");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job.value, JobObjectExtendedLimitInformation,
                                      &limits, sizeof(limits)))
            throw std::runtime_error("PROCESS_JOB_CONFIG_FAILED");
    }
    PROCESS_INFORMATION process{};
    auto mutable_command = command;
    if (!CreateProcessW(executable.c_str(), mutable_command.data(), nullptr, nullptr, TRUE,
                         EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED | CREATE_NO_WINDOW,
                         nullptr, nullptr, &startup.StartupInfo, &process))
        throw std::runtime_error("PROCESS_START_FAILED");
    Handle child(process.hProcess), thread(process.hThread);
    if (own_child_tree && !AssignProcessToJobObject(job.value, child.value)) {
        TerminateProcess(child.value, 1);
        WaitForSingleObject(child.value, 1000);
        throw std::runtime_error("PROCESS_JOB_ASSIGN_FAILED");
    }
    if (ResumeThread(thread.value) == DWORD(-1)) {
        if (own_child_tree) TerminateJobObject(job.value, 1);
        else TerminateProcess(child.value, 1);
        WaitForSingleObject(child.value, 1000);
        throw std::runtime_error("PROCESS_RESUME_FAILED");
    }
    CloseHandle(out.writer.value);
    out.writer.value = nullptr;
    CloseHandle(err.writer.value);
    err.writer.value = nullptr;
    ProcessResult result;
    const auto deadline = Clock::now() + timeout;
    try {
        for (;;) {
            out.drain(output_limit);
            err.drain(output_limit);
            if (stop.stop_requested()) {
                result.state = ProcessState::Cancelled;
                break;
            }
            if (WaitForSingleObject(child.value, 0) == WAIT_OBJECT_0) {
                out.drain(output_limit);
                err.drain(output_limit);
                if (!GetExitCodeProcess(child.value, &result.exit_code))
                    throw std::runtime_error("PROCESS_EXIT_QUERY_FAILED");
                result.state = ProcessState::Exited;
                break;
            }
            if (Clock::now() >= deadline) {
                result.state = ProcessState::TimedOut;
                break;
            }
            std::this_thread::sleep_for(10ms);
        }
    } catch (...) {
        if (own_child_tree) TerminateJobObject(job.value, 1);
        else TerminateProcess(child.value, 1);
        WaitForSingleObject(child.value, 1000);
        throw;
    }
    if (result.state != ProcessState::Exited) {
        if (own_child_tree) TerminateJobObject(job.value, 1);
        else TerminateProcess(child.value, 1);
        if (WaitForSingleObject(child.value, 1000) != WAIT_OBJECT_0)
            throw std::runtime_error("PROCESS_CLEANUP_UNCONFIRMED");
    }
    result.stdout_bytes = std::move(out.bytes);
    result.stderr_bytes = std::move(err.bytes);
    return result;
}
} // namespace wvd::platform
