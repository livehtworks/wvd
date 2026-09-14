#include "party.hpp"
#include "inn.hpp"

namespace wvd::games::supply {
tasks::CompiledWorkflow assemble_party(const std::optional<std::string> &party_image) {
    using C = tasks::PipelineCompiler;
    C graph("supply.assemble_party");
    const auto inn = C::image("Inn"), guild = C::image("guild"), edit = C::image("Edit");
    const auto management = C::image("PartyManagement"), title = C::image("PartyManagementTitle");
    // 旧 Windows 调用写作 ok，资源权威名称实际是 OK.png；不引入大小写并存副本。
    const auto assemble = C::image("AssembleParty"), ok = C::image("OK");
    const auto selection = C::all({title, C::absent(ok)});
    const auto city = C::all({inn, C::absent(title), C::absent(ok)});
    // 初次可从公会/队伍管理中接续，但仅确认并退出后的城市状态才是完成。
    graph.route("Entry", {"Select", "Management", "Edit", "Guild"});
    graph.click("Guild", C::all({city, guild}), guild, edit, {"Edit"});
    graph.click("Edit", edit, edit, management, {"Management"});
    graph.click("Management", management, management, title, {"Select"});
    if (party_image)
        graph.click("Select", selection, C::image(*party_image), C::all({title, assemble}), {"Assemble"});
    else
        graph.fixed_click("Select", selection, C::all({title, assemble}), {137, 290}, {"Assemble"});
    graph.click("Assemble", selection, assemble, ok, {"Confirm"});
    graph.click("Confirm", ok, ok, C::all({title, C::absent(ok)}), {"Return"});
    graph.back("Return", C::all({title, C::absent(ok)}),
               C::any({city, edit, management, title}), {"Returned", "Return", "ReturnGuild"});
    graph.back("ReturnGuild", C::all({C::any({edit, management}), C::absent(title)}),
               C::any({city, edit, management}), {"Returned", "ReturnGuild"});
    graph.observe("Returned", city, {"Terminal"});
    return graph.finish();
}
tasks::CompiledWorkflow assemble_and_rest(bool royal_suite,
                                         const std::optional<std::string> &party_image) {
    tasks::PipelineCompiler graph("supply.assemble_and_rest");
    const auto rest = graph.append("Rest", rest_at_inn(royal_suite), {"Terminal"});
    const auto party = graph.append("Party", assemble_party(party_image), {rest});
    graph.route("Entry", {party});
    return graph.finish();
}
}
