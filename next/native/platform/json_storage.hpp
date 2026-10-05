#pragma once
#include <json.hpp>
#include <cstdint>

namespace wvd::platform {
// Container capacities, not allocator/ONNX/OpenCV resident bytes. Boundary use only.
struct JsonStorageEstimate {
    std::uint64_t nodes{}, capacity_bytes{}, string_bytes{}, array_bytes{}, object_entries{};
    bool truncated{};
    void add(const nlohmann::json &value, unsigned depth = 0) {
        if (depth > 64 || nodes >= 1000000) { truncated = true; return; }
        ++nodes;
        if (value.is_string()) {
            const auto size = value.get_ref<const nlohmann::json::string_t &>().capacity();
            string_bytes += size;
            capacity_bytes += size;
        } else if (value.is_array()) {
            const auto size = value.get_ref<const nlohmann::json::array_t &>().capacity() * sizeof(nlohmann::json);
            array_bytes += size;
            capacity_bytes += size;
            for (const auto &child : value) add(child, depth + 1);
        } else if (value.is_object()) {
            object_entries += value.size();
            capacity_bytes += value.size() * (sizeof(nlohmann::json::object_t::value_type) + 4 * sizeof(void *));
            for (const auto &[key, child] : value.items()) { capacity_bytes += key.capacity(); add(child, depth + 1); }
        }
    }
    nlohmann::json json() const {
        return {{"nodes", nodes}, {"estimated_capacity_bytes", capacity_bytes},
            {"string_capacity_bytes", string_bytes}, {"array_capacity_bytes", array_bytes},
            {"object_entries", object_entries}, {"truncated", truncated},
            {"measurement", "container_capacity_estimate_not_resident_allocation"}};
    }
};
}
