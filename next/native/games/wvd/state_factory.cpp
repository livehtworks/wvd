#include "state.hpp"
#include "storage/karma_writer.hpp"

namespace wvd::games {
namespace {
std::unique_ptr<contracts::BusinessRunState> create_state(
    const nlohmann::json &parameters, const contracts::StateCreationContext &creation) {
    const auto &profile = parameters.at("profile");
    auto writer = parameters.contains("profile_store")
        ? storage::make_karma_writer(parameters.at("profile_store"), profile) : nullptr;
    return std::make_unique<WvdRunState>(profile, creation, std::move(writer));
}
}
void register_wvd_state(runtime::BehaviorRegistry &registry) {
    registry.add_state_factory({"wvd.state", "1"}, create_state);
}
contracts::BehaviorBinding wvd_state_binding(const nlohmann::json &profile) {
    return {"WvdState", {"wvd.state", "1"}, {{"profile", profile}}};
}
}
