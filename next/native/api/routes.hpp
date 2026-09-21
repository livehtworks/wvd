#pragma once
#include <boost/beast/http.hpp>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
namespace wvd::api {
namespace http = boost::beast::http;
using Request = http::request<http::string_body>;
using Response = http::response<http::string_body>;
struct DynamicReply {
    http::status status{http::status::ok};
    std::string body;
    std::string mime{"application/json; charset=utf-8"};
};
using DynamicHandler = std::function<std::optional<DynamicReply>(const Request &)>;
// 动态API只委托给应用装配；静态磁盘访问仍严格限制在显式站点根目录。
Response route(const Request &request, const std::filesystem::path &web_root, unsigned short port,
               const DynamicHandler &handler = {});
void finalize_response_for_send(const Request &request, Response &response);
} // namespace wvd::api
