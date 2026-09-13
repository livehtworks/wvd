#pragma once
#include <boost/beast/http.hpp>
#include <filesystem>
#include <string>
namespace wvd::api {
namespace http = boost::beast::http;
using Request = http::request<http::string_body>;
using Response = http::response<http::string_body>;
// 只读路由不拥有执行状态；磁盘访问严格限制在显式指定的静态站点根目录。
Response route(const Request &request, const std::filesystem::path &web_root, unsigned short port);
void finalize_response_for_send(const Request &request, Response &response);
} // namespace wvd::api
