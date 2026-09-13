#include "routes.hpp"
#include "contracts/version.hpp"
#include "json.hpp"
#include <fstream>
#include <optional>
namespace wvd::api {
namespace fs = std::filesystem;
using Json = nlohmann::json;
static Response response(http::status status, std::string body,
                         std::string_view mime = "application/json; charset=utf-8") {
    Response r{status, 11};
    r.set(http::field::content_type, mime);
    r.set(http::field::server, "wvd-next");
    r.set(http::field::cache_control, "no-store");
    r.set("X-Content-Type-Options", "nosniff");
    r.set("Content-Security-Policy", "default-src 'self'; img-src 'self' data:; style-src 'self' "
                                     "'unsafe-inline'; connect-src 'self'; frame-ancestors 'none'");
    r.keep_alive(false);
    r.body() = std::move(body);
    r.prepare_payload();
    return r;
}
static Response error(http::status status, const char *code) {
    return response(status, Json{{"error_code", code}}.dump());
}
static std::optional<std::string> decode_path(std::string_view input) {
    std::string value;
    auto hex = [](char c) {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < input.size(); ++i) {
        char c = input[i];
        if (c == '%') {
            if (i + 2 >= input.size())
                return {};
            int a = hex(input[++i]), b = hex(input[++i]);
            if (a < 0 || b < 0)
                return {};
            c = static_cast<char>(a * 16 + b);
        }
        if (static_cast<unsigned char>(c) < 32 || c == '\\' || c == ':')
            return {};
        value += c;
    }
    return value;
}
Response route(const Request &request, const fs::path &root, unsigned short port) {
    const std::string authority = "127.0.0.1:" + std::to_string(port);
    if (request[http::field::host] != authority)
        return error(http::status::forbidden, "INVALID_HOST");
    const auto origin = request[http::field::origin];
    if (!origin.empty() && origin != "http://" + authority)
        return error(http::status::forbidden, "CROSS_ORIGIN_DENIED");
    if (request.method() != http::verb::get && request.method() != http::verb::head)
        return error(http::status::method_not_allowed, "READ_ONLY_STAGE");
    auto target = std::string(request.target());
    target = target.substr(0, target.find('?'));
    Response result;
    if (target == "/api/v1/version") {
        result = response(http::status::ok, Json{{"service", "automationd"},
                                                 {"version", contracts::service_version},
                                                 {"api_version", contracts::api_version},
                                                 {"stage", contracts::stage}}
                                                .dump());
    } else if (target == "/api/v1/capabilities") {
        result = response(
            http::status::ok,
            Json{{"platform", "windows-x64"},
                 {"stage", "M1"},
                 {"api_version", 1},
                 {"capabilities", {"version_query", "static_inventory_review"}},
                 {"maafw", {{"locked_version", contracts::maafw_version}, {"loaded", false}}},
                 {"device_control", false},
                 {"task_execution", false},
                 {"websocket", false},
                 {"production_switch", false}}
                .dump());
    } else if (target.starts_with("/api/"))
        return error(http::status::not_found, "UNKNOWN_API");
    else {
        auto decoded = decode_path(target);
        if (!decoded || decoded->empty() || (*decoded)[0] != '/')
            return error(http::status::bad_request, "INVALID_PATH");
        auto relative = *decoded == "/" ? std::string("index.html") : decoded->substr(1);
        auto path = fs::path(std::u8string(relative.begin(), relative.end()));
        if (path.is_absolute())
            return error(http::status::forbidden, "PATH_OUTSIDE_ROOT");
        for (const auto &part : path)
            if (part == ".." || part == ".")
                return error(http::status::forbidden, "PATH_TRAVERSAL");
        std::error_code ec;
        auto canonical = fs::weakly_canonical(root / path, ec);
        auto rel = canonical.lexically_relative(root);
        if (ec || rel.empty() || *rel.begin() == ".." || !fs::is_regular_file(canonical))
            return error(http::status::not_found, "ASSET_NOT_FOUND");
        auto size = fs::file_size(canonical, ec);
        if (ec || size > 32 * 1024 * 1024)
            return error(http::status::payload_too_large, "ASSET_TOO_LARGE");
        std::ifstream input(canonical, std::ios::binary);
        if (!input)
            return error(http::status::not_found, "ASSET_NOT_FOUND");
        std::string body((std::istreambuf_iterator<char>(input)), {});
        auto extension = canonical.extension().string();
        std::string mime = extension == ".html"   ? "text/html; charset=utf-8"
                           : extension == ".js"   ? "text/javascript; charset=utf-8"
                           : extension == ".css"  ? "text/css; charset=utf-8"
                           : extension == ".json" ? "application/json; charset=utf-8"
                           : extension == ".png"  ? "image/png"
                                                  : "application/octet-stream";
        result = response(http::status::ok, std::move(body), mime);
    }
    if (request.method() == http::verb::head)
        result.body().clear();
    return result;
}
} // namespace wvd::api
