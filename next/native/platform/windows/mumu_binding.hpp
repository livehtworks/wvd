#pragma once
#include <filesystem>
#include <json.hpp>
#include <stop_token>

namespace wvd::platform {
// 初次启动与崩溃恢复共用选定实例的可见窗口入口，不启动后台无窗口实例。
void launch_selected_instance(const std::filesystem::path &launcher, int index);
// 崩溃实例的info仍携带有效身份，但error_code可能是900/901而非0。
bool mumu_metadata_usable(const nlohmann::json &live);
// 正式应用根据用户选择的安装目录、实例和ADB地址生成私有绑定。
// 该操作只查询MuMu元数据，不连接设备、不启动实例，也不要求用户编写证据文件。
nlohmann::json create_mumu_binding(const std::filesystem::path &manager, int index,
                                   std::string serial,
                                   std::stop_token cancellation = {});
// 只接受已定位的单实例；建立设备会话前核对管理器和进程现场。
nlohmann::json verify_mumu_binding(const std::filesystem::path &file,
                                   std::stop_token cancellation = {});
nlohmann::json verify_mumu_binding(const nlohmann::json &binding,
                                   std::stop_token cancellation = {});
} // namespace wvd::platform
