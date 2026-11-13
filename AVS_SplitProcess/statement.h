#pragma once

#include <regex>
#include <type_traits>

// A regex over script lines: '^' and '$' match at the start and end of every line.
// That needs std::regex::multiline (C++17) in the newer MSVC STL (v145), libstdc++ and libc++.
// The older MSVC STLs (v141, v143 up to 14.44) don't have it, and their '^' and '$' match at
// line boundaries anyway (non-standard).
namespace statement_detail {
template <typename R, typename = void>
struct multiline_flag
{
    static constexpr std::regex_constants::syntax_option_type value = std::regex_constants::syntax_option_type();
};
template <typename R>
struct multiline_flag<R, std::void_t<decltype(R::multiline)>>
{
    static constexpr std::regex_constants::syntax_option_type value = R::multiline;
};
}

inline std::regex make_line_regex(const char* pattern, bool icase)
{
    std::regex_constants::syntax_option_type flags = std::regex::ECMAScript | statement_detail::multiline_flag<std::regex>::value;
    if (icase)
    {
        flags |= std::regex::icase;
    }
    return std::regex(pattern, flags);
}

// varargs = (format, pointer)*
// group in the respective position will be sscanf-ed to the pointer
// format can be null, in that case: *pointer = strdup(group)
// pointer can be null, the function will stop processing at that point
bool scan_statement(const char* script, const char* regex_pattern, const char** statement_pos_out, ...);

bool scan_statement(char* script, const char* regex_pattern, char** statement_pos_out, ...);

bool has_statement(const char* script, const char* regex_pattern);

// number of (non-overlapping) matches of the statement pattern
int count_statements(const char* script, const char* regex_pattern);