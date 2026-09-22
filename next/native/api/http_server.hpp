#pragma once
#include <boost/asio.hpp>
#include <filesystem>
#include <memory>
#include "routes.hpp"
namespace wvd::api {
class HttpServer {
  public:
    HttpServer(boost::asio::io_context &, unsigned short port, std::filesystem::path web_root,
               DynamicHandler handler = {});
    ~HttpServer();
    void start();
    void stop();
    void join_workers();
    unsigned short port() const;

  private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
} // namespace wvd::api
