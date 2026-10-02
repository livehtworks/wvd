#pragma once

#include <cstdint>
#include <json.hpp>
#include <stdexcept>
#include <string>

namespace wvd::storage {
enum class LogLevel : int { Trace, Debug, Info, Warn, Error, Off };

struct LoggingPolicy {
    LogLevel level{LogLevel::Info};
    bool performance{true};
    bool memory{true};
    bool recognition{false};
    std::uint32_t memory_interval_ms{1000};

    bool accepts(LogLevel candidate) const noexcept {
        return level != LogLevel::Off && static_cast<int>(candidate) >= static_cast<int>(level);
    }
    static LoggingPolicy parse(const nlohmann::json &value) {
        if (!value.is_object() || value.size() != 6 || value.value("schema", 0) != 1 ||
            !value.contains("level") || !value.at("level").is_string() ||
            !value.contains("performance") || !value.at("performance").is_boolean() ||
            !value.contains("memory") || !value.at("memory").is_boolean() ||
            !value.contains("recognition") || !value.at("recognition").is_boolean() ||
            !value.contains("memory_interval_ms") || !value.at("memory_interval_ms").is_number_integer())
            throw std::runtime_error("LOGGING_POLICY_INVALID");
        const auto name = value.at("level").get<std::string>();
        LoggingPolicy result;
        if (name == "trace") result.level = LogLevel::Trace;
        else if (name == "debug") result.level = LogLevel::Debug;
        else if (name == "info") result.level = LogLevel::Info;
        else if (name == "warn") result.level = LogLevel::Warn;
        else if (name == "error") result.level = LogLevel::Error;
        else if (name == "off") result.level = LogLevel::Off;
        else throw std::runtime_error("LOGGING_POLICY_INVALID");
        result.performance = value.at("performance").get<bool>();
        result.memory = value.at("memory").get<bool>();
        result.recognition = value.at("recognition").get<bool>();
        const auto interval = value.at("memory_interval_ms").get<std::int64_t>();
        if (interval < 1000 || interval > 60000)
            throw std::runtime_error("LOGGING_POLICY_INVALID");
        result.memory_interval_ms = static_cast<std::uint32_t>(interval);
        return result;
    }
    static LoggingPolicy from_profile(const nlohmann::json &document) {
        return document.contains("logging") ? parse(document.at("logging")) : LoggingPolicy{};
    }
    nlohmann::json json() const {
        const char *names[] = {"trace", "debug", "info", "warn", "error", "off"};
        return {{"schema", 1}, {"level", names[static_cast<int>(level)]},
                {"performance", performance}, {"memory", memory},
                {"recognition", recognition}, {"memory_interval_ms", memory_interval_ms}};
    }
};
} // namespace wvd::storage
