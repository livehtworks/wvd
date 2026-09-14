#include "karma_writer.hpp"
#include "profile_store.hpp"
#include "maafw/preflight.hpp"

namespace wvd::storage {
namespace {
using J = nlohmann::json;
class Writer final : public games::KarmaCommitPort {
  public:
    Writer(const J &binding, const J &values)
        : store_(maafw::path_from_utf8(binding.at("path")), binding.at("descriptor")), document_(store_.load()) {
        if (document_.at("revision") != binding.at("revision") || document_.at("values") != values)
            throw std::runtime_error("PROFILE_BINDING_MISMATCH");
    }
    std::string save(const J &effect) override {
        if (effect.at("field") != "KARMA_ADJUST" ||
            document_.at("values").at("KARMA_ADJUST") != effect.at("before"))
            throw std::runtime_error("PROFILE_EFFECT_MISMATCH");
        auto next = document_;
        next["values"]["KARMA_ADJUST"] = effect.at("after");
        next["last_business_update"] = effect;
        next["last_business_update"].erase("save_status");
        // 只有整份 CAS 成功才推进本地 revision。失败不能触发动作重放。
        document_ = store_.compare_exchange(document_.at("revision"), next);
        return document_.at("revision").get<std::string>();
    }
  private:
    ProfileStore store_;
    J document_;
};
}
std::unique_ptr<games::KarmaCommitPort> make_karma_writer(const J &binding, const J &values) {
    return std::make_unique<Writer>(binding, values);
}
}
