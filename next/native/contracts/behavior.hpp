#pragma once
#include <compare>
#include <json.hpp>
#include <string>
#include <vector>

namespace wvd::contracts {
struct ImplementationIdentity {
    std::string id, revision;
    auto operator<=>(const ImplementationIdentity &) const = default;
};
struct BehaviorBinding {
    std::string name;
    ImplementationIdentity implementation;
    nlohmann::json parameters = nlohmann::json::object();
};
using BehaviorBindings = std::vector<BehaviorBinding>;
} // namespace wvd::contracts
