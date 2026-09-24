#pragma once
#include "document_parameters.hpp"
#include <algorithm>
#include <array>
#include <string_view>

namespace wvd::authoring {
inline constexpr std::array<std::string_view, 5> resource_locales{
    "", "en", "zh-Hant", "zh-Hans", "ja"};

inline void validate_resource_locale(const std::string &locale) {
    if (std::find(resource_locales.begin(), resource_locales.end(), locale) == resource_locales.end())
        contract_error("AUTHOR_RESOURCE_LOCALE_INVALID", locale);
}

// 请求中显式的空字符串仍是用户选择；不能被文档值或工作台界面语言覆盖。
inline std::string effective_resource_locale(const Json &request, const Json &execution) {
    std::string locale;
    if (request.contains("resource_locale")) locale = request.at("resource_locale").get<std::string>();
    else if (execution.contains("resource_locale")) locale = execution.at("resource_locale").get<std::string>();
    validate_resource_locale(locale);
    return locale;
}
} // namespace wvd::authoring
