#include "workflow_repository.hpp"
#include "platform/windows/bundle_lease.hpp"

#include "games/wvd/tasks/author_workflow.hpp"
#include "platform/windows/file_digest.hpp"
#include "platform/windows/runtime_files.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <stdexcept>
#include <windows.h>

namespace wvd::storage {
namespace {
using J = nlohmann::json;
constexpr std::uintmax_t max_document_size = 1048576;

[[noreturn]] void fail(const std::string &code, const std::string &identity = {}) {
    throw std::runtime_error(identity.empty() ? code : code + ":" + identity);
}

bool identifier(const std::string &value) {
    if (value.empty() || value.size() > 64 ||
        !std::isalnum(static_cast<unsigned char>(value.front())))
        return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '_' || c == '-';
    });
}

void verify_plain_directory(const std::filesystem::path &root) {
    const auto attributes = GetFileAttributesW(root.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
        fail("WORKFLOW_REPOSITORY_ROOT_UNSAFE");
}

struct RepositoryLock {
    HANDLE handle{INVALID_HANDLE_VALUE};
    explicit RepositoryLock(const std::filesystem::path &root) {
        const auto path = root / L".workflow-repository.lock";
        const auto attributes = GetFileAttributesW(path.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES &&
            ((attributes & FILE_ATTRIBUTE_DIRECTORY) || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)))
            fail("WORKFLOW_REPOSITORY_LOCK_UNSAFE");
        handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                             FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
            fail("WORKFLOW_REPOSITORY_BUSY");
    }
    ~RepositoryLock() {
        if (handle != INVALID_HANDLE_VALUE)
            CloseHandle(handle);
    }
    RepositoryLock(const RepositoryLock &) = delete;
    RepositoryLock &operator=(const RepositoryLock &) = delete;
};

std::string read_plain_file(const std::filesystem::path &path, const std::string &flow_id) {
    const auto file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                  OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT |
                                      FILE_FLAG_SEQUENTIAL_SCAN,
                                  nullptr);
    if (file == INVALID_HANDLE_VALUE)
        fail("WORKFLOW_READ_FAILED", flow_id);
    FILE_ATTRIBUTE_TAG_INFO attributes{};
    LARGE_INTEGER file_size{};
    const bool valid = GetFileInformationByHandleEx(
                           file, FileAttributeTagInfo, &attributes, sizeof(attributes)) &&
                       !(attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                       !(attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
                       GetFileSizeEx(file, &file_size) && file_size.QuadPart > 0 &&
                       file_size.QuadPart <= static_cast<LONGLONG>(max_document_size);
    if (!valid) {
        CloseHandle(file);
        fail("WORKFLOW_FILE_UNSAFE", flow_id);
    }
    std::string text(static_cast<std::size_t>(file_size.QuadPart), '\0');
    std::size_t position{};
    bool success = true;
    while (position < text.size()) {
        DWORD read{};
        const auto requested = static_cast<DWORD>(
            std::min<std::size_t>(text.size() - position, 65536));
        if (!ReadFile(file, text.data() + position, requested, &read, nullptr) || !read) {
            success = false;
            break;
        }
        position += read;
    }
    CloseHandle(file);
    if (!success || position != text.size())
        fail("WORKFLOW_READ_FAILED", flow_id);
    return text;
}

std::string revision(J document) {
    document.erase("revision");
    const auto text = document.dump();
    return platform::bytes_sha256(
        {reinterpret_cast<const std::uint8_t *>(text.data()), text.size()});
}

J persisted(J document) {
    games::tasks::validate_author_workflow(document);
    document["revision"] = revision(document);
    games::tasks::validate_author_workflow(document);
    if (document.dump(2).size() > max_document_size)
        fail("AUTHOR_DOCUMENT_TOO_LARGE");
    return document;
}

void require_revision(const std::string &value) {
    if (value.size() != 64 ||
        !std::all_of(value.begin(), value.end(), [](unsigned char c) {
            return std::isdigit(c) || (c >= 'a' && c <= 'f');
        }))
        fail("WORKFLOW_REVISION_INVALID");
}
} // namespace

WorkflowRepository::WorkflowRepository(std::filesystem::path root)
    : root_(std::move(root)) {
    if (!root_.is_absolute() || root_ != root_.lexically_normal())
        fail("WORKFLOW_REPOSITORY_ROOT_INVALID");
    std::error_code error;
    std::filesystem::create_directories(root_, error);
    if (error)
        fail("WORKFLOW_REPOSITORY_CREATE_FAILED");
    verify_plain_directory(root_);
}

std::filesystem::path WorkflowRepository::path_for(const std::string &flow_id) const {
    if (!identifier(flow_id))
        fail("WORKFLOW_ID_INVALID", flow_id);
    const auto result = root_ / platform::BundleLease::checked_relative(flow_id + ".json");
    if (result.parent_path() != root_)
        fail("WORKFLOW_PATH_ESCAPE", flow_id);
    return result;
}

