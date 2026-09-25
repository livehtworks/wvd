#include "run_store.hpp"
#include "platform/windows/runtime_files.hpp"
#include "platform/windows/file_digest.hpp"
#include <opencv2/imgcodecs.hpp>
#include <algorithm>
#include <array>
#include <fstream>
#include <cstdio>
#include <windows.h>

namespace wvd::storage {
using J = nlohmann::json;
namespace {
void diagnostic_require(bool condition, const char *error) {
    if (!condition) throw std::runtime_error(error);
}
std::string diagnostic_error(const char *text) {
    const std::string value(text);
    if (!value.empty() && value.size() <= 256 &&
        std::all_of(value.begin(), value.end(), [](unsigned char c) {
            return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        })) return value;
    return "DIAGNOSTIC_SAVE_FAILED";
}
// 不跟随junction/symlink，持有各级目录的非共享写/删除句柄直至本次原子写结束。
struct DiagnosticDirectories {
    std::vector<HANDLE> handles;
    ~DiagnosticDirectories() { for (auto handle : handles) CloseHandle(handle); }
    BY_HANDLE_FILE_INFORMATION hold(const std::filesystem::path &path) {
        const auto handle = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        diagnostic_require(handle != INVALID_HANDLE_VALUE, "DIAGNOSTIC_DIRECTORY_LOCK_FAILED");
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(handle, &info) ||
            !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            CloseHandle(handle);
            throw std::runtime_error("DIAGNOSTIC_REPARSE_REJECTED");
        }
        try { handles.push_back(handle); }
        catch (...) { CloseHandle(handle); throw; }
        return info;
    }
    void ancestors(const std::filesystem::path &directory) {
        const auto absolute = std::filesystem::absolute(directory).lexically_normal();
        auto current = absolute.root_path();
        hold(current);
        for (const auto &part : absolute.relative_path()) {
            diagnostic_require(handles.size() < 64, "DIAGNOSTIC_PATH_DEPTH");
            current /= part;
            hold(current);
        }
    }
};
std::vector<std::uint8_t> diagnostic_png(const contracts::FrameEnvelope &frame,
                                         std::size_t limit) {
    auto bytes = frame.encoded_image;
    const auto size = frame.identity.recognition_size;
    if (bytes.empty() && frame.raw_bgr) {
        diagnostic_require(size.width > 0 && size.height > 0 &&
            frame.raw_bgr->size() == static_cast<std::uint64_t>(size.width) * size.height * 3,
            "DIAGNOSTIC_RAW_FRAME_INVALID");
        const cv::Mat image(size.height, size.width, CV_8UC3,
            const_cast<std::uint8_t *>(frame.raw_bgr->data()));
        diagnostic_require(cv::imencode(".png", image, bytes),
                           "DIAGNOSTIC_PNG_ENCODE_FAILED");
    }
    constexpr std::array<std::uint8_t, 8> signature{137, 80, 78, 71, 13, 10, 26, 10};
    diagnostic_require(bytes.size() <= limit, "DIAGNOSTIC_FRAME_BYTES_EXCEEDED");
    diagnostic_require(bytes.size() >= 33 &&
        std::equal(signature.begin(), signature.end(), bytes.begin()), "DIAGNOSTIC_NOT_PNG");
    // 先核对PNG固定IHDR，限制尺寸后再由OpenCV解码验证像素。
    const auto u32 = [&](std::size_t offset) {
        return (std::uint32_t(bytes[offset]) << 24) | (std::uint32_t(bytes[offset + 1]) << 16) |
               (std::uint32_t(bytes[offset + 2]) << 8) | bytes[offset + 3];
    };
    diagnostic_require(u32(8) == 13 && bytes[12] == 'I' && bytes[13] == 'H' &&
        bytes[14] == 'D' && bytes[15] == 'R' && size.width > 0 && size.height > 0 &&
        size.width <= 4096 && size.height <= 4096 && u32(16) == std::uint32_t(size.width) &&
        u32(20) == std::uint32_t(size.height), "DIAGNOSTIC_PNG_SIZE_INVALID");
    const auto image = cv::imdecode(bytes, cv::IMREAD_COLOR);
    diagnostic_require(!image.empty() && image.type() == CV_8UC3 &&
        image.cols == size.width && image.rows == size.height,
        "DIAGNOSTIC_PNG_DECODE_FAILED");
    return bytes;
}
J event_record(const std::string &instance, std::uint64_t run, std::uint64_t generation,
               std::uint64_t seq, std::string type, J payload) {
    auto node = payload.is_object() ? payload.value("node_id", payload.value("node", J(nullptr))) : J(nullptr);
    if (!node.is_string() && payload.is_object() && payload.contains("name") &&
        payload.at("name").is_string())
        node = payload.at("name");
    auto outcome = payload.is_object()
                       ? payload.value("outcome", payload.value("state", J(nullptr)))
                       : J(nullptr);
    return {{"server_instance_id", instance},
            {"run_id", run},
            {"session_generation", generation},
            {"seq", seq},
            {"monotonic_time", std::chrono::duration_cast<std::chrono::nanoseconds>(
                                   std::chrono::steady_clock::now().time_since_epoch())
                                   .count()},
            {"type", std::move(type)},
            {"node_id", std::move(node)},
            {"source_path", payload.is_object() ? payload.value("source_path", J(nullptr)) : J(nullptr)},
            {"outcome", std::move(outcome)},
            {"payload", std::move(payload)}};
}
} // namespace
EventJournal::EventJournal(std::string instance, std::uint64_t run, std::size_t capacity)
    : instance_(std::move(instance)), run_(run), capacity_(capacity) {
    if (capacity < 8 || capacity > 65536)
        throw std::runtime_error("EVENT_CAPACITY_INVALID");
}
std::uint64_t EventJournal::emit(std::uint64_t generation, std::string type, J payload,
                                 bool critical) {
    if (type.size() > 256 || payload.dump().size() > 65536)
        throw std::runtime_error("EVENT_TOO_LARGE");
    std::lock_guard lock(mutex_);
    if (terminal_)
        throw std::runtime_error("JOURNAL_ALREADY_TERMINAL");
    if (committing_)
        throw std::runtime_error("JOURNAL_COMMIT_IN_PROGRESS");
    const auto sequence = ++sequence_;
    if (events_.size() == capacity_) {
        auto discard = std::find_if(events_.begin(), events_.end(),
                                    [](const auto &event) { return !event.critical; });
        if (discard == events_.end()) {
            dropped_through_ = sequence;
            if (critical)
                throw std::runtime_error("CRITICAL_EVENT_CAPACITY_EXCEEDED");
            return sequence;
        }
        dropped_through_ = std::max(dropped_through_, discard->seq);
        events_.erase(discard);
    }
    events_.push_back(
        {sequence, critical,
         event_record(instance_, run_, generation, sequence, std::move(type), std::move(payload))});
    return sequence;
}
J EventJournal::read(std::uint64_t after) const {
    std::lock_guard lock(mutex_);
    J rows = J::array();
    for (const auto &event : events_)
        if (event.seq > after)
            rows.push_back(event.value);
    return {
        {"last_seq", sequence_}, {"resync_required", after < dropped_through_}, {"events", rows}};
}
void EventJournal::commit_terminal(std::uint64_t generation, J payload,
                                   const std::function<void(const J &)> &persist) {
    std::unique_lock lock(mutex_);
    if (terminal_)
        throw std::runtime_error("JOURNAL_ALREADY_TERMINAL");
    if (committing_)
        throw std::runtime_error("JOURNAL_COMMIT_IN_PROGRESS");
    // 额外保留一个终态槽位；先构造完整提交内容，原子落盘成功后才发布到可读事件流。
    // 因而常规事件挤满、写盘失败或进程中断都不会产生未提交的 Completed 事件。
    auto committed = events_;
    const auto seq = sequence_ + 1;
    committed.push_back(
        {seq, true,
         event_record(instance_, run_, generation, seq, "run.terminal", std::move(payload))});
    J rows = J::array();
    for (const auto &event : committed)
        rows.push_back(event.value);
    J pending{
        {"last_seq", seq}, {"resync_required", dropped_through_ > 0}, {"events", std::move(rows)}};
    committing_ = true;
    lock.unlock();
    try {
        persist(pending);
    } catch (...) {
        lock.lock();
        committing_ = false;
        throw;
    }
    lock.lock();
    events_.swap(committed);
    sequence_ = seq;
    terminal_ = true;
    committing_ = false;
}
J snapshot_json(const contracts::RunSnapshot &s) {
    J result{{"run_id", s.run_id},
            {"generation", s.generation},
            {"state", contracts::name(s.state)},
            {"reason", s.reason},
            {"storage_error", s.storage_error},
            {"secondary_errors", s.secondary_errors},
            {"sessions", s.sessions},
            {"business", s.business},
            {"completed_business_units", s.completed_business_units},
            {"quiescent", s.quiescent},
            {"result_saved", s.result_saved},
            {"inputs",
             {{"attempted", s.inputs.attempted},
              {"accepted", s.inputs.accepted},
              {"rejected", s.inputs.rejected},
              {"backend_called", s.inputs.backend_called},
              {"cleanup_called", s.inputs.cleanup_called}}}};
    if (!s.outcome_category.empty()) result["outcome_category"] = s.outcome_category;
    if (!s.active_event.is_null()) result["active_event"] = s.active_event;
    if (!s.execution.is_null()) result["execution"] = s.execution;
    if (!s.unresolved_inputs.empty()) result["unresolved_inputs"] = s.unresolved_inputs;
    return result;
}
RunStore::RunStore(const std::filesystem::path &root, const std::string &instance,
                   std::uint64_t run, const J &definition,
                   std::shared_ptr<const contracts::MonotonicClock> diagnostic_clock,
                   DiagnosticLimits limits)
    : instance_(instance), run_(run), definition_(definition),
      diagnostic_clock_(std::move(diagnostic_clock)), diagnostic_limits_(limits) {
    diagnostic_require(diagnostic_clock_ && limits.rewards > 0 && limits.rewards <= 128 &&
        limits.failures > 0 && limits.failures <= 32 && limits.frame_bytes > 0 &&
        limits.frame_bytes <= 8 * 1024 * 1024, "DIAGNOSTIC_LIMITS_INVALID");
    // root 是调用者明确指定的新数据根；只新建本实例/本运行目录，既有同名目录不接管。
    auto parent = root / instance;
    std::filesystem::create_directories(parent);
    directory_ = parent / std::to_string(run);
    recent_directory_ = root.parent_path() / "recent-frames";
    if (!std::filesystem::create_directory(directory_))
        throw std::runtime_error("RUN_DIRECTORY_EXISTS");
    platform::atomic_write(
        directory_ / "run.json",
        J{{"schema", 1}, {"instance", instance}, {"run_id", run}, {"definition", definition},
          {"diagnostic_policy", {{"schema", 1}, {"reward_limit", limits.rewards},
              {"failure_limit", limits.failures}, {"frame_bytes_limit", limits.frame_bytes},
              {"reserved_bytes_limit", std::uint64_t(limits.rewards + limits.failures) * limits.frame_bytes},
              {"default_interval_seconds", 60}, {"pause_interval_seconds", 120}}}}.dump(
            2),
        false);
}
bool RunStore::save_recent_frame(const contracts::FrameEnvelope &frame) {
    std::lock_guard lock(diagnostic_mutex_);
    const auto now = diagnostic_clock_->now();
    if (recent_last_ != contracts::MonotonicClock::TimePoint{} &&
        now - recent_last_ < std::chrono::seconds{15}) return false;
    recent_last_ = now;
    const auto size = frame.identity.recognition_size;
    if (size.width != 900 || size.height != 1600) return false;
    cv::Mat image;
    if (frame.raw_bgr && frame.raw_bgr->size() == std::size_t(size.width) * size.height * 3)
        image = cv::Mat(size.height, size.width, CV_8UC3,
            const_cast<std::uint8_t *>(frame.raw_bgr->data()));
    else if (!frame.encoded_image.empty())
        image = cv::imdecode(frame.encoded_image, cv::IMREAD_COLOR);
    if (image.empty() || image.cols != size.width || image.rows != size.height)
        throw std::runtime_error("RECENT_FRAME_INVALID");
    std::vector<std::uint8_t> jpeg;
    if (!cv::imencode(".jpg", image, jpeg, {cv::IMWRITE_JPEG_QUALITY, 72}))
        throw std::runtime_error("RECENT_FRAME_ENCODE_FAILED");
    if (jpeg.size() > 2 * 1024 * 1024) throw std::runtime_error("RECENT_FRAME_TOO_LARGE");
    std::filesystem::create_directories(recent_directory_);
    if (std::filesystem::is_symlink(recent_directory_))
        throw std::runtime_error("RECENT_FRAME_DIRECTORY_LINK");
    SYSTEMTIME utc{};
    GetSystemTime(&utc);
    char name[100]{};
    std::snprintf(name, sizeof(name), "%04u%02u%02uT%02u%02u%02u%03u_r%llu_f%llu.jpg",
        utc.wYear, utc.wMonth, utc.wDay, utc.wHour, utc.wMinute, utc.wSecond,
        utc.wMilliseconds, static_cast<unsigned long long>(run_),
        static_cast<unsigned long long>(frame.identity.frame_id));
    platform::atomic_write(recent_directory_ / name,
        std::string(reinterpret_cast<const char *>(jpeg.data()), jpeg.size()), false);
    std::vector<std::filesystem::directory_entry> files;
    std::uintmax_t total_bytes = 0;
    for (const auto &entry : std::filesystem::directory_iterator(recent_directory_)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".jpg") continue;
        total_bytes += entry.file_size();
        files.push_back(entry);
    }
    std::sort(files.begin(), files.end(), [](const auto &a, const auto &b) {
        return a.path().filename() < b.path().filename();
    });
    constexpr std::uintmax_t byte_limit = 128ULL * 1024 * 1024;
    for (std::size_t i = 0; i < files.size() &&
        (files.size() - i > 240 || total_bytes > byte_limit); ++i) {
        total_bytes -= files[i].file_size();
        std::filesystem::remove(files[i].path());
    }
    return true;
}
J RunStore::save_diagnostic(const contracts::FrameEnvelope *frame, const DiagnosticRequest &request) {
    std::lock_guard lock(diagnostic_mutex_);
    const auto skipped = [](const char *status) { return J{{"status", status}}; };
    if (diagnostic_closed_) return skipped("closed");
    const bool reward = request.stage == "reward";
    if (request.run_id != run_ || !request.generation || request.task_id <= 0 || request.depth < 0 ||
        request.node.empty() || request.node.size() > 256 || request.reason.empty() ||
        request.reason.size() > 256 || request.operation_id.size() > 512 || request.error.size() > 256 ||
        (reward && request.operation_id.empty()) ||
        (request.stage != "reward" && request.stage != "pre_action" &&
         request.stage != "postcondition" && request.stage != "recovery_entry")) {
        ++diagnostic_unrecorded_;
        return skipped("invalid_request");
    }
    if (reward && diagnostic_operations_.contains(request.operation_id)) {
        ++diagnostic_duplicates_;
        return skipped("duplicate");
    }
    const auto now = diagnostic_clock_->now();
    const auto interval = std::chrono::seconds(request.reason.find("pause") != std::string::npos ? 120 : 60);
    bool clock_backwards = false;
    if (!reward) {
        const auto found = diagnostic_times_.find(request.reason);
        clock_backwards = found != diagnostic_times_.end() && now < found->second;
        if (found != diagnostic_times_.end() && now >= found->second && now - found->second < interval) {
            ++diagnostic_throttled_;
            return skipped("throttled");
        }
    }
    auto &attempts = reward ? diagnostic_rewards_ : diagnostic_failures_;
    if (attempts >= (reward ? diagnostic_limits_.rewards : diagnostic_limits_.failures)) {
        ++diagnostic_quota_;
        return skipped("quota_exceeded");
    }
    // 失败/tmp也占一次完整单帧预算，不自动重试，不让失败绕过数量/字节上限。
    ++attempts;
    if (reward) diagnostic_operations_.insert(request.operation_id);
    else diagnostic_times_[request.reason] = now;
    J entry{{"id", diagnostic_rewards_ + diagnostic_failures_}, {"status", "failed"},
        {"instance", instance_}, {"run_id", run_}, {"generation", request.generation},
        {"unit_index", request.unit_index}, {"task_id", request.task_id}, {"depth", request.depth},
        {"node", request.node}, {"reason", request.reason}, {"stage", request.stage},
        {"operation_id", request.operation_id}};
    try {
        diagnostic_require(!clock_backwards, "DIAGNOSTIC_CLOCK_MOVED_BACKWARD");
        diagnostic_require(request.error.empty(), request.error.c_str());
        diagnostic_require(frame != nullptr, "DIAGNOSTIC_FRAME_UNAVAILABLE");
        const auto &id = frame->identity;
        diagnostic_require(id.generation == request.generation && id.frame_id > 0 &&
            id.device_id == definition_.at("device_id").get<std::string>() &&
            id.game_id == definition_.at("game_id").get<std::string>() &&
            id.pack_revision == definition_.at("pack_revision").get<std::string>() && id.color_format == "BGR8" &&
            id.raw_size.width > 0 && id.raw_size.height > 0 &&
            id.raw_size.width <= 16384 && id.raw_size.height <= 16384 &&
            (definition_.value("observed_read_only_viewport", false) ||
                id.viewport_id == definition_.at("viewport").get<std::string>()) &&
            id.captured_at.time_since_epoch().count() > 0 && id.captured_at <= std::chrono::steady_clock::now(),
            "DIAGNOSTIC_FRAME_IDENTITY_INVALID");
        entry["frame"] = {{"frame_id", id.frame_id}, {"generation", id.generation},
            {"device_id", id.device_id}, {"game_id", id.game_id}, {"pack_revision", id.pack_revision},
            {"viewport_id", id.viewport_id}, {"connection_generation", id.connection_generation},
            {"action_epoch", id.action_epoch}, {"raw_size", {id.raw_size.width, id.raw_size.height}},
            {"recognition_size", {id.recognition_size.width, id.recognition_size.height}},
            {"color_format", id.color_format}, {"backend", id.backend},
            {"foreground_application", id.foreground_application},
            {"captured_at_ns", std::chrono::duration_cast<std::chrono::nanoseconds>(id.captured_at.time_since_epoch()).count()},
            {"age_at_submit_ms", std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - id.captured_at).count()},
            {"input_authorization", false}};
        const auto image_bytes = diagnostic_png(*frame, diagnostic_limits_.frame_bytes);
        DiagnosticDirectories directories;
        directories.ancestors(directory_);
        const auto folder = directory_ / "diagnostics";
        if (!diagnostic_directory_created_) {
            diagnostic_require(std::filesystem::create_directory(folder), "DIAGNOSTIC_DIRECTORY_EXISTS");
            const auto info = directories.hold(folder);
            diagnostic_volume_ = info.dwVolumeSerialNumber;
            diagnostic_directory_id_ = (std::uint64_t(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
            diagnostic_directory_created_ = true;
        } else {
            const auto info = directories.hold(folder);
            diagnostic_require(info.dwVolumeSerialNumber == diagnostic_volume_ &&
                ((std::uint64_t(info.nFileIndexHigh) << 32) | info.nFileIndexLow) == diagnostic_directory_id_,
                "DIAGNOSTIC_DIRECTORY_CHANGED");
        }
        const auto relative = "diagnostics/" + std::to_string(entry.at("id").get<std::uint64_t>()) + ".png";
        const auto hash = platform::bytes_sha256(image_bytes);
        const std::string content(image_bytes.begin(), image_bytes.end());
        platform::atomic_write(directory_ / relative, content, false);
        entry["path"] = relative;
        entry["sha256"] = hash;
        entry["bytes"] = content.size();
        entry["status"] = "saved";
        diagnostic_bytes_ += content.size();
    } catch (const std::exception &error) {
        entry["error"] = diagnostic_error(error.what());
        ++diagnostic_failed_;
        if (!frame) ++diagnostic_unavailable_;
    } catch (...) {
        entry["error"] = "DIAGNOSTIC_SAVE_EXCEPTION";
        ++diagnostic_failed_;
    }
    diagnostic_entries_.push_back(entry);
    return entry;
}
J RunStore::diagnostic_summary() const {
    std::lock_guard lock(diagnostic_mutex_);
    return {{"schema", 1}, {"entries", diagnostic_entries_}, {"bytes_saved", diagnostic_bytes_},
        {"reserved_bytes", (diagnostic_rewards_ + diagnostic_failures_) * diagnostic_limits_.frame_bytes},
        {"reward_attempts", diagnostic_rewards_}, {"failure_attempts", diagnostic_failures_},
        {"failed", diagnostic_failed_}, {"unavailable", diagnostic_unavailable_},
        {"throttled", diagnostic_throttled_}, {"duplicates", diagnostic_duplicates_},
        {"quota_exceeded", diagnostic_quota_}, {"unrecorded", diagnostic_unrecorded_},
        {"complete", diagnostic_failed_ == 0 && diagnostic_quota_ == 0 && diagnostic_unrecorded_ == 0}};
}
void RunStore::note_diagnostic_hook_failure() noexcept {
    try {
        std::lock_guard lock(diagnostic_mutex_);
        ++diagnostic_unrecorded_;
    } catch (...) {}
}
void RunStore::save_events(const EventJournal &events) {
    platform::atomic_write(directory_ / "events.json", events.read().dump(2), true);
}
void RunStore::save_terminal(const contracts::RunSnapshot &snapshot,
                             const contracts::SessionResult &session, const J &events) {
    if (saved_)
        throw std::runtime_error("TERMINAL_ALREADY_SAVED");
    J document = snapshot_json(snapshot);
    // 协调器在全部Session join后调用；此后拒绝任何迟到图片，索引与终态同次提交。
    {
        std::lock_guard lock(diagnostic_mutex_);
        diagnostic_closed_ = true;
    }
    document["diagnostics"] = diagnostic_summary();
    document["result_saved"] = true;
    document["events"] = events;
    document["root_terminal"] = {{"generation", session.terminal.generation},
                                 {"source_path", session.terminal.source_path}};
    platform::atomic_write(directory_ / "result.json", document.dump(2), false);
    saved_ = true;
}
J RunStore::read_summary(const std::filesystem::path &directory) {
    std::ifstream run(directory / "run.json");
    J definition;
    if (!run || !(run >> definition) || definition.at("schema") != 1)
        throw std::runtime_error("RUN_RECORD_INVALID");
    if (!std::filesystem::exists(directory / "result.json"))
        return {{"state", "Interrupted"},
                {"reason", "NO_COMMITTED_TERMINAL"},
                {"resume_allowed", false}};
    std::ifstream result(directory / "result.json");
    J value;
    result >> value;
    return value;
}
} // namespace wvd::storage
