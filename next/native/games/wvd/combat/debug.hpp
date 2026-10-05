#pragma once
#include <json.hpp>

namespace wvd::games::combat {
// A transient author document: no saved workflow, boot chain or dungeon loop.
inline nlohmann::json debug_document(const nlohmann::json &revision) {
    using J = nlohmann::json;
    return J{{"schema", 1}, {"revision", revision},
        {"flow", {{"id", "combat-debug"}, {"name", "战斗方案调试"}, {"description", ""}}}, {"entry", "battle"},
        {"nodes", J::array({J{{"id", "battle"}, {"type", "business"}, {"name", "当前战斗"},
            {"parameters", {{"binding", "combat"}}}}, J{{"id", "done"}, {"type", "end"},
            {"name", "战斗结束"}, {"parameters", {{"outcome", "success"}}}}})},
        {"edges", J::array({J{{"id", "end-battle"}, {"from", "battle"}, {"to", "done"},
            {"outcome", "success"}, {"order", 0}}})},
        {"layout", {{"nodes", J::array({J{{"node_id", "battle"}, {"x", 0}, {"y", 0}},
            J{{"node_id", "done"}, {"x", 300}, {"y", 0}}})}, {"viewport", {{"x", 0}, {"y", 0}, {"zoom", 1}}}}},
        {"execution", {{"time_limit_ms", 1800000}}}};
}
}