J WorkflowRepository::read_unlocked(const std::string &flow_id) const {
    verify_plain_directory(root_);
    const auto path = path_for(flow_id);
    const auto text = read_plain_file(path, flow_id);
    J document;
    try {
        document = J::parse(text, [](int depth, J::parse_event_t, J &) {
            if (depth > 32)
                fail("WORKFLOW_JSON_DEPTH_INVALID");
            return true;
        });
        games::tasks::validate_author_workflow(document);
    } catch (const J::exception &) {
        fail("WORKFLOW_JSON_INVALID", flow_id);
    }
    if (!document.contains("revision"))
        fail("WORKFLOW_REVISION_MISMATCH", flow_id);
    if (document.at("flow").at("id") != flow_id)
        fail("WORKFLOW_ID_MISMATCH", flow_id);
    if (document.at("revision") != revision(document))
        fail("WORKFLOW_REVISION_MISMATCH", flow_id);
    return document;
}

J WorkflowRepository::create(const J &document) {
    verify_plain_directory(root_);
    if (document.contains("revision"))
        fail("WORKFLOW_CREATE_REVISION_FORBIDDEN");
    games::tasks::validate_author_workflow(document);
    const auto flow_id = document.at("flow").at("id").get<std::string>();
    const auto path = path_for(flow_id);
    RepositoryLock lock(root_);
    if (std::filesystem::exists(path))
        fail("WORKFLOW_ALREADY_EXISTS", flow_id);
    const auto result = persisted(document);
    platform::atomic_write(path, result.dump(2), false);
    return result;
}

J WorkflowRepository::list() const {
    verify_plain_directory(root_);
    RepositoryLock lock(root_);
    J result = J::array();
    std::error_code error;
    for (std::filesystem::directory_iterator iterator(root_, error), end;
         !error && iterator != end; iterator.increment(error)) {
        const auto &entry = *iterator;
        if (entry.path().extension() != L".json")
            continue;
        const auto stem = entry.path().stem().string();
        if (!identifier(stem))
            fail("WORKFLOW_FILE_NAME_INVALID", stem);
        const auto document = read_unlocked(stem);
        result.push_back({{"id", stem},
                          {"name", document.at("flow").at("name")},
                          {"description", document.at("flow").at("description")},
                          {"revision", document.at("revision")},
                          {"node_count", document.at("nodes").size()},
                          {"edge_count", document.at("edges").size()}});
    }
    if (error)
        fail("WORKFLOW_LIST_FAILED");
    std::sort(result.begin(), result.end(), [](const J &left, const J &right) {
        return left.at("id").get<std::string>() < right.at("id").get<std::string>();
    });
    return result;
}

J WorkflowRepository::read(const std::string &flow_id) const {
    verify_plain_directory(root_);
    RepositoryLock lock(root_);
    return read_unlocked(flow_id);
}

J WorkflowRepository::copy(const std::string &source_id, const std::string &new_id,
                           const std::string &new_name) {
    verify_plain_directory(root_);
    const auto destination = path_for(new_id);
    RepositoryLock lock(root_);
    if (std::filesystem::exists(destination))
        fail("WORKFLOW_ALREADY_EXISTS", new_id);
    auto document = read_unlocked(source_id);
    document.erase("revision");
    document["flow"]["id"] = new_id;
    document["flow"]["name"] = new_name;
    const auto result = persisted(std::move(document));
    platform::atomic_write(destination, result.dump(2), false);
    return result;
}

J WorkflowRepository::compare_exchange(const std::string &flow_id,
                                       const std::string &expected_revision,
                                       const J &document) {
    verify_plain_directory(root_);
    require_revision(expected_revision);
    games::tasks::validate_author_workflow(document);
    if (!document.contains("revision") || document.at("revision") != expected_revision)
        fail("WORKFLOW_DRAFT_REVISION_MISMATCH", flow_id);
    if (document.at("flow").at("id") != flow_id)
        fail("WORKFLOW_ID_MISMATCH", flow_id);
    RepositoryLock lock(root_);
    const auto current = read_unlocked(flow_id);
    if (current.at("revision") != expected_revision)
        fail("WORKFLOW_CONFLICT", flow_id);
    auto next = document;
    next.erase("revision");
    next = persisted(std::move(next));
    platform::atomic_write(path_for(flow_id), next.dump(2), true);
    return next;
}

void WorkflowRepository::erase(const std::string &flow_id,
                               const std::string &expected_revision) {
    verify_plain_directory(root_);
    require_revision(expected_revision);
    RepositoryLock lock(root_);
    const auto current = read_unlocked(flow_id);
    if (current.at("revision") != expected_revision)
        fail("WORKFLOW_CONFLICT", flow_id);
    if (!DeleteFileW(path_for(flow_id).c_str()))
        fail("WORKFLOW_DELETE_FAILED", flow_id);
}

} // namespace wvd::storage
