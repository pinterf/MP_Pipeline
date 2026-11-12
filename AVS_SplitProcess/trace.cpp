#define UNICODE
#define _UNICODE

#include "stdafx.h"
#include "trace.h"

void _trace(const TCHAR *format, ...)
{
   TCHAR buffer[ 4096 ];

   buffer[ sizeof(buffer) / sizeof(TCHAR) - 1 ] = 0;

   va_list argptr;
   va_start( argptr, format );
   _vsntprintf_s( buffer, sizeof(buffer) / sizeof(TCHAR) - 1, format, argptr );
   va_end( argptr );

   OutputDebugString( buffer );
   _tprintf( TEXT("%s\n"), buffer );
}

void _trace_hr( const TCHAR* strFile, DWORD dwLine, const TCHAR* strFunction, HRESULT hr, const TCHAR* strMsg )
{
    TRACE( "Trace HR: Code=0x%08x, File=%s, Line=%d, Function=%s, Msg=%s", hr, strFile, dwLine, strFunction, strMsg );
}

void _trace_hr( const TCHAR* strFile, DWORD dwLine, const TCHAR* strFunction, HRESULT hr, const TCHAR* strMsg, const TCHAR* strMsg2 )
{
    TRACE( "Trace HR: Code=0x%08x, File=%s, Line=%d, Function=%s, Msg=%s, Msg2=%s", hr, strFile, dwLine, strFunction, strMsg, strMsg2 );
}
