#pragma once
#include <filesystem>
#include <json.hpp>
#include <stop_token>

namespace wvd::platform {
// 正式应用根据用户选择的安装目录、实例和ADB地址生成私有绑定。
// 该操作只查询MuMu元数据，不连接设备、不启动实例，也不要求用户编写证据文件。
nlohmann::json create_mumu_binding(const std::filesystem::path &manager, int index,
                                   std::string serial,
                                   std::stop_token cancellation = {});
// 只接受已定位的单实例；在创建任何 Maa Controller 前再读取管理器和进程现场。
nlohmann::json verify_mumu_binding(const std::filesystem::path &file,
                                   std::stop_token cancellation = {});
nlohmann::json verify_mumu_binding(const nlohmann::json &binding,
                                   std::stop_token cancellation = {});
} // namespace wvd::platform
