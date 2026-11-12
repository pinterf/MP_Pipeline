// Minimal OutputDebugString listener (DBWIN protocol), for checking TRACE output of the
// Debug plugin and slave processes without a debugger or DebugView.
//
// usage: dbwin_listen <seconds> [filter]
//   prints "<pid> <message>" for each message containing 'filter' (all when omitted),
//   exits after <seconds>. Only one DBWIN listener should run at a time (close DebugView).
// build: cl /nologo /O2 /EHsc dbwin_listen.cpp

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

struct DbWinBuffer
{
    DWORD process_id;
    char data[4096 - sizeof(DWORD)];
};

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        printf("usage: dbwin_listen <seconds> [filter]\n");
        return 2;
    }
    setvbuf(stdout, nullptr, _IONBF, 0);
    const DWORD duration_ms = (DWORD)atoi(argv[1]) * 1000;
    const char* filter = argc > 2 ? argv[2] : nullptr;

    HANDLE buffer_ready = CreateEventA(NULL, FALSE, FALSE, "DBWIN_BUFFER_READY");
    HANDLE data_ready = CreateEventA(NULL, FALSE, FALSE, "DBWIN_DATA_READY");
    HANDLE mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(DbWinBuffer), "DBWIN_BUFFER");
    if (!buffer_ready || !data_ready || !mapping)
    {
        printf("unable to create DBWIN objects, code %lu\n", GetLastError());
        return 1;
    }
    // ERROR_ALREADY_EXISTS is normal: every process that called OutputDebugString keeps the
    // buffer open. A second listener (e.g. DebugView) would steal messages, close it first.
    const DbWinBuffer* buffer = (const DbWinBuffer*)MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    if (!buffer)
    {
        printf("MapViewOfFile failed, code %lu\n", GetLastError());
        return 1;
    }

    const ULONGLONG end = GetTickCount64() + duration_ms;
    SetEvent(buffer_ready);
    while (GetTickCount64() < end)
    {
        if (WaitForSingleObject(data_ready, 100) != WAIT_OBJECT_0)
        {
            continue;
        }
        if (!filter || strstr(buffer->data, filter))
        {
            const char* text = buffer->data;
            size_t len = strnlen(text, sizeof(buffer->data));
            while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r'))
            {
                len--;
            }
            printf("%lu %.*s\n", buffer->process_id, (int)len, text);
        }
        SetEvent(buffer_ready);
    }
    UnmapViewOfFile(buffer);
    CloseHandle(mapping);
    CloseHandle(data_ready);
    CloseHandle(buffer_ready);
    return 0;
}
