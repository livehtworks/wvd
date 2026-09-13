#pragma once
#include <json.hpp>
#include <stdexcept>

namespace wvd::devices {
// 单连接内只允许主 -> 后备。打开函数必须先正常销毁旧 Controller，不能并行试探。
class ScreenshotRoute {
  public:
    explicit ScreenshotRoute(bool encode_only = false)
        : encode_only_(encode_only), encode_(encode_only) {}
    bool encode() const {
        return encode_;
    }
    const nlohmann::json &failures() const {
        return failures_;
    }
    template <class Open> bool connect(Open &&open) {
        encode_ = encode_only_;
        if (encode_)
            return open(true);
        try {
            if (open(false))
                return true;
            failures_.push_back({{"stage", "connect"}, {"reason", "PRIMARY_CONNECT_FAILED"}});
        } catch (const std::exception &error) {
            if (std::string_view(error.what()) == "STOP_TIMEOUT")
                throw;
            failures_.push_back({{"stage", "connect"}, {"reason", error.what()}});
        }
        encode_ = true;
        return open(true);
    }
    template <class Capture, class Open> auto capture(Capture &&capture, Open &&open) {
        try {
            return capture();
        } catch (const std::exception &error) {
            if (std::string_view(error.what()) == "STOP_TIMEOUT")
                throw;
            failures_.push_back({{"stage", "capture"},
                                 {"backend", encode_ ? "ADB_ENCODE" : "MUMU_EXTRAS"},
                                 {"reason", error.what()}});
            if (encode_)
                throw;
            encode_ = true;
            if (!open(true))
                throw std::runtime_error("ADB_FALLBACK_CONNECT_FAILED");
            return capture();
        }
    }

  private:
    bool encode_only_, encode_;
    nlohmann::json failures_ = nlohmann::json::array();
};
} // namespace wvd::devices
