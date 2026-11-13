#pragma once

// Debug tracing, in Debug builds (DISABLE_TRACING switches it off in a file): to the debugger /
// DebugView (platform::debug_output) and stdout. printf format, narrow strings.

#if defined(_DEBUG) && !defined(DISABLE_TRACING)
#define ENABLE_TRACING
#endif

#if defined(ENABLE_TRACING)

#ifndef TRACE_PREFIX
#define TRACE_PREFIX "AVS_SplitProcess "
#endif

#ifndef TRACE_COMPONENT
#define TRACE_COMPONENT ""
#endif

void _trace(const char* format, ...);

#define TRACE(format, ...) _trace(TRACE_PREFIX TRACE_COMPONENT ":" format, ##__VA_ARGS__)
#else
#define TRACE(...)
#endif