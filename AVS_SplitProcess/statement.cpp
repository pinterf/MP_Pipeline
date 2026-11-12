#include "stdafx.h"
#include "statement.h"

#include <regex>
#include <stdarg.h>

using namespace std;

bool scan_statement_impl(const char* script, const char* regex_pattern, const char** statement_pos_out, va_list vargs)
{
    if (statement_pos_out)
    {
        *statement_pos_out = NULL;
    }

    regex re(regex_pattern, regex::ECMAScript | regex::icase);
    cmatch m;
    if (!regex_search(script, m, re))
    {
        return false;
    }
    if (statement_pos_out)
    {
        *statement_pos_out = m[0].first;
    }

    for (size_t i = 1; i < m.size(); i++)
    {
        const char* format = va_arg(vargs, const char*);
        void* pointer = va_arg(vargs, void*);
        if (!pointer)
        {
            break;
        }
        if (!format)
        {
            size_t buffer_length = m[i].length() + 1;
            char* str = (char*)malloc(buffer_length);
            memset(str, 0, buffer_length);
            memcpy(str, m[i].first, m[i].length());
            *(char**)pointer = str;
        } else {
            auto str = m[i].str();
            int result = sscanf(str.c_str(), format, pointer);
            if (result == EOF || result < 1)
            {
                // invalid statement
                return false;
            }
        }
    }
    

    return true;
}

bool scan_statement(const char* script, const char* regex_pattern, const char** statement_pos_out, ...)
{
    va_list vargs;
    va_start(vargs, statement_pos_out);
    return scan_statement_impl(script, regex_pattern, statement_pos_out, vargs);
}

bool scan_statement(char* script, const char* regex_pattern, char** statement_pos_out, ...)
{
    va_list vargs;
    va_start(vargs, statement_pos_out);
    return scan_statement_impl(script, regex_pattern, (const char**)statement_pos_out, vargs);
}

bool has_statement(const char* script, const char* regex_pattern)
{
    return scan_statement(script, regex_pattern, NULL, NULL, NULL);
}

int count_statements(const char* script, const char* regex_pattern)
{
    regex re(regex_pattern, regex::ECMAScript | regex::icase);
    auto begin = cregex_iterator(script, script + strlen(script), re);
    return (int)distance(begin, cregex_iterator());
}