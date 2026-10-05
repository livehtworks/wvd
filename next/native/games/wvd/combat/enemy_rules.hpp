#pragma once
#include <json.hpp>
#include <string>
#include <vector>

namespace wvd::games::combat {
struct EnemyRule {
    std::string id, name, image, strategy, png;
};
// 顺序即优先级；同一开场帧命中多个头像时，只选择第一条。
std::vector<EnemyRule> enemy_rules(const nlohmann::json &profile);
}
