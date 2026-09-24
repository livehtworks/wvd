#include "mumu_capture.hpp"
#include "capture_protocol.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>
#include <thread>

namespace wvd::devices {
namespace {
using namespace std::chrono_literals;
std::wstring quote(const std::wstring &source) {
    std::wstring target = L"\"";
    unsigned slash{};
    for (wchar_t c : source) {
        if (c == L'\\') { ++slash; continue; }
        if (c == L'"') { target.append(slash * 2 + 1, L'\\'); target.push_back(c); }
        else { target.append(slash, L'\\'); target.push_back(c); }
        slash = 0;
    }
    target.append(slash * 2, L'\\');
    return target + L'"';
}
void discard(HANDLE &handle) {
    if (handle && handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
    handle = nullptr;
}
} // namespace

MumuCaptureClient::MumuCaptureClient(std::filesystem::path helper,
                                     std::filesystem::path install_root,
                                     std::filesystem::path ipc_library,
                                     int instance, std::wstring package)
    : helper_(std::move(helper)), root_(std::move(install_root)),
      library_(std::move(ipc_library)), package_(std::move(package)), instance_(instance) {
    if (!helper_.is_absolute() || !root_.is_absolute() || !library_.is_absolute() ||
        !std::filesystem::is_regular_file(helper_) ||
        !std::filesystem::is_regular_file(library_) || instance_ < 0 || instance_ > 10000 ||
        package_.empty()) throw std::runtime_error("MUMU_CAPTURE_CONFIG_INVALID");
}
MumuCaptureClient::~MumuCaptureClient() { close(); }

void MumuCaptureClient::start() {
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE parent_read{}, child_write{}, child_read{}, parent_write{};
    if (!CreatePipe(&parent_read, &child_write, &security, 0) ||
        !CreatePipe(&child_read, &parent_write, &security, 0)) {
        discard(parent_read); discard(child_write); discard(child_read); discard(parent_write);
        throw std::runtime_error("MUMU_CAPTURE_PIPE_FAILED");
    }
    auto cleanup = [&] {
        discard(parent_read); discard(child_write); discard(child_read); discard(parent_write);
    };
    if (!SetHandleInformation(parent_read, HANDLE_FLAG_INHERIT, 0) ||
        !SetHandleInformation(parent_write, HANDLE_FLAG_INHERIT, 0)) {
        cleanup(); throw std::runtime_error("MUMU_CAPTURE_PIPE_INHERIT_FAILED");
    }
    HANDLE error = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (error == INVALID_HANDLE_VALUE) { cleanup(); throw std::runtime_error("MUMU_CAPTURE_STDERR_FAILED"); }
    std::array<HANDLE, 3> inherited{child_read, child_write, error};
    SIZE_T size{};
    InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    std::vector<std::uint8_t> buffer(size);
    auto *attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(buffer.data());
    if (!InitializeProcThreadAttributeList(attributes, 1, 0, &size) ||
        !UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                   inherited.data(), sizeof(inherited), nullptr, nullptr)) {
        DeleteProcThreadAttributeList(attributes); discard(error); cleanup();
        throw std::runtime_error("MUMU_CAPTURE_HANDLE_LIST_FAILED");
    }
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = child_read;
    startup.StartupInfo.hStdOutput = child_write;
    startup.StartupInfo.hStdError = error;
    startup.lpAttributeList = attributes;
    std::wstring command = quote(helper_.wstring()) + L" " + quote(root_.wstring()) +
        L" " + quote(library_.wstring()) + L" " + std::to_wstring(instance_) +
        L" " + quote(package_);
    job_ = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job_ || !SetInformationJobObject(job_, JobObjectExtendedLimitInformation,
                                           &limits, sizeof(limits))) {
        DeleteProcThreadAttributeList(attributes); discard(error); cleanup(); close();
        throw std::runtime_error("MUMU_CAPTURE_JOB_FAILED");
    }
    PROCESS_INFORMATION child{};
    const bool created = CreateProcessW(helper_.c_str(), command.data(), nullptr, nullptr, TRUE,
                                         EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED |
                                             CREATE_NO_WINDOW,
                                         nullptr, nullptr, &startup.StartupInfo, &child);
    DeleteProcThreadAttributeList(attributes);
    discard(error);
    if (!created) { cleanup(); close(); throw std::runtime_error("MUMU_CAPTURE_START_FAILED"); }
    process_ = child.hProcess;
    bool assigned = AssignProcessToJobObject(job_, process_);
    bool resumed = assigned && ResumeThread(child.hThread) != DWORD(-1);
    CloseHandle(child.hThread);
    if (!resumed) { cleanup(); close(); throw std::runtime_error("MUMU_CAPTURE_JOB_ASSIGN_FAILED"); }
    discard(child_read); discard(child_write);
    input_ = parent_write; output_ = parent_read;
    ++generation_;
}

