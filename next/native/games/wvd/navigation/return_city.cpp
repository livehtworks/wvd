#include "return_city.hpp"

namespace wvd::games::navigation {
tasks::CompiledWorkflow return_to_fortress() {
    using C = tasks::PipelineCompiler;
    C graph("navigation.return_to_fortress", std::chrono::seconds{120});
    const auto inn = C::image("Inn");
    const auto normal = C::any({inn, C::image("EdgeOfTown"), C::image("returntotown"), C::image("returnText"), C::image("leaveDung"), C::image("blessing")});
    graph.route("Entry", {"Done", "Exit0", "Exit1", "Exit2", "Exit3", "Dismiss"});
    graph.observe("Done", inn, {"Terminal"});
    std::size_t index = 0;
    for (const auto *name : {"returntotown", "returnText", "leaveDung", "blessing"}) {
        const auto node = "Exit" + std::to_string(index++), marker = std::string(name);
        graph.click(node, C::all({C::image(marker), C::absent(inn)}), C::image(marker), normal, {"Entry"});
        graph.delay_after(node, 2000); graph.hit_limit(node, 16);
    }
    graph.fixed_click("Dismiss", C::all({C::image("EdgeOfTown"), C::absent(inn)}), normal, {1, 1}, {"Entry"});
    graph.delay_after("Dismiss", 2000); graph.hit_limit("Dismiss", 16); graph.hit_limit("Entry", 64);
    return graph.finish();
}
}
