#pragma once

// varargs = (format, pointer)*
// group in the respective position will be sscanf-ed to the pointer
// format can be null, in that case: *pointer = strdup(group)
// pointer can be null, the function will stop processing at that point
bool scan_statement(const char* script, const char* regex_pattern, const char** statement_pos_out, ...);

bool scan_statement(char* script, const char* regex_pattern, char** statement_pos_out, ...);

bool has_statement(const char* script, const char* regex_pattern);

// number of (non-overlapping) matches of the statement pattern
int count_statements(const char* script, const char* regex_pattern);