#include "metadata_query.hpp"
#include "maafw/buffers.hpp"
#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>
#include <windows.h>

namespace wvd::platform {
namespace {
using Clock = std::chrono::steady_clock;
std::atomic<std::uint64_t> pipe_sequence{};
struct Handle {
    HANDLE value{};
    ~Handle() { reset(); }
    void reset(HANDLE next = nullptr) {
        if (value && value != INVALID_HANDLE_VALUE)
            CloseHandle(value);
        value = next;
    }
};
void check(bool ok, const char *code) {
    if (!ok)
        throw std::runtime_error(code);
}
DWORD remaining(Clock::time_point end) {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - Clock::now()).count();
    return ms <= 0 ? 0 : static_cast<DWORD>(std::min<std::int64_t>(ms, 100));
}
struct Pipe {
    Handle reader, writer, event;
    OVERLAPPED io{};
    std::array<char, 8192> block{};
    bool pending{}, ended{};
    std::string output;
    void create() {
        // FIRST_PIPE_INSTANCE + PID/时钟/序号确保独占；不为临时管道初始化 COM/RPC UUID 服务。
        auto path = L"\\\\.\\pipe\\wvd-metadata-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                    std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(++pipe_sequence);
        reader.reset(CreateNamedPipeW(
            path.c_str(),
            PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 8192,
            8192, 0, nullptr));
        check(reader.value != INVALID_HANDLE_VALUE, "METADATA_PIPE_CREATE");
        event.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        check(event.value != nullptr, "METADATA_EVENT_CREATE");
        io.hEvent = event.value;
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
        writer.reset(CreateFileW(path.c_str(), GENERIC_WRITE, 0, &sa, OPEN_EXISTING, 0, nullptr));
        check(writer.value != INVALID_HANDLE_VALUE, "METADATA_PIPE_CONNECT");
        ULONG client{};
        check(GetNamedPipeClientProcessId(reader.value, &client) && client == GetCurrentProcessId(),
              "METADATA_PIPE_CLIENT_MISMATCH");
    }
    void consume(DWORD count, std::size_t &total) {
        check(total + count <= 2 * 1024 * 1024, "METADATA_OUTPUT_LIMIT");
        output.append(block.data(), count);
        total += count;
    }
    void pump(std::size_t &total) {
        if (ended)
            return;
        DWORD count{};
        if (pending) {
            if (!GetOverlappedResult(reader.value, &io, &count, FALSE)) {
                auto error = GetLastError();
                if (error == ERROR_IO_INCOMPLETE)
                    return;
                pending = false;
                if (error == ERROR_BROKEN_PIPE || error == ERROR_OPERATION_ABORTED) {
                    ended = true;
                    return;
                }
                throw std::runtime_error("METADATA_PIPE_READ");
            }
            pending = false;
            consume(count, total);
        }
        ResetEvent(event.value);
        if (ReadFile(reader.value, block.data(), static_cast<DWORD>(block.size()), &count, &io))
            consume(count, total);
        else {
            auto error = GetLastError();
            if (error == ERROR_IO_PENDING)
                pending = true;
            else if (error == ERROR_BROKEN_PIPE)
                ended = true;
            else
                throw std::runtime_error("METADATA_PIPE_READ");
        }
    }
    bool drain_cancel() {
        if (!pending)
            return true;
        DWORD count{};
        if (GetOverlappedResult(reader.value, &io, &count, FALSE) ||
            GetLastError() != ERROR_IO_INCOMPLETE)
            pending = false;
        return !pending;
    }
};
struct Attributes {
    std::vector<std::uint8_t> storage;
    LPPROC_THREAD_ATTRIBUTE_LIST list{};
    ~Attributes() {
        if (list)
            DeleteProcThreadAttributeList(list);
    }
    void initialize(HANDLE *handles, std::size_t bytes) {
        SIZE_T size{};
        InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
        storage.resize(size);
        list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        if (!InitializeProcThreadAttributeList(list, 1, 0, &size)) {
            list = nullptr;
            throw std::runtime_error("METADATA_HANDLE_LIST");
        }
        check(UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles, bytes,
                                        nullptr, nullptr),
              "METADATA_HANDLE_LIST");
    }
};
} // namespace
struct MetadataQuery::Impl {
    Handle cancel_event, job, child, thread, input;
    std::array<Pipe, 2> pipes;
    bool started{}, assigned{}, cleaning{}, quiet{true};
    std::size_t total{};
    nlohmann::json record = nlohmann::json::object();
    Impl() {
        cancel_event.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        check(cancel_event.value != nullptr, "METADATA_CANCEL_EVENT");
    }
    bool tree_exited() {
        if (child.value && WaitForSingleObject(child.value, 0) != WAIT_OBJECT_0)
            return false;
        if (!assigned)
            return true;
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
        return QueryInformationJobObject(job.value, JobObjectBasicAccountingInformation,
                                         &accounting, sizeof(accounting), nullptr) &&
               accounting.ActiveProcesses == 0;
    }
};
namespace {
bool finish_impl(MetadataQuery::Impl &s, std::chrono::milliseconds budget) {
    if (s.quiet)
        return true;
    if (!s.cleaning) {
        s.cleaning = true;
        for (auto &p : s.pipes) {
            p.writer.reset();
            if (p.pending)
                CancelIoEx(p.reader.value, &p.io);
        }
        if (s.assigned)
            TerminateJobObject(s.job.value, 1);
        else if (s.child.value)
            TerminateProcess(s.child.value, 1);
    }
    const auto deadline = Clock::now() + budget;
    do {
        bool drained = true;
        for (auto &p : s.pipes)
            drained = p.drain_cancel() && drained;
        if (drained && s.tree_exited()) {
            for (auto &p : s.pipes) {
                p.reader.reset();
                p.event.reset();
            }
            s.child.reset();
            s.thread.reset();
            s.input.reset();
            s.job.reset();
            s.quiet = true;
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (Clock::now() < deadline);
    return false;
}

// 查询超时后的内核对象由进程级清理所有者继续持有。请求线程可以返回，
// 但 OVERLAPPED、管道和 Job 在真正静止前不会释放；进程退出时该线程也会 join。
class MetadataCleanupOwner {
  public:
    MetadataCleanupOwner() : worker_([this](std::stop_token stop) { run(stop); }) {}
    ~MetadataCleanupOwner() {
        worker_.request_stop();
        cv_.notify_all();
    }
    void adopt(std::unique_ptr<MetadataQuery::Impl> value) {
        std::lock_guard lock(mutex_);
        pending_.push_back(std::move(value));
        cv_.notify_one();
    }
  private:
    void run(std::stop_token stop) {
        for (;;) {
            std::unique_lock lock(mutex_);
            cv_.wait_for(lock, std::chrono::milliseconds(100),
                         [&] { return !pending_.empty() || stop.stop_requested(); });
            for (auto it = pending_.begin(); it != pending_.end();) {
                const bool quiet = finish_impl(**it, std::chrono::milliseconds(20));
                it = quiet ? pending_.erase(it) : std::next(it);
            }
            if (stop.stop_requested() && pending_.empty())
                return;
        }
    }
    std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<std::unique_ptr<MetadataQuery::Impl>> pending_;
    std::jthread worker_;
};
MetadataCleanupOwner &cleanup_owner() {
    static MetadataCleanupOwner owner;
    return owner;
}
} // namespace
MetadataQuery::MetadataQuery() : impl_(std::make_unique<Impl>()) {}
MetadataQuery::~MetadataQuery() {
    cancel();
    if (impl_ && !finish_cleanup(std::chrono::milliseconds(20)))
        cleanup_owner().adopt(std::move(impl_));
}
void MetadataQuery::cancel() { SetEvent(impl_->cancel_event.value); }
bool MetadataQuery::finish_cleanup(std::chrono::milliseconds budget) {
    return !impl_ || finish_impl(*impl_, budget);
}
nlohmann::json MetadataQuery::run(const std::filesystem::path &executable, int index,
                                  std::chrono::milliseconds budget) {
    auto &s = *impl_;
    check(!s.started, "METADATA_QUERY_ALREADY_STARTED");
    s.started = true;
    const auto begin = Clock::now(), deadline = begin + budget;
    std::string error;
    auto trace = [&](const char *stage) {
        DWORD count{};
        GetProcessHandleCount(GetCurrentProcess(), &count);
        s.record["handle_trace"][stage] = count;
    };
    trace("begin");
    DWORD exit_code = STILL_ACTIVE;
    try {
        check(budget.count() > 0 && budget <= std::chrono::seconds(10), "METADATA_BUDGET_INVALID");
        check(executable.is_absolute() && std::filesystem::is_regular_file(executable) &&
                  (executable.filename() == "MuMuManager.exe" ||
                   executable.filename() == "wvd_metadata_helper.exe") &&
                  index >= 0 && index <= 10000,
              "METADATA_COMMAND_INVALID");
        check(WaitForSingleObject(s.cancel_event.value, 0) != WAIT_OBJECT_0, "METADATA_CANCELLED");
        s.quiet = false;
        s.job.reset(CreateJobObjectW(nullptr, nullptr));
        check(s.job.value != nullptr, "METADATA_JOB_CREATE");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        check(SetInformationJobObject(s.job.value, JobObjectExtendedLimitInformation, &limits,
                                      sizeof(limits)),
              "METADATA_JOB_LIMIT");
        for (auto &p : s.pipes)
            p.create();
        trace("pipes_created");
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
        s.input.reset(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                                  OPEN_EXISTING, 0, nullptr));
        check(s.input.value != INVALID_HANDLE_VALUE, "METADATA_STDIN");
        HANDLE inherited[]{s.pipes[0].writer.value, s.pipes[1].writer.value, s.input.value};
        Attributes attributes;
        attributes.initialize(inherited, sizeof(inherited));
        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(startup);
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        startup.StartupInfo.wShowWindow = SW_HIDE;
        startup.StartupInfo.hStdOutput = inherited[0];
        startup.StartupInfo.hStdError = inherited[1];
        startup.StartupInfo.hStdInput = inherited[2];
        startup.lpAttributeList = attributes.list;
        auto path = std::filesystem::canonical(executable);
        trace("before_process");
        auto command = L"\"" + path.wstring() + L"\" info -v " + std::to_wstring(index);
        PROCESS_INFORMATION child{};
        check(CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT,
                             nullptr, path.parent_path().c_str(), &startup.StartupInfo, &child),
              "METADATA_PROCESS_START");
        s.child.value = child.hProcess;
        s.thread.value = child.hThread;
        trace("process_created");
        FILETIME created{}, ended{}, kernel{}, user{};
        check(GetProcessTimes(s.child.value, &created, &ended, &kernel, &user),
              "METADATA_PROCESS_IDENTITY");
        s.record.update(
            {{"pid", child.dwProcessId},
             {"image", maafw::utf8(path)},
             {"created", (std::uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime}});
        check(AssignProcessToJobObject(s.job.value, s.child.value), "METADATA_JOB_ASSIGN");
        s.assigned = true;
        s.record["job_owned"] = true;
        check(ResumeThread(s.thread.value) != DWORD(-1), "METADATA_PROCESS_RESUME");
        s.thread.reset();
        s.input.reset();
        for (auto &p : s.pipes)
            p.writer.reset();
        for (;;) {
            check(WaitForSingleObject(s.cancel_event.value, 0) != WAIT_OBJECT_0,
                  "METADATA_CANCELLED");
            check(Clock::now() < deadline, "METADATA_TIMEOUT");
            for (auto &p : s.pipes)
                p.pump(s.total);
            if (s.tree_exited() && s.pipes[0].ended && s.pipes[1].ended)
                break;
            HANDLE waits[]{s.cancel_event.value, s.pipes[0].event.value, s.pipes[1].event.value};
            // 每次只使用绝对截止点的剩余预算；同步读完的数据会在下一轮立即继续。
            DWORD delay =
                (s.pipes[0].pending || s.pipes[0].ended) && (s.pipes[1].pending || s.pipes[1].ended)
                    ? remaining(deadline)
                    : 0;
            check(WaitForMultipleObjects(3, waits, FALSE, delay) != WAIT_FAILED,
                  "METADATA_WAIT_FAILED");
        }
        check(GetExitCodeProcess(s.child.value, &exit_code), "METADATA_EXIT_UNAVAILABLE");
        check(exit_code == 0, "METADATA_NONZERO_EXIT");
        auto parsed = nlohmann::json::parse(s.pipes[0].output, nullptr, false);
        check(parsed.is_object(), "METADATA_JSON_INVALID");
        s.record["data"] = std::move(parsed);
    } catch (const std::exception &e) {
        error = e.what();
    }
    const bool quiet = finish_cleanup();
    trace("after_cleanup");
    s.record.update(
        {{"error", quiet ? error : "METADATA_CLEANUP_PENDING"},
         {"primary_error", error},
         {"success", quiet && error.empty()},
         {"quiescent", quiet},
         {"stdout", s.pipes[0].output},
         {"stderr", s.pipes[1].output},
         {"exit_code", exit_code},
         {"bytes", s.total},
         {"pending_io", int(s.pipes[0].pending) + int(s.pipes[1].pending)},
         {"handles_released", quiet},
         {"helper_exited", quiet},
         {"elapsed_ms", std::chrono::duration<double, std::milli>(Clock::now() - begin).count()}});
    return s.record;
}
} // namespace wvd::platform
