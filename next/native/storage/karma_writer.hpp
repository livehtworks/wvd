#pragma once
#include "games/wvd/karma.hpp"
#include <memory>

namespace wvd::storage {
// binding 是本次显式选定的新版 profile；不使用默认路径或旧 config 回退。
std::unique_ptr<games::KarmaCommitPort> make_karma_writer(const nlohmann::json &binding,
                                                       const nlohmann::json &frozen_values);
}
