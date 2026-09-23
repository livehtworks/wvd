#pragma once

#include "pipeline_compiler.hpp"
#include <map>
#include <functional>
#include <string>
#include <vector>

namespace wvd::games::tasks {

// 作者模型只描述可编辑图；运行时仍使用 PipelineCompiler 产出的 Maa Pipeline。
struct AuthorWorkflowCompilation {
    CompiledWorkflow workflow;
    std::map<std::string, std::vector<std::string>> node_to_pipeline;
    std::map<std::string, std::string> pipeline_to_node;
    // 编译节点 -> 外层调用节点 / 块内节点；运行中可展开定位，不丢掉调用实例。
    nlohmann::json source_paths = nlohmann::json::object();
};

// revision 可缺省（新建草稿），其余字段严格校验。错误码尾部携带具体 node/edge ID。
void validate_author_workflow(const nlohmann::json &document);
using AuthorBusinessResolver = std::function<CompiledWorkflow(const nlohmann::json &parameters)>;
using AuthorFlowResolver = std::function<AuthorWorkflowCompilation(const nlohmann::json &call)>;
AuthorWorkflowCompilation compile_author_workflow(
    const nlohmann::json &document, AuthorBusinessResolver resolver = {},
    AuthorFlowResolver public_flow = {});

} // namespace wvd::games::tasks
