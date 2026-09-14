#pragma once
#include <json.hpp>
#include <string>

namespace wvd::games {
struct KarmaChoice {
    bool ambush{};
    std::string before, after;
};
KarmaChoice choose_karma(const std::string &value);

// 业务只提交已确认的效果，存储实现独立负责 revision/CAS 和原子替换。
// 不提供“重新执行动作”或隐式寻找旧配置的入口。
class KarmaCommitPort {
  public:
    virtual ~KarmaCommitPort() = default;
    virtual std::string save(const nlohmann::json &confirmed_effect) = 0;
};
}
