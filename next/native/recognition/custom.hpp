#pragma once

#include "request.hpp"
#include "resources.hpp"
#include "platform/windows/memory_diagnostics.hpp"
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
    bool cancelled() const { return cancelled_ && cancelled_->load(); }
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
          std::function<nlohmann::json(const nlohmann::json &)> ocr,
          const std::atomic<bool> *cancelled)
        : allowed_roi_(roi), invocation_id_(id), business_(business), ocr_(std::move(ocr)),
          cancelled_(cancelled) {}
    const contracts::Box allowed_roi_;
    const std::uint64_t invocation_id_;
    const contracts::BusinessRunState *business_;
    const std::function<nlohmann::json(const nlohmann::json &)> ocr_;
    const std::atomic<bool> *cancelled_;
};

struct Pixels {
    std::span<const std::uint8_t> bgr;
    contracts::Size size;
};

struct Cache {
    // 游戏时序状态留在这里；可重建像素由独立缓存淘汰。
    std::map<std::string, std::any> assets;
    std::shared_ptr<DecodedAssetCache> decoded;
    std::shared_ptr<MatchBudget> match_budget;
    std::shared_ptr<platform::MemoryDiagnostics> diagnostics;
    const std::atomic<bool> *cancelled{};
    std::uint64_t source_id{};
    std::string frame_key;
    std::map<std::string, nlohmann::json> results;
    std::uint64_t result_bytes{};
    // 单帧的纯模板叶子证据，服务换帧时清空；不保存像素或业务/运动状态。
    std::map<std::string, nlohmann::json> template_results;
    std::uint64_t template_result_bytes{};
};

using Handler = std::function<nlohmann::json(const Bundle &, Pixels, const nlohmann::json &,
                                              const Scope &, Cache &)>;
using Handlers = std::map<std::string, Handler>;
} // namespace wvd::recognition
