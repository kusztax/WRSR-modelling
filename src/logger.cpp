#include "logger.h"

#include <windows.h>
#include <stdio.h>
#include <wchar.h>

namespace wrsr {
namespace {

SRWLOCK   g_lock = SRWLOCK_INIT;   // static init: usable before anything runs
HANDLE    g_file = INVALID_HANDLE_VALUE;
HostLogFn g_host = nullptr;
wchar_t   g_path[MAX_PATH * 2] = L"";

const wchar_t kLogName[] = L"wrsr_rail_mod.log";

// Directory part of a module's path, without the trailing backslash.
bool ModuleDir(HMODULE mod, wchar_t* out, DWORD cap)
{
    DWORD n = GetModuleFileNameW(mod, out, cap);
    if (n == 0 || n >= cap) return false;
    wchar_t* slash = wcsrchr(out, L'\\');
    if (!slash) slash = wcsrchr(out, L'/');
    if (!slash) return false;
    *slash = 0;
    return true;
}

HANDLE TryCreate(const wchar_t* dir)
{
    if (!dir || !dir[0]) return INVALID_HANDLE_VALUE;
    _snwprintf(g_path, sizeof(g_path) / sizeof(g_path[0]) - 1, L"%ls\\%ls", dir, kLogName);
    g_path[sizeof(g_path) / sizeof(g_path[0]) - 1] = 0;
    // Share everything: the game keeps it open, and you will want to tail it
    // from macOS (the bottle's drive_c is a normal folder) while it runs.
    return CreateFileW(g_path, GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}

void WriteRaw(const char* s, size_t len)
{
    if (g_file == INVALID_HANDLE_VALUE || len == 0) return;
    DWORD wrote = 0;
    WriteFile(g_file, s, (DWORD)len, &wrote, nullptr);
}

}  // namespace

namespace log {

bool Open(void* selfModule)
{
    AcquireSRWLockExclusive(&g_lock);
    if (g_file == INVALID_HANDLE_VALUE)
    {
        wchar_t dir[MAX_PATH * 2];

        // 1. the game folder (directory of SOVIET64.exe)
        if (ModuleDir(nullptr, dir, MAX_PATH * 2)) g_file = TryCreate(dir);

        // 2. the folder this DLL was loaded from
        if (g_file == INVALID_HANDLE_VALUE && selfModule &&
            ModuleDir((HMODULE)selfModule, dir, MAX_PATH * 2))
            g_file = TryCreate(dir);

        // 3. %TEMP%
        if (g_file == INVALID_HANDLE_VALUE)
        {
            DWORD n = GetTempPathW(MAX_PATH * 2, dir);
            if (n > 0 && n < MAX_PATH * 2)
            {
                if (dir[n - 1] == L'\\') dir[n - 1] = 0;
                g_file = TryCreate(dir);
            }
        }

        if (g_file == INVALID_HANDLE_VALUE) g_path[0] = 0;
        else
        {
            static const char kHeader[] =
                "==== wrsr_rail_mod " WRSR_MOD_VERSION " log ====\r\n";
            WriteRaw(kHeader, sizeof(kHeader) - 1);
        }
    }
    bool ok = g_file != INVALID_HANDLE_VALUE;
    ReleaseSRWLockExclusive(&g_lock);
    return ok;
}

void Close()
{
    AcquireSRWLockExclusive(&g_lock);
    if (g_file != INVALID_HANDLE_VALUE)
    {
        CloseHandle(g_file);
        g_file = INVALID_HANDLE_VALUE;
    }
    g_host = nullptr;
    ReleaseSRWLockExclusive(&g_lock);
}

void SetHostSink(HostLogFn fn)
{
    AcquireSRWLockExclusive(&g_lock);
    g_host = fn;
    ReleaseSRWLockExclusive(&g_lock);
}

const wchar_t* Path() { return g_path; }

}  // namespace log

void LogV(const char* fmt, va_list ap)
{
    char msg[1024];
    int  n = vsnprintf(msg, sizeof(msg), fmt, ap);
    if (n < 0) return;
    if ((size_t)n >= sizeof(msg)) n = (int)sizeof(msg) - 1;

    SYSTEMTIME t;
    GetLocalTime(&t);

    char line[1200];
    int  len = snprintf(line, sizeof(line), "%02u:%02u:%02u.%03u [%5lu] %s\r\n",
                        t.wHour, t.wMinute, t.wSecond, t.wMilliseconds,
                        (unsigned long)GetCurrentThreadId(), msg);
    if (len < 0) return;
    if ((size_t)len >= sizeof(line)) len = (int)sizeof(line) - 1;

    AcquireSRWLockExclusive(&g_lock);
    WriteRaw(line, (size_t)len);
    HostLogFn host = g_host;
    ReleaseSRWLockExclusive(&g_lock);

    // Outside the lock: the host takes its own.
    if (host) host("wrsr_rail  %s", msg);
}

void Log(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    LogV(fmt, ap);
    va_end(ap);
}

}  // namespace wrsr
