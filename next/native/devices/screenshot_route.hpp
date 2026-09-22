#pragma once
#include <json.hpp>
#include <stdexcept>
#include <string_view>

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
            record_failure({{"stage", "connect"}, {"reason", "PRIMARY_CONNECT_FAILED"}});
        } catch (const std::exception &error) {
            if (!fallback_error(error.what()))
                throw;
            record_failure({{"stage", "connect"}, {"reason", error.what()}});
        }
        encode_ = true;
        return open(true);
    }
    template <class Capture, class Open> auto capture(Capture &&capture, Open &&open) {
        try {
            return capture();
        } catch (const std::exception &error) {
            if (!fallback_error(error.what()))
                throw;
            record_failure({{"stage", "capture"},
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
    static bool fallback_error(std::string_view reason) {
        return reason == "ADB_CONTROLLER_CREATE_FAILED" || reason == "ADB_CAPTURE_FAILED" ||
               reason == "ADB_CAPTURE_DECODE_FAILED" || reason == "ADB_CAPTURE_ENCODE_FAILED" ||
               reason == "ADB_CAPTURE_SIZE_INVALID" || reason.starts_with("MUMU_EXTRAS_");
    }
    void record_failure(nlohmann::json failure) {
        if (failures_.size() >= 128) failures_.erase(failures_.begin());
        failures_.push_back(std::move(failure));
    }
    bool encode_only_, encode_;
    nlohmann::json failures_ = nlohmann::json::array();
};
} // namespace wvd::devices