void MumuCaptureClient::read_exact(void *buffer, std::size_t remaining,
                                    std::chrono::steady_clock::time_point deadline,
                                    std::stop_token stop) {
    auto *cursor = static_cast<std::uint8_t *>(buffer);
    while (remaining) {
        if (stop.stop_requested()) throw std::runtime_error("MUMU_CAPTURE_CANCELLED");
        if (std::chrono::steady_clock::now() >= deadline)
            throw std::runtime_error("MUMU_CAPTURE_TIMEOUT");
        DWORD available{};
        if (!PeekNamedPipe(output_, nullptr, 0, nullptr, &available, nullptr))
            throw std::runtime_error("MUMU_CAPTURE_HOST_EXITED");
        if (available) {
            DWORD count{};
            const auto chunk = static_cast<DWORD>(std::min<std::size_t>(remaining, available));
            if (!ReadFile(output_, cursor, chunk, &count, nullptr) || !count)
                throw std::runtime_error("MUMU_CAPTURE_READ_FAILED");
            cursor += count; remaining -= count;
            continue;
        }
        if (WaitForSingleObject(process_, 0) == WAIT_OBJECT_0)
            throw std::runtime_error("MUMU_CAPTURE_HOST_EXITED");
        std::this_thread::sleep_for(5ms);
    }
}

MumuPixels MumuCaptureClient::capture(std::chrono::milliseconds timeout,
                                      std::stop_token stop) {
    if (timeout <= 0ms || timeout > 30000ms)
        throw std::runtime_error("MUMU_CAPTURE_TIMEOUT_INVALID");
    if (stop.stop_requested()) throw std::runtime_error("MUMU_CAPTURE_CANCELLED");
    if (!process_) start();
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    capture_protocol::Request request;
    request.sequence = ++sequence_;
    DWORD written{};
    if (!WriteFile(input_, &request, sizeof(request), &written, nullptr) ||
        written != sizeof(request)) {
        close(); throw std::runtime_error("MUMU_CAPTURE_SEND_FAILED");
    }
    try {
        capture_protocol::Reply reply;
        read_exact(&reply, sizeof(reply), deadline, stop);
        if (reply.marker != capture_protocol::magic ||
            reply.schema != capture_protocol::version || reply.sequence != request.sequence ||
            reply.status != 0 || reply.width == 0 || reply.height == 0 ||
            reply.width > capture_protocol::max_dimension ||
            reply.height > capture_protocol::max_dimension ||
            reply.stride != reply.width * 4 ||
            reply.byte_count != reply.stride * reply.height)
            throw std::runtime_error("MUMU_CAPTURE_REPLY_INVALID");
        MumuPixels pixels;
        pixels.width = static_cast<int>(reply.width);
        pixels.height = static_cast<int>(reply.height);
        pixels.display_id = static_cast<int>(reply.display_id);
        pixels.rgba_bottom_up.resize(reply.byte_count);
        read_exact(pixels.rgba_bottom_up.data(), pixels.rgba_bottom_up.size(), deadline, stop);
        return pixels;
    } catch (...) { close(); throw; }
}

void MumuCaptureClient::close() {
    if (input_ && process_ && WaitForSingleObject(process_, 0) == WAIT_TIMEOUT) {
        capture_protocol::Request request;
        request.operation = capture_protocol::quit;
        DWORD written{};
        WriteFile(input_, &request, sizeof(request), &written, nullptr);
    }
    if (process_ && WaitForSingleObject(process_, 250) != WAIT_OBJECT_0 && job_)
        TerminateJobObject(job_, 1);
    if (process_) WaitForSingleObject(process_, 1000);
    discard(input_); discard(output_); discard(process_); discard(job_);
}
} // namespace wvd::devices
