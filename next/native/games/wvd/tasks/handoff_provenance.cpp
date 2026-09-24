#include "handoff_provenance.hpp"
#include "platform/windows/file_digest.hpp"
#include <stdexcept>

namespace wvd::games::tasks {
using J = nlohmann::json;
std::string digest_handoff_json(const J &value) {
    const auto text = value.dump();
    return platform::bytes_sha256({reinterpret_cast<const std::uint8_t *>(text.data()),
                                   text.size()});
}
void validate_handoff_source(const J &source, const J &profile) {
    auto body = source;
    const auto expected = body.at("digest").get<std::string>();
    body.erase("digest");
    if (body.at("schema") != 1 || digest_handoff_json(body) != expected ||
        body.at("profile_digest").get<std::string>() != digest_handoff_json(profile) ||
        profile.at("FARM_TARGET") != body.at("task").at("id") ||
        body.at("target_task").at("id") != "7000G" ||
        body.at("target_task").at("type") != "quest" ||
        !body.at("profile_sources").is_object() ||
        !body.at("selected_section").is_string() ||
        body.at("request_id").get<std::string>().empty())
        throw std::runtime_error("HANDOFF_SOURCE_INVALID");
    for (const auto &[key, value] : profile.items()) {
        (void)value;
        if (!body.at("profile_sources").contains(key) ||
            !body.at("profile_sources").at(key).is_string())
            throw std::runtime_error("HANDOFF_SOURCE_INVALID");
    }
}
bool handoff_has_unconfirmed_effect(const J &business) {
    for (const char *pointer : {"/inn_payment_pending", "/karma_pending", "/bounty_report_pending",
            "/special_dialogue_pending", "/manual_separation/transfer_pending",
            "/featured_visit/pending", "/golden_chest/leap_pending", "/sandman/leap_pending",
            "/gold_income/pending", "/bull_cave/leap_pending", "/steel_trial/pending",
            "/repel_forces/pending", "/bounty_cycle/transfer_pending", "/fishing/casting_pending",
            "/fordraig/pending", "/cave_of_separation/pending",
            "/fishing/reward_pending", "/fishing/transfer_pending"})
        if (business.at(J::json_pointer(pointer)).get<bool>())
            return true;
    return false;
}
} // namespace wvd::games::tasks
