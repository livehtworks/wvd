#pragma once
#include <cstdint>

namespace wvd::devices::capture_protocol {
constexpr std::uint32_t magic = 0x57435644;
constexpr std::uint32_t version = 1;
constexpr std::uint32_t capture = 1;
constexpr std::uint32_t quit = 2;
constexpr std::uint32_t max_dimension = 8192;
constexpr std::uint32_t max_payload = max_dimension * max_dimension * 4;

struct Request {
    std::uint32_t marker{magic};
    std::uint32_t schema{version};
    std::uint32_t operation{capture};
    std::uint32_t reserved{};
    std::uint64_t sequence{};
};
struct Reply {
    std::uint32_t marker{magic};
    std::uint32_t schema{version};
    std::uint32_t status{};
    std::uint32_t display_id{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t stride{};
    std::uint32_t byte_count{};
    std::uint64_t sequence{};
};
static_assert(sizeof(Request) == 24);
static_assert(sizeof(Reply) == 40);
} // namespace wvd::devices::capture_protocol
