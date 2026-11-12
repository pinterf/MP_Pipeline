#pragma once

#if defined(_DEBUG) && !defined(DISABLE_TRACING)
#define ENABLE_TRACING
#endif

#if defined(ENABLE_TRACING)

#define __TRACE_TEXT(x) L##x

#define _TRACE_TEXT(x) __TRACE_TEXT(x)

#ifndef TRACE_PREFIX
#define TRACE_PREFIX _TRACE_TEXT("AVS_SplitProcess ")
#endif

#ifndef TRACE_COMPONENT
#define TRACE_COMPONENT _TRACE_TEXT("")
#endif


void _trace(const wchar_t *format, ...);
void _trace_hr( const wchar_t* strFile, DWORD dwLine, const wchar_t* strFunction, HRESULT hr, const wchar_t* strMsg );
void _trace_hr( const wchar_t* strFile, DWORD dwLine, const wchar_t* strFunction, HRESULT hr, const wchar_t* strMsg, const wchar_t* strMsg2 );

#define TRACE(format, ...) _trace(TRACE_PREFIX TRACE_COMPONENT _TRACE_TEXT(":") _TRACE_TEXT(format), ##__VA_ARGS__)
#define TRACE_HR(hr, msg) _trace_hr( _TRACE_TEXT(__FILE__), (DWORD)__LINE__, _TRACE_TEXT(__FUNCTION__), hr, msg )
#define TRACE_HR2(hr, msg, msg2) _trace_hr( _TRACE_TEXT(__FILE__), (DWORD)__LINE__, _TRACE_TEXT(__FUNCTION__), hr, msg, msg2 )
#else
#define TRACE(...) 
#define TRACE_HR(...) 
#define TRACE_HR2(...) 
#endif
