#pragma once
#include <filesystem>
#include <json.hpp>

namespace wvd::platform {
// 只接受已定位的单实例；在创建任何 Maa Controller 前再读取管理器和进程现场。
nlohmann::json verify_mumu_binding(const std::filesystem::path &file);
} // namespace wvd::platform
