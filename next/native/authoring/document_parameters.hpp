#pragma once
#include "parameter_contract.hpp"
#include <json.hpp>
#include <limits>

namespace wvd::authoring {
using Json = nlohmann::json;
inline Scalar scalar(const Json &value) {
    if (value.is_boolean()) return value.get<bool>();
    if (value.is_number_unsigned()) {
        const auto v = value.get<std::uint64_t>();
        if (v > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()))
            contract_error("FLOW_INTEGER_OVERFLOW");
        return static_cast<std::int64_t>(v);
    }
    if (value.is_number_integer()) return value.get<std::int64_t>();
    if (value.is_number_float()) return value.get<double>();
    if (value.is_string()) return value.get<std::string>();
    contract_error("FLOW_SCALAR_REQUIRED");
}
inline Json json_scalar(const Scalar &value) {
    return std::visit([](const auto &v) { return Json(v); }, value);
}
inline Arguments arguments(const Json &value) {
    if (!value.is_object() || value.size() > 64) contract_error("FLOW_ARGUMENTS_INVALID");
    Arguments result;
    for (const auto &[key, v] : value.items()) result.emplace(key, scalar(v));
    return result;
}
inline std::vector<ParameterSpec> parameter_specs(const Json &document) {
    if (!document.contains("interface")) return {};
    const auto &api = document.at("interface");
    if (!api.is_object()) contract_error("FLOW_INTERFACE_INVALID");
    for (const auto &[key, _] : api.items())
        if (key != "kind" && key != "category" && key != "parameters")
            contract_error("FLOW_INTERFACE_FIELD", key);
    const auto kind = api.value("kind", std::string("task"));
    if (kind != "step" && kind != "block" && kind != "task") contract_error("FLOW_KIND_INVALID");
    const auto category = api.value("category", std::string{});
    if (category.size() > 128) contract_error("FLOW_CATEGORY_INVALID");
    const auto rows = api.value("parameters", Json::array());
    if (!rows.is_array() || rows.size() > 64) contract_error("FLOW_PARAMETERS_INVALID");
    std::vector<ParameterSpec> result;
    const std::map<std::string, ParameterType> types{
        {"boolean", ParameterType::Boolean}, {"integer", ParameterType::Integer},
        {"number", ParameterType::Number}, {"string", ParameterType::String},
        {"resource", ParameterType::Resource}};
    for (const auto &row : rows) {
        if (!row.is_object()) contract_error("FLOW_PARAMETER_INVALID");
        for (const auto &[key, _] : row.items())
            if (key != "name" && key != "label" && key != "type" && key != "default" &&
                key != "min" && key != "max" && key != "choices" && key != "max_length" && key != "bindings")
                contract_error("FLOW_PARAMETER_FIELD", key);
        ParameterSpec spec;
        spec.name = row.at("name").get<std::string>();
        spec.label = row.value("label", spec.name);
        const auto type = row.at("type").get<std::string>();
        if (!types.contains(type)) contract_error("FLOW_PARAMETER_TYPE", spec.name);
        spec.type = types.at(type);
        if (!row.contains("default")) contract_error("FLOW_DEFAULT_REQUIRED", spec.name);
        spec.fallback = scalar(row.at("default"));
        if (row.contains("min")) spec.minimum = row.at("min").get<double>();
        if (row.contains("max")) spec.maximum = row.at("max").get<double>();
        if (row.contains("max_length")) {
            const auto length = scalar(row.at("max_length"));
            if (!std::holds_alternative<std::int64_t>(length) || std::get<std::int64_t>(length) < 1 ||
                std::get<std::int64_t>(length) > 4096) contract_error("FLOW_PARAMETER_LENGTH", spec.name);
            spec.max_length = static_cast<std::size_t>(std::get<std::int64_t>(length));
        }
        if (row.contains("choices")) {
            if (!row.at("choices").is_array() || row.at("choices").empty()) contract_error("FLOW_CHOICES_INVALID", spec.name);
            for (const auto &choice : row.at("choices")) spec.choices.push_back(scalar(choice));
            auto check = spec;
            check.choices.clear();
            for (const auto &choice : spec.choices) check_scalar(check, choice);
        }
        if (!row.contains("bindings") || !row.at("bindings").is_array() || row.at("bindings").empty())
            contract_error("FLOW_PARAMETER_BINDINGS", spec.name);
        result.push_back(std::move(spec));
    }
    return result;
}
// 参数只能写入指定节点 parameters 内已经存在的字段，不能改 node_id、边、实现名或被调用定义。
inline Json instantiate_document(Json document, const Json &provided = Json::object(),
                                 const Json &extensions = Json::object()) {
    // revision 即使随后从实例化产物移除，也必须先校验，不能绕过旧草稿契约。
    if (document.contains("revision")) {
        if (!document.at("revision").is_string()) contract_error("AUTHOR_REVISION_INVALID");
        const auto rev = document.at("revision").get<std::string>();
        if (rev.size() != 64 || rev.find_first_not_of("0123456789abcdef") != std::string::npos)
            contract_error("AUTHOR_REVISION_INVALID");
    }
    const auto specs = parameter_specs(document);
    const auto bound = bind_arguments(specs, arguments(provided));
    std::set<std::string> written;
    if (document.contains("interface")) {
        for (const auto &spec : document.at("interface").value("parameters", Json::array())) {
            const auto value = json_scalar(bound.at(spec.at("name").get<std::string>()));
            for (const auto &binding : spec.at("bindings")) {
                if (!binding.is_object() || binding.size() != 2) contract_error("FLOW_BINDING_INVALID");
                const auto node_id = binding.at("node").get<std::string>();
                const auto path = binding.at("path").get<std::string>();
                if (path.empty() || path.front() != '/')
                    contract_error("FLOW_BINDING_STRUCTURAL", node_id + path);
                // 任何深度都禁止参数改写依赖身份，防止绑定后绕过同锁取得的依赖快照。
                std::size_t token_start = 1;
                while (token_start <= path.size()) {
                    const auto end = path.find('/', token_start);
                    auto token = path.substr(token_start, end == std::string::npos ? end : end - token_start);
                    for (const auto &[from, to] : std::vector<std::pair<std::string, std::string>>{{"~1", "/"}, {"~0", "~"}}) {
                        std::size_t at{};
                        while ((at = token.find(from, at)) != std::string::npos) { token.replace(at, from.size(), to); ++at; }
                    }
                    if (token == "binding" || token == "flow_id" || token == "expected_revision" ||
                        token == "calls" || token == "extensions")
                        contract_error("FLOW_BINDING_STRUCTURAL", node_id + path);
                    if (end == std::string::npos) break;
                    token_start = end + 1;
                }
                if (!written.insert(node_id + ":" + path).second) contract_error("FLOW_BINDING_DUPLICATE", node_id + path);
                auto &nodes = document.at("nodes");
                auto found = std::find_if(nodes.begin(), nodes.end(), [&](const Json &n) { return n.at("id") == node_id; });
                if (found == nodes.end()) contract_error("FLOW_BINDING_NODE_MISSING", node_id);
                const Json::json_pointer pointer(path);
                auto &parameters = found->at("parameters");
                if (!parameters.contains(pointer)) contract_error("FLOW_BINDING_PATH_MISSING", node_id + path);
                if (!parameters.at(pointer).is_primitive() || parameters.at(pointer).is_null())
                    contract_error("FLOW_BINDING_SCALAR_REQUIRED", node_id + path);
                parameters.at(pointer) = value;
            }
        }
        document.erase("interface");
    }
    if (!extensions.is_object() || extensions.size() > 32) contract_error("FLOW_EXTENSIONS_INVALID");
    std::set<std::string> slots;
    for (auto &node : document.at("nodes")) {
        if (node.at("type") != "slot") continue;
        auto &p = node.at("parameters");
        const auto name = p.at("name").get<std::string>();
        if (!public_id(name) || !slots.insert(name).second) contract_error("FLOW_SLOT_DUPLICATE", name);
        if (extensions.contains(name)) p["calls"] = extensions.at(name);
        if (!p.contains("calls")) p["calls"] = Json::array();
        if (!p.at("calls").is_array() || p.at("calls").size() > 32) contract_error("FLOW_SLOT_CALLS_INVALID", name);
    }
    for (const auto &[name, _] : extensions.items())
        if (!slots.contains(name)) contract_error("FLOW_SLOT_UNKNOWN", name);
    // 实例化产物不是可写草稿，不挪用模板 revision 冒充实例参数的版本。
    document.erase("revision");
    return document;
}
inline void validate_call(const Json &call, unsigned depth = 0) {
    if (depth > 8 || !call.is_object() || !call.contains("flow_id")) contract_error("FLOW_CALL_INVALID");
    for (const auto &[key, _] : call.items())
        if (key != "flow_id" && key != "arguments" && key != "extensions" && key != "expected_revision")
            contract_error("FLOW_CALL_FIELD", key);
    const auto id = call.at("flow_id").get<std::string>();
    if (!public_id(id) || id.size() > 64 || id.find('.') != std::string::npos)
        contract_error("FLOW_REFERENCE_ID", id);
    (void)arguments(call.value("arguments", Json::object()));
    if (call.contains("expected_revision")) {
        const auto revision = call.at("expected_revision").get<std::string>();
        if (revision.size() != 64 || revision.find_first_not_of("0123456789abcdef") != std::string::npos)
            contract_error("FLOW_REFERENCE_REVISION", id);
    }
    const auto extensions = call.value("extensions", Json::object());
    if (!extensions.is_object() || extensions.size() > 32) contract_error("FLOW_EXTENSIONS_INVALID", id);
    for (const auto &[name, list] : extensions.items()) {
        if (!public_id(name) || !list.is_array() || list.size() > 32) contract_error("FLOW_SLOT_CALLS_INVALID", name);
        for (const auto &nested : list) validate_call(nested, depth + 1);
    }
}
inline std::set<std::string> referenced_flows(const Json &document) {
    std::set<std::string> result;
    const auto collect = [&](auto &&self, const Json &call) -> void {
        validate_call(call);
        result.insert(call.at("flow_id").get<std::string>());
        for (const auto &list : call.value("extensions", Json::object()))
            for (const auto &child : list) self(self, child);
    };
    for (const auto &node : document.at("nodes")) {
        if (node.at("type") == "call") collect(collect, node.at("parameters"));
        if (node.at("type") == "slot")
            for (const auto &call : node.at("parameters").value("calls", Json::array())) collect(collect, call);
    }
    return result;
}
inline Json slot_names(const Json &document) {
    Json result = Json::array();
    for (const auto &node : document.at("nodes"))
        if (node.at("type") == "slot") result.push_back(node.at("parameters").at("name"));
    return result;
}
} // namespace wvd::authoring
