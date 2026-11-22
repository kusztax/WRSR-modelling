#include "env_info.h"

#include <stdio.h>
#include <string.h>

#include "logger.h"

namespace wrsr::env {
namespace {

void CopyStr(char* dst, size_t cap, const char* src)
{
    if (!src) { dst[0] = 0; return; }
    snprintf(dst, cap, "%s", src);
}

// Wine exports these from ntdll; real Windows does not.
void QueryWine(Info& info)
{
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return;

    using WineGetVersion     = const char* (*)();
    using WineGetHostVersion = void (*)(const char** sysname, const char** release);

    auto getVersion = (WineGetVersion)(void*)GetProcAddress(ntdll, "wine_get_version");
    if (getVersion) CopyStr(info.wineVersion, sizeof(info.wineVersion), getVersion());

    auto getHost = (WineGetHostVersion)(void*)GetProcAddress(ntdll, "wine_get_host_version");
    if (getHost)
    {
        const char* sys = nullptr;
        const char* rel = nullptr;
        getHost(&sys, &rel);
        CopyStr(info.hostSystem, sizeof(info.hostSystem), sys);
        CopyStr(info.hostRelease, sizeof(info.hostRelease), rel);
    }
}

void QueryFileVersion(HMODULE exe, Info& info)
{
    wchar_t path[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(exe, path, MAX_PATH * 2);
    if (n == 0 || n >= MAX_PATH * 2) return;

    DWORD dummy = 0;
    DWORD size  = GetFileVersionInfoSizeW(path, &dummy);
    if (!size) return;

    void* buf = HeapAlloc(GetProcessHeap(), 0, size);
    if (!buf) return;
    if (GetFileVersionInfoW(path, 0, size, buf))
    {
        VS_FIXEDFILEINFO* ffi = nullptr;
        UINT len = 0;
        if (VerQueryValueW(buf, L"\\", (void**)&ffi, &len) && ffi && len >= sizeof(*ffi))
        {
            snprintf(info.exeVersion, sizeof(info.exeVersion), "%u.%u.%u.%u",
                     HIWORD(ffi->dwFileVersionMS), LOWORD(ffi->dwFileVersionMS),
                     HIWORD(ffi->dwFileVersionLS), LOWORD(ffi->dwFileVersionLS));
        }
    }
    HeapFree(GetProcessHeap(), 0, buf);
}

void QueryPe(HMODULE exe, Info& info)
{
    auto* base = (BYTE*)exe;
    auto* dos  = (IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
    auto* nt = (IMAGE_NT_HEADERS64*)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return;
    info.exeTimeStamp   = nt->FileHeader.TimeDateStamp;
    info.exeSizeOfImage = nt->OptionalHeader.SizeOfImage;
}

}  // namespace

Info Collect(HMODULE exe)
{
    Info info;
    QueryWine(info);
    QueryFileVersion(exe, info);
    QueryPe(exe, info);
    return info;
}

void LogAll(HMODULE exe, HMODULE self)
{
    Info info = Collect(exe);

    char exePath[MAX_PATH * 2] = "";
    GetModuleFileNameA(exe, exePath, sizeof(exePath));
    Log("env   process  %s (pid %lu)", exePath, (unsigned long)GetCurrentProcessId());
    Log("env   exe      base %p, image 0x%lX, version %s, PE stamp 0x%08lX",
        (void*)exe, (unsigned long)info.exeSizeOfImage,
        info.exeVersion[0] ? info.exeVersion : "(none)", (unsigned long)info.exeTimeStamp);
    Log("env   plugin   base %p", (void*)self);

    if (info.wineVersion[0])
        Log("env   wine     %s on %s %s%s", info.wineVersion,
            info.hostSystem[0] ? info.hostSystem : "?", info.hostRelease,
            strcmp(info.hostSystem, "Darwin") == 0 ? "  (macOS: CrossOver, x86_64 via Rosetta 2)" : "");
    else
        Log("env   wine     not detected (native Windows?)");

    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    Log("env   cpu      arch %u, %lu logical processors, page %lu, alloc gran 0x%lX",
        si.wProcessorArchitecture, (unsigned long)si.dwNumberOfProcessors,
        (unsigned long)si.dwPageSize, (unsigned long)si.dwAllocationGranularity);

    Log("env   log file %ls", log::Path());
}

}  // namespace wrsr::env
