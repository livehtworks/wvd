#include "http_server.hpp"
#include "routes.hpp"
#include <boost/beast.hpp>
#include <boost/version.hpp>
#include <atomic>
#include <set>
static_assert(BOOST_VERSION == 109000, "Use the locked Boost 1.90.0 headers");
namespace wvd::api {
namespace asio = boost::asio;
namespace beast = boost::beast;
using tcp = asio::ip::tcp;
struct HttpServer::Impl : std::enable_shared_from_this<HttpServer::Impl> {
    struct Connection;
    tcp::acceptor acceptor;
    const std::filesystem::path root;
    DynamicHandler handler;
    unsigned short bound_port{};
    std::set<std::shared_ptr<Connection>> active; // only the io_context thread mutates
    std::atomic<bool> stopping{false};
    bool joined{}; // only the host thread calls join
    asio::thread_pool workers{3};
    asio::thread_pool controls{1}; // stop/status never queue behind three slow probes
    Impl(asio::io_context &io, unsigned short port, std::filesystem::path web_root,
         DynamicHandler dynamic_handler)
        : acceptor(io), root(std::filesystem::canonical(web_root)), handler(std::move(dynamic_handler)) {
        tcp::endpoint endpoint{asio::ip::make_address("127.0.0.1"), port};
        acceptor.open(endpoint.protocol());
        acceptor.bind(endpoint);
        acceptor.listen(16);
        bound_port = acceptor.local_endpoint().port();
    }
    static bool control_request(const Request &request) {
        const auto raw = std::string(request.target());
        const auto path = raw.substr(0, raw.find('?'));
        return ((request.method() == http::verb::get || request.method() == http::verb::head) &&
                 (path == "/api/v1/runs/current" || path == "/api/v1/device" || path == "/api/v1/version")) ||
               (request.method() == http::verb::post && path.starts_with("/api/v1/runs/") && path.ends_with("/stop"));
    }
    struct Connection : std::enable_shared_from_this<Connection> {
        std::shared_ptr<Impl> owner;
        beast::tcp_stream stream;
        beast::flat_buffer buffer;
        http::request_parser<http::string_body> parser;
        Response reply;
        bool closed{};
        Connection(std::shared_ptr<Impl> o, tcp::socket socket)
            : owner(std::move(o)), stream(std::move(socket)) {
            // 8 MiB image -> ~10.7 MiB base64 plus JSON; matches Application's 16 MiB limit.
            parser.body_limit(16 * 1024 * 1024);
            parser.header_limit(8192);
        }
        void send(Response response) { // io thread only
            if (closed || owner->stopping) { close(); return; }
            reply = std::move(response);
            finalize_response_for_send(parser.get(), reply);
            stream.expires_after(std::chrono::seconds(5));
            http::async_write(stream, reply, [self=shared_from_this()](beast::error_code, std::size_t) { self->close(); });
        }
        void read() {
            stream.expires_after(std::chrono::seconds(5));
            http::async_read(stream, buffer, parser,
                [self=shared_from_this()](beast::error_code ec, std::size_t) {
                    if (ec) {
                        if (ec == http::error::body_limit) {
                            Response rejected{http::status::payload_too_large, 11};
                            rejected.set(http::field::content_type, "application/json");
                            rejected.body() = "{\"error_code\":\"REQUEST_TOO_LARGE\"}";
                            rejected.prepare_payload();
                            self->send(std::move(rejected));
                        } else self->close();
                        return;
                    }
                    auto &pool = Impl::control_request(self->parser.get()) ? self->owner->controls : self->owner->workers;
                    asio::post(pool, [self] {
                        if (self->owner->stopping) return;
                        Response response;
                        try {
                            response = route(self->parser.get(), self->owner->root,
                                             self->owner->bound_port, self->owner->handler);
                        } catch (...) {
                            response = Response{http::status::internal_server_error, 11};
                            response.set(http::field::content_type, "application/json");
                            response.body() = "{\"error_code\":\"INTERNAL_ERROR\"}";
                            response.prepare_payload();
                        }
                        asio::post(self->stream.get_executor(), [self, response=std::move(response)]() mutable {
                            self->send(std::move(response));
                        });
                    });
                });
        }
        void close() { // io thread only; idempotent, owner remains alive through callbacks
            if (closed) return;
            closed = true;
            beast::error_code ignored;
            stream.socket().cancel(ignored);
            stream.socket().close(ignored);
            owner->active.erase(shared_from_this());
        }
    };
    void accept() {
        acceptor.async_accept([self=shared_from_this()](beast::error_code ec, tcp::socket socket) {
            if (self->stopping) return;
            if (!ec && self->active.size() < 64) {
                auto client = std::make_shared<Connection>(self, std::move(socket));
                self->active.insert(client);
                client->read();
            }
            if (ec != asio::error::operation_aborted) self->accept();
        });
    }
    void stop() { // invoke on the host io thread; no joins here
        if (stopping.exchange(true)) return;
        beast::error_code ignored;
        acceptor.cancel(ignored);
        acceptor.close(ignored);
        while (!active.empty()) { auto client = *active.begin(); client->close(); }
        // 不停止 pool 队列：已排队 handler 读取 stopping 后退出并释放 Connection。
        // 直接 pool.stop() 会留下带 shared_ptr<Impl> 的闭环，join 也不能析构这些任务。
    }
    void join() {
        if (joined) return;
        workers.join();
        controls.join();
        joined = true;
    }
};
HttpServer::HttpServer(asio::io_context &io, unsigned short port, std::filesystem::path root,
                       DynamicHandler handler)
    : impl_(std::make_shared<Impl>(io, port, std::move(root), std::move(handler))) {}
HttpServer::~HttpServer() { impl_->stop(); impl_->join(); }
void HttpServer::start() { impl_->accept(); }
void HttpServer::stop() { impl_->stop(); }
void HttpServer::join_workers() { impl_->join(); }
unsigned short HttpServer::port() const { return impl_->bound_port; }
} // namespace wvd::api
