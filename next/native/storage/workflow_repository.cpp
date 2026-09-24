#include "workflow_repository.hpp"
#include "authoring/document_parameters.hpp"
#include "platform/windows/bundle_lease.hpp"

#include "authoring/workflow_validator.hpp"
#include "platform/windows/file_digest.hpp"
#include "platform/windows/runtime_files.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <windows.h>

namespace wvd::storage {
namespace {
using J = nlohmann::json;
constexpr std::uintmax_t max_document_size = 1048576;
// 8f61540 随包内置源的文档摘要；只用于证明旧安装确实未改，不是第二份可执行流程。
const std::map<std::string, std::string> historical_builtins{
    {"city-enter-guild", "15868443375eaa256511a6c95ade747bbc3b9cd18653f21f717776e41702ee99"},
    {"guild-enter-commissions", "b6308ba31aa1b2a6be8051cd97e87b58cb3c6ca908e616d83dd67a36f7cad222"},
    {"guild-open-bounties", "20f445cec5f4421d9c0e0013fb7b0875f1774e7733f200d25e4030ea4dcc6d0a"},
    {"guild-open-bounty-page", "3dabcd6c877d7a0872bb9c3c4cf968a4e6d0d51b47a8744cfca47dcaa9549b95"},
    {"guild-claim-first-bounty", "4813892dbe29686eb5ddfc9a650332ae1b3450f40984aa9d4a91875e64e719dd"},
    {"city-open-ruins", "f1a9a9ef88356494927f14c79346654d60da5ece3b7d4479f3b2d88cdadfece7"},
    {"wheel-jump", "24a779d38979d32a3b7970f0b465c833f50a86564fafbbc410c124c0b4174e7b"},
    {"wheel-open", "4e23c829ec7487fbd20424a8a50cea1fa8825ecef6faa8085d7a383832534d36"},
    {"wheel-select-target", "799e75c968ef75e33d674a8c1128f8f9c8cc97dd871a01e90968c57db18b0587"},
    {"wheel-reset", "9e46b181a80824e6964bc2081a8b21b16b6bd9b22bece13eff59d6150f233da9"},
    {"ore-reset-to-royal", "0cf3f1e48cb56948bb91460ec468b0fed4088fb5690d88d56930365326830ae3"},
    {"bounty-refresh", "f9b39289f75ddb3198634f75d2e3434c5b6f2f03fb028102a0f48c6d61a12869"},
};

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
    authoring::validate_author_workflow(document);
    document["revision"] = revision(document);
    authoring::validate_author_workflow(document);
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

std::filesystem::path WorkflowRepository::builtin_path_for(const std::string &flow_id) const {
    (void)path_for(flow_id);
    return root_ / ".builtin" / (flow_id + ".json");
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
        authoring::validate_author_workflow(document);
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
    authoring::validate_author_workflow(document);
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
                          {"edge_count", document.at("edges").size()},
                          {"interface", document.value("interface", J::object())},
                          {"slots", authoring::slot_names(document)},
                          {"resource_locale", document.at("execution").value("resource_locale", std::string{})}});
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

J WorkflowRepository::snapshot_closure(const J &root) const {
    verify_plain_directory(root_);
    RepositoryLock lock(root_);
    const auto root_id = root.at("flow").at("id").get<std::string>();
    const auto persisted_root = read_unlocked(root_id);
    if (root != persisted_root) fail("WORKFLOW_CONFLICT", root_id);
    J documents = J::object();
    std::size_t bytes{};
    std::map<std::string, std::vector<std::string>> graph;
    const auto load = [&](auto &&self, const std::string &id, unsigned depth) -> void {
        if (documents.contains(id)) return;
        if (depth >= 8) fail("FLOW_REFERENCE_DEPTH", id);
        if (documents.size() >= 1024) fail("FLOW_SNAPSHOT_LIMIT");
        auto document = id == root_id ? root : read_unlocked(id);
        bytes += document.dump().size();
        if (bytes > 16 * 1024 * 1024) fail("FLOW_SNAPSHOT_BYTES_LIMIT");
        documents[id] = document;
        const auto references = authoring::referenced_flows(document);
        graph[id] = std::vector<std::string>(references.begin(), references.end());
        for (const auto &next : references) self(self, next, depth + 1);
    };
    load(load, root_id, 0);
    authoring::validate_call_graph(graph, root_id);
    return documents;
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
    authoring::validate_author_workflow(document);
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
    for (const auto &entry : std::filesystem::directory_iterator(root_)) {
        if (entry.path().extension() != L".json") continue;
        const auto id = entry.path().stem().string();
        if (id == flow_id) continue;
        const auto references = authoring::referenced_flows(read_unlocked(id));
        if (references.contains(flow_id)) fail("WORKFLOW_STILL_REFERENCED", id);
    }
    if (!DeleteFileW(path_for(flow_id).c_str()))
        fail("WORKFLOW_DELETE_FAILED", flow_id);
}

void WorkflowRepository::register_builtin(const J &document, const std::string &local_revision) {
    const auto id = document.at("flow").at("id").get<std::string>();
    require_revision(local_revision);
    const auto path = builtin_path_for(id);
    RepositoryLock lock(root_);
    if (std::filesystem::exists(path)) return;
    const auto current = read_unlocked(id);
    const auto builtin = persisted(document);
    if (current.at("revision") != local_revision || current != builtin)
        fail("BUILTIN_SOURCE_UNCONFIRMED", id);
    std::filesystem::create_directories(path.parent_path());
    verify_plain_directory(path.parent_path());
    J metadata{{"schema", 1}, {"flow_id", id},
               {"import_baseline", builtin.at("revision")},
               {"accepted_builtin", builtin.at("revision")},
               {"local_revision", local_revision}};
    platform::atomic_write(path, metadata.dump(2), false);
}

J WorkflowRepository::inspect_builtin(const J &document) const {
    const auto id = document.at("flow").at("id").get<std::string>();
    const auto path = builtin_path_for(id);
    const auto builtin = persisted(document);
    RepositoryLock lock(root_);
    const auto current = read_unlocked(id);
    J result{{"flow_id", id}, {"local_revision", current.at("revision")},
             {"builtin_revision", builtin.at("revision")}, {"current", current},
             {"builtin", builtin}, {"affected_references", J::array()}};
    if (!std::filesystem::exists(path)) {
        const auto known = historical_builtins.find(id);
        const auto local = current.at("revision").get<std::string>();
        result["status"] = local == builtin.at("revision").get<std::string>() ? "current" :
            known != historical_builtins.end() && local == known->second
                ? "update_available" : "source_unknown";
        if (known != historical_builtins.end() && local == known->second)
            result["accepted_builtin"] = known->second;
    } else {
        verify_plain_directory(path.parent_path());
        const auto metadata = J::parse(read_plain_file(path, id));
        if (metadata.value("schema", 0) != 1 || metadata.value("flow_id", "") != id)
            fail("BUILTIN_METADATA_INVALID", id);
        const auto accepted = metadata.at("accepted_builtin").get<std::string>();
        const auto local = metadata.at("local_revision").get<std::string>();
        result["accepted_builtin"] = accepted;
        result["status"] = current.at("revision") == builtin.at("revision") ? "current" :
            current.at("revision") == local && local == accepted ? "update_available" : "local_modified";
    }
    for (const auto &entry : std::filesystem::directory_iterator(root_)) {
        if (entry.path().extension() != L".json" || entry.path().stem().string() == id) continue;
        const auto other = entry.path().stem().string();
        const auto candidate = read_unlocked(other);
        if (authoring::referenced_flows(candidate).contains(id))
            result["affected_references"].push_back(other);
    }
    return result;
}

J WorkflowRepository::sync_builtin(const J &document,
    const std::string &expected_local_revision, const std::string &expected_builtin_revision) {
    const auto id = document.at("flow").at("id").get<std::string>();
    require_revision(expected_local_revision);
    require_revision(expected_builtin_revision);
    const auto builtin = persisted(document);
    if (builtin.at("revision") != expected_builtin_revision)
        fail("BUILTIN_PACKAGE_CHANGED", id);
    const auto metadata_path = builtin_path_for(id);
    RepositoryLock lock(root_);
    const bool tracked = std::filesystem::is_regular_file(metadata_path);
    if (tracked) verify_plain_directory(metadata_path.parent_path());
    auto metadata = tracked ? J::parse(read_plain_file(metadata_path, id)) : J::object();
    if (!tracked) {
        const auto known = historical_builtins.find(id);
        if (known == historical_builtins.end() || known->second != expected_local_revision)
            fail("BUILTIN_SOURCE_UNCONFIRMED", id);
        metadata = {{"schema", 1}, {"flow_id", id},
                    {"import_baseline", known->second},
                    {"accepted_builtin", known->second},
                    {"local_revision", known->second}};
    }
    if (metadata.value("schema", 0) != 1 || metadata.value("flow_id", "") != id)
        fail("BUILTIN_METADATA_INVALID", id);
    const auto current = read_unlocked(id);
    if (current.at("revision") != expected_local_revision ||
        metadata.at("local_revision") != expected_local_revision ||
        metadata.at("accepted_builtin") != expected_local_revision)
        fail("BUILTIN_LOCAL_CONFLICT", id);
    const auto fixed_ref = [&](auto &&self, const J &value) -> bool {
        if (value.is_object()) {
            if (value.value("flow_id", std::string{}) == id &&
                value.value("expected_revision", std::string{}) == expected_local_revision)
                return true;
            for (const auto &[key, child] : value.items())
                if (self(self, child)) return true;
        } else if (value.is_array()) {
            for (const auto &child : value) if (self(self, child)) return true;
        }
        return false;
    };
    for (const auto &entry : std::filesystem::directory_iterator(root_)) {
        if (entry.path().extension() != L".json" || entry.path().stem().string() == id) continue;
        if (fixed_ref(fixed_ref, read_unlocked(entry.path().stem().string())))
            fail("BUILTIN_FIXED_REFERENCE_REQUIRES_REVIEW", id);
    }
    // 两文件无法原子同时替换；内容保留备份，元数据失败时不删除旧资料。
    platform::atomic_write(path_for(id), builtin.dump(2), true);
    metadata["accepted_builtin"] = builtin.at("revision");
    metadata["local_revision"] = builtin.at("revision");
    if (!tracked) std::filesystem::create_directories(metadata_path.parent_path());
    platform::atomic_write(metadata_path, metadata.dump(2), tracked);
    return builtin;
}

} // namespace wvd::storage
