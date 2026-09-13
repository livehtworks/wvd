#include "api/routes.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
void require(bool condition, const char *reason) {
    if (!condition)
        throw std::runtime_error(reason);
}

void check_exception_response(wvd::api::http::verb method) {
    namespace http = wvd::api::http;
    wvd::api::Request request{method, "/", 11};
    wvd::api::Response response;
    const std::string error_body = "{\"error_code\":\"INTERNAL_ERROR\"}";
    // 只在内部驱动构造 Connection 现有 catch 的响应，不向服务增加故障路由。
    try {
        throw std::runtime_error("controlled response construction");
    } catch (const std::exception &) {
        response = wvd::api::Response{http::status::internal_server_error, 11};
        response.body() = error_body;
        response.prepare_payload();
    }
    wvd::api::finalize_response_for_send(request, response);
    std::ostringstream serialized;
    serialized << response;
    require(!serialized.fail(), "Beast serialization failed");
    const auto bytes = serialized.str();
    const auto boundary = bytes.find("\r\n\r\n");
    require(boundary != std::string::npos, "missing header boundary");
    const auto body = bytes.substr(boundary + 4);
    require(response.result_int() == 500, "status changed");
    require(response[http::field::content_length] == std::to_string(error_body.size()),
            "GET representation length changed");
    require(body == (method == http::verb::head ? "" : error_body), "invalid wire body");
    std::cout << "{\"method\":\"" << http::to_string(method)
              << "\",\"status\":500,\"declared_length\":" << error_body.size()
              << ",\"actual_body_bytes\":" << body.size() << "}\n";
}
} // namespace

int main() {
    try {
        check_exception_response(wvd::api::http::verb::get);
        check_exception_response(wvd::api::http::verb::head);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "HEAD_TEST_FAILED: " << error.what() << '\n';
        return 1;
    }
}
