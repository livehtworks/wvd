#pragma once
#include <boost/asio.hpp>
#include <filesystem>
#include <memory>
namespace wvd::api {
class HttpServer {
  public:
    HttpServer(boost::asio::io_context &, unsigned short port, std::filesystem::path web_root);
    ~HttpServer();
    void start();
    void stop();
    unsigned short port() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace wvd::api
