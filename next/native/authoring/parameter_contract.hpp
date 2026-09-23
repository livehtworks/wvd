#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace wvd::authoring {
// 公开参数是值，不是函数地址、任意表达式或设备句柄。集合型附加行为由 slot/call 表达。
using Scalar = std::variant<bool, std::int64_t, double, std::string>;
using Arguments = std::map<std::string, Scalar>;
enum class ParameterType { Boolean, Integer, Number, String, Resource };
struct ParameterSpec {
    std::string name, label;
    ParameterType type{};
    std::optional<Scalar> fallback;
    std::optional<double> minimum, maximum;
    std::vector<Scalar> choices;
    std::size_t max_length{512};
};
inline bool public_id(const std::string &value) {
    if (value.empty() || value.size() > 160) return false;
    for (const unsigned char c : value)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) return false;
    const auto c = static_cast<unsigned char>(value.front());
    return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9')) && value.find("..") == std::string::npos;
}
class ContractError final : public std::runtime_error {
  public:
    explicit ContractError(const std::string &message) : std::runtime_error(message) {}
};
[[noreturn]] inline void contract_error(const std::string &code, const std::string &id = {}) {
    throw ContractError(id.empty() ? code : code + ":" + id);
}
inline void check_scalar(const ParameterSpec &spec, const Scalar &value) {
    bool valid = false;
    switch (spec.type) {
    case ParameterType::Boolean: valid = std::holds_alternative<bool>(value); break;
    case ParameterType::Integer: valid = std::holds_alternative<std::int64_t>(value); break;
    case ParameterType::Number:
        valid = std::holds_alternative<double>(value) || std::holds_alternative<std::int64_t>(value);
        break;
    case ParameterType::String:
    case ParameterType::Resource: valid = std::holds_alternative<std::string>(value); break;
    }
    if (!valid) contract_error("FLOW_ARGUMENT_TYPE", spec.name);
    if (const auto *s = std::get_if<std::string>(&value)) {
        if (s->size() > spec.max_length) contract_error("FLOW_ARGUMENT_LENGTH", spec.name);
        if (spec.type == ParameterType::Resource && !public_id(*s))
            contract_error("FLOW_RESOURCE_ID", spec.name);
    }
    if (std::holds_alternative<double>(value) || std::holds_alternative<std::int64_t>(value)) {
        const long double n = std::holds_alternative<double>(value)
            ? static_cast<long double>(std::get<double>(value))
            : static_cast<long double>(std::get<std::int64_t>(value));
        if (!std::isfinite(n) || (spec.minimum && n < *spec.minimum) ||
            (spec.maximum && n > *spec.maximum)) contract_error("FLOW_ARGUMENT_RANGE", spec.name);
    }
    if (!spec.choices.empty()) {
        const bool found = std::any_of(spec.choices.begin(), spec.choices.end(), [&](const Scalar &choice) {
            if (choice == value) return true;
            // number 允许 JSON 整数与浮点数字面量等值；boolean 从不作为 0/1。
            if (spec.type != ParameterType::Number) return false;
            const auto numeric = [](const Scalar &v) -> std::optional<long double> {
                if (const auto *d = std::get_if<double>(&v))
                    return static_cast<long double>(*d);
                if (const auto *i = std::get_if<std::int64_t>(&v))
                    return static_cast<long double>(*i);
                return {};
            };
            const auto a = numeric(choice), b = numeric(value);
            return a && b && *a == *b;
        });
        if (!found) contract_error("FLOW_ARGUMENT_CHOICE", spec.name);
    }
}
inline Arguments bind_arguments(const std::vector<ParameterSpec> &specs, const Arguments &supplied) {
    Arguments result;
    std::set<std::string> declared;
    for (const auto &spec : specs) {
        if (!public_id(spec.name) || !declared.insert(spec.name).second)
            contract_error("FLOW_PARAMETER_DUPLICATE_OR_INVALID", spec.name);
        if ((spec.minimum && !std::isfinite(*spec.minimum)) ||
            (spec.maximum && !std::isfinite(*spec.maximum)) ||
            (spec.minimum && spec.maximum && *spec.minimum > *spec.maximum))
            contract_error("FLOW_PARAMETER_RANGE", spec.name);
        if (spec.max_length == 0 || spec.max_length > 4096)
            contract_error("FLOW_PARAMETER_LENGTH", spec.name);
        if (spec.fallback) check_scalar(spec, *spec.fallback);
        const auto it = supplied.find(spec.name);
        if (it != supplied.end()) {
            check_scalar(spec, it->second);
            result.emplace(spec.name, it->second);
        } else if (spec.fallback) result.emplace(spec.name, *spec.fallback);
        else contract_error("FLOW_ARGUMENT_REQUIRED", spec.name);
    }
    for (const auto &[key, _] : supplied)
        if (!declared.contains(key)) contract_error("FLOW_ARGUMENT_UNKNOWN", key);
    return result;
}
// 这是“定义调用图”的静态检查，不是设备调度器。普通流程内的有限循环仍由原编译器检查。
inline void validate_call_graph(const std::map<std::string, std::vector<std::string>> &graph,
                                const std::string &root, std::size_t max_depth = 8) {
    std::set<std::string> active;
    std::map<std::string, std::size_t> heights;
    const auto visit = [&](auto &&self, const std::string &id) -> std::size_t {
        if (!graph.contains(id)) contract_error("FLOW_REFERENCE_MISSING", id);
        if (active.contains(id)) contract_error("FLOW_REFERENCE_CYCLE", id);
        if (auto it = heights.find(id); it != heights.end()) return it->second;
        if (active.size() >= max_depth) contract_error("FLOW_REFERENCE_DEPTH", id);
        active.insert(id);
        std::size_t height = 1;
        for (const auto &child : graph.at(id)) height = std::max(height, 1 + self(self, child));
        active.erase(id);
        if (height > max_depth) contract_error("FLOW_REFERENCE_DEPTH", id);
        heights[id] = height;
        return height;
    };
    if (!max_depth) contract_error("FLOW_REFERENCE_DEPTH");
    (void)visit(visit, root);
}
} // namespace wvd::authoring
