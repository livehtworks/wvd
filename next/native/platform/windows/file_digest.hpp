#pragma once
#include <filesystem>
#include <string>
#include <span>
#include <cstdint>
#include <functional>

namespace wvd::platform {
std::string file_sha256(const std::filesystem::path &path);
std::string bytes_sha256(std::span<const std::uint8_t> bytes);
// Borrowed synchronous Win32 HANDLE: hash exactly byte_count bytes from offset 0.
// The caller owns the handle, freezes its file and serializes all cursor access.
// This function never reopens a path and leaves the cursor at byte_count on success.
std::string handle_sha256(void *handle, std::uint64_t byte_count,
                          const std::function<void()> &check_cancel = {});
// Both handles are borrowed. Copy and hash exactly byte_count bytes with a
// bounded buffer; the caller verifies identities and owns target publication.
std::string copy_handle_sha256(void *source, void *target, std::uint64_t byte_count,
                               const std::function<void()> &check_cancel = {});
} // namespace wvd::platform
