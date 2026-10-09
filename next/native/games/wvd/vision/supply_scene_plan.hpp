#pragma once
#include <array>
#include <json.hpp>

namespace wvd::games::vision {
enum class SupplyPage { Unknown, Interrupted, Panel, Dungeon };
inline const std::array<nlohmann::json, 9> &supply_scene_probes() {
    using J = nlohmann::json;
    static const std::array<J,9> probes{
        J{{"mode","combat_active"}}, J{{"mode","template"},{"image","chestFlag"}},
        J{{"mode","template"},{"image","whowillopenit"}}, J{{"mode","template"},{"image","chestOpening"}},
        J{{"mode","template"},{"image","RiseAgain"}}, J{{"mode","template"},{"image","trait"}},
        J{{"mode","template"},{"image","recover"}}, J{{"mode","template"},{"image","dungFlag"}},
        J{{"mode","template"},{"image","mapFlag"}}};
    return probes;
}
template<class Probe> SupplyPage classify_supply_page(Probe &&probe) {
    const auto &plan=supply_scene_probes();
    for(std::size_t i=0;i<5;++i) if(probe(plan[i])) return SupplyPage::Interrupted;
    // Once a panel is proven, dungeon/map negatives are not that stage's input prerequisites.
    if(probe(plan[5]) || probe(plan[6])) return SupplyPage::Panel;
    if(probe(plan[7]) && !probe(plan[8])) return SupplyPage::Dungeon;
    return SupplyPage::Unknown;
}
inline bool supply_phase_matches(SupplyPage page, const std::string &phase) {
    if(phase=="interrupted") return page==SupplyPage::Interrupted;
    if(phase=="panel") return page==SupplyPage::Panel;
    if(phase=="dungeon") return page==SupplyPage::Dungeon;
    if(phase=="context") return page==SupplyPage::Panel || page==SupplyPage::Dungeon;
    if(phase=="post") return page!=SupplyPage::Unknown;
    throw std::runtime_error("SUPPLY_PHASE_INVALID");
}
}
