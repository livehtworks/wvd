#include "http_server.hpp"
#include "routes.hpp"
#include <boost/beast.hpp>
#include <boost/version.hpp>
#include <set>
static_assert(BOOST_VERSION == 109000, "Use the locked Boost 1.90.0 headers");
namespace wvd::api {
namespace asio = boost::asio;
namespace beast = boost::beast;
using tcp = asio::ip::tcp;
struct HttpServer::Impl {
    struct Connection;
    tcp::acceptor acceptor;
    std::filesystem::path root;
    std::set<std::shared_ptr<Connection>> active;
    bool stopping = false;
    Impl(asio::io_context &io, unsigned short port, std::filesystem::path web_root)
        : acceptor(io), root(std::filesystem::canonical(web_root)) {
        tcp::endpoint endpoint{asio::ip::make_address("127.0.0.1"), port};
        acceptor.open(endpoint.protocol());
        acceptor.bind(endpoint);
        acceptor.listen(16);
    }
    struct Connection : std::enable_shared_from_this<Connection> {
        Impl &owner;
        beast::tcp_stream stream;
        beast::flat_buffer buffer;
        http::request_parser<http::string_body> parser;
        Response reply;
        Connection(Impl &o, tcp::socket socket) : owner(o), stream(std::move(socket)) {
            parser.body_limit(4096);
            parser.header_limit(8192);
        }
        void read() {
            stream.expires_after(std::chrono::seconds(5));
            http::async_read(
                stream, buffer, parser,
                [self = shared_from_this()](beast::error_code ec, std::size_t) {
                    if (ec) {
                        self->close();
                        return;
                    }
                    try {
                        self->reply = route(self->parser.get(), self->owner.root,
                                            self->owner.acceptor.local_endpoint().port());
                    } catch (const std::exception &) {
                        self->reply = Response{http::status::internal_server_error, 11};
                        self->reply.body() = "{\"error_code\":\"INTERNAL_ERROR\"}";
                        self->reply.prepare_payload();
                    }
                    // HEAD 收口必须位于路由早返回和异常构造之后；长度代表对应 GET 内容。
                    finalize_response_for_send(self->parser.get(), self->reply);
                    self->stream.expires_after(std::chrono::seconds(5));
                    http::async_write(self->stream, self->reply,
                                      [self](beast::error_code, std::size_t) { self->close(); });
                });
        }
        void close() {
            beast::error_code ignored;
            stream.socket().cancel(ignored);
            stream.socket().close(ignored);
            owner.active.erase(shared_from_this());
        }
    };
    void accept() {
        // 活跃集合只持有在途连接。达到上限时拒绝新连接，不积累历史请求。
        acceptor.async_accept([this](beast::error_code ec, tcp::socket socket) {
            if (stopping)
                return;
            if (!ec && active.size() < 64) {
                auto client = std::make_shared<Connection>(*this, std::move(socket));
                active.insert(client);
                client->read();
            }
            if (ec != asio::error::operation_aborted)
                accept();
        });
    }
    void stop() {
        if (stopping)
            return;
        stopping = true;
        beast::error_code ignored;
        acceptor.cancel(ignored);
        acceptor.close(ignored);
        // 所有回调都在同一 io_context 线程；先关闭连接，运行完取消回调后再释放服务对象。
        while (!active.empty()) {
            auto client = *active.begin();
            client->close();
        }
    }
};
HttpServer::HttpServer(asio::io_context &io, unsigned short port, std::filesystem::path root)
    : impl_(std::make_unique<Impl>(io, port, std::move(root))) {}
HttpServer::~HttpServer() = default;
void HttpServer::start() {
    impl_->accept();
}
void HttpServer::stop() {
    impl_->stop();
}
unsigned short HttpServer::port() const {
    return impl_->acceptor.local_endpoint().port();
}
} // namespace wvd::api
