#pragma once
#include "json.hpp"
#include <string>
namespace platform {
nlohmann::json memory();
nlohmann::json identity();
std::string utc();
}

