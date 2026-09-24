#pragma once

#include "request.hpp"
#include "contracts/business_state.hpp"
#include <any>
#include <functional>
#include <map>
#include <span>
#include <stdexcept>

namespace wvd::recognition {
class Service;

class Scope {
  public:
    contracts::Box allowed_roi() const { return allowed_roi_; }
    std::uint64_t invocation_id() const { return invocation_id_; }
    nlohmann::json recognize_ocr(const nlohmann::json &parameters) const {
        if (!ocr_) throw std::runtime_error("OCR_CONTEXT_UNAVAILABLE");
        return ocr_(parameters);
    }
    nlohmann::json business_summary() const {
        if (!business_) throw std::runtime_error("BUSINESS_STATE_REQUIRED");
        return business_->summary();
    }

  private:
    friend class Service;
    Scope(contracts::Box roi, std::uint64_t id,
          const contracts::BusinessRunState *business,
          std::function<nlohmann::json(const nlohmann::json &)> ocr)
        : allowed_roi_(roi), invocation_id_(id), business_(business), ocr_(std::move(ocr)) {}
    const contracts::Box allowed_roi_;
    const std::uint64_t invocation_id_;
    const contracts::BusinessRunState *business_;
    const std::function<nlohmann::json(const nlohmann::json &)> ocr_;
};

struct Pixels {
    std::span<const std::uint8_t> bgr;
    contracts::Size size;
};

struct Cache {
    std::map<std::string, std::any> assets;
    std::string frame_key;
    std::map<std::string, nlohmann::json> results;
};

using Handler = std::function<nlohmann::json(const Bundle &, Pixels, const nlohmann::json &,
                                              const Scope &, Cache &)>;
using Handlers = std::map<std::string, Handler>;
} // namespace wvd::recognition
