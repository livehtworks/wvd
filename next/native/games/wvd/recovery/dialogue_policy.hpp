#pragma once
#include <stdexcept>
#include <string>

namespace wvd::games::recovery {
// 冻结在业务编译产物中的任务语义，不是全局GUI配置或可执行脚本。
enum class DialoguePolicy { Default, Jier };
inline std::string dialogue_policy_name(DialoguePolicy policy) {
    switch (policy) {
    case DialoguePolicy::Default: return "";
    case DialoguePolicy::Jier: return "jier";
    }
    throw std::runtime_error("DIALOGUE_POLICY_INVALID");
}
}
