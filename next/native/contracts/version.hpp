#pragma once
#include <string_view>
namespace wvd::contracts {
inline constexpr std::string_view service_version = "0.1.0";
inline constexpr int api_version = 1;
inline constexpr std::string_view stage = "M1";
// 锁定路线不代表运行时加载：M1 不链接 Maa，也没有执行会话或设备入口。
inline constexpr std::string_view maafw_version = "5.13.0";
} // namespace wvd::contracts
