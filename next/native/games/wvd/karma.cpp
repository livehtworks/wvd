#include "karma.hpp"
#include <boost/multiprecision/cpp_int.hpp>
#include <stdexcept>

namespace wvd::games {
KarmaChoice choose_karma(const std::string &value) {
    using boost::multiprecision::cpp_int;
    auto first = value.find_first_not_of(" \t\r\n\f\v");
    const auto last = value.find_last_not_of(" \t\r\n\f\v");
    if (first == std::string::npos)
        throw std::runtime_error("KARMA_VALUE_INVALID");
    bool negative = value[first] == '-';
    if (negative || value[first] == '+')
        ++first;
    if (first > last)
        throw std::runtime_error("KARMA_VALUE_INVALID");
    cpp_int number = 0;
    bool digit = false;
    for (auto i = first; i <= last; ++i) {
        const auto c = value[i];
        if (c == '_' && digit && i < last && value[i + 1] >= '0' && value[i + 1] <= '9') {
            digit = false;
            continue;
        }
        if (c < '0' || c > '9')
            throw std::runtime_error("KARMA_VALUE_INVALID");
        number = number * 10 + (c - '0');
        digit = true;
    }
    if (negative)
        number = -number;
    // 保留旧 startswith('-') 与 +0 约定，不以数值正负替换字符串业务键。
    if (number == 0)
        return {true, value, "+2"};
    if (value.starts_with('-')) {
        number += 2;
        return {true, value, number.str()};
    }
    number -= 1;
    return {false, value, "+" + number.str()};
}
}
