// fake_host.exe - loads a Tesmio-API plugin the way TesmioLoader does, with a
// minimal host table, WITHOUT the game. Run it inside the CrossOver bottle:
//
//   wine fake_host.exe wrsr_rail_mod.dll
//
// If this passes but the game does not produce wrsr_rail_mod.log, the problem
// is the loader/injection, not the DLL or CrossOver/Rosetta. If this fails,
// the game is not the problem. That split is the whole point.
//
// Limitations (deliberate): installInlineHook/patchIat refuse, findIatSlot and
// consume return null, and "the exe" is fake_host.exe itself - so the plugin's
// probes of SOVIET64.exe code report "not found". That is expected here.

#include <windows.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "tesmio_api.h"

static char  g_baseDir[MAX_PATH];
static char  g_pluginDir[MAX_PATH];
static FILE* g_log;

static void HostLog(const char* fmt, ...)
{
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    printf("[host] %s\n", buf);
    if (g_log) { fprintf(g_log, "%s\n", buf); fflush(g_log); }
}

static void** FindIatSlotStub(void*, const char*, const char*) { return nullptr; }

static int PatchIatStub(void*, const char*, const char* fn, void*, void**, const char* label)
{
    HostLog("patchIat(%s, %s) refused: fake host", fn, label ? label : "");
    return 0;
}

static int InstallInlineHookStub(void* target, void*, void**, const unsigned char*, size_t stolen,
                                 const char* label)
{
    HostLog("installInlineHook(%p, %zu, %s) refused: fake host", target, stolen, label ? label : "");
    return 0;
}

static unsigned char* AllocNearImpl(unsigned char* anchor, size_t size)
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    const ULONG_PTR gran = si.dwAllocationGranularity;
    ULONG_PTR a = (ULONG_PTR)anchor & ~(gran - 1);
    for (ULONG_PTR d = gran; d < 0x7FF00000ULL; d += gran)
    {
        void* p = VirtualAlloc((void*)(a + d), size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
        if (p) return (unsigned char*)p;
        if (a > d)
        {
            p = VirtualAlloc((void*)(a - d), size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
            if (p) return (unsigned char*)p;
        }
    }
    return nullptr;
}

static int ReadablePtrImpl(const void* p, size_t n)
{
    const BYTE* cur = (const BYTE*)p;
    const BYTE* end = cur + n;
    while (cur < end)
    {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(cur, &mbi, sizeof(mbi))) return 0;
        if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return 0;
        cur = (const BYTE*)mbi.BaseAddress + mbi.RegionSize;
    }
    return 1;
}

static long FaultFilterImpl(const char* what, void*)
{
    HostLog("FAULT %s", what ? what : "");
    return EXCEPTION_EXECUTE_HANDLER;
}

// Tesmio resolves "plugins\<name>.ini" beside the discovered plugin.
static void ResolveIni(const char* iniName, char* out, size_t cap)
{
    const char* prefix = "plugins\\";
    if (_strnicmp(iniName, prefix, strlen(prefix)) == 0)
        snprintf(out, cap, "%s\\%s", g_pluginDir, iniName + strlen(prefix));
    else
        snprintf(out, cap, "%s\\%s", g_baseDir, iniName);
}

static int ConfigIntImpl(const char* ini, const char* sec, const char* key, int fallback)
{
    char path[MAX_PATH * 2];
    ResolveIni(ini, path, sizeof(path));
    return (int)GetPrivateProfileIntA(sec, key, fallback, path);
}

static int ConfigStringImpl(const char* ini, const char* sec, const char* key, char* out, int outSize,
                            const char* fallback)
{
    char path[MAX_PATH * 2];
    ResolveIni(ini, path, sizeof(path));
    return (int)GetPrivateProfileStringA(sec, key, fallback ? fallback : "", out, (DWORD)outSize, path);
}

static int ProvideImpl(const char* service, unsigned version, const void*)
{
    HostLog("provide(%s v%u) noted", service, version);
    return 1;
}

static const void* ConsumeImpl(const char*, unsigned) { return nullptr; }

static void DirOf(const char* path, char* out, size_t cap)
{
    snprintf(out, cap, "%s", path);
    char* s = strrchr(out, '\\');
    if (!s) s = strrchr(out, '/');
    if (s) *s = 0; else snprintf(out, cap, ".");
}

int main(int argc, char** argv)
{
    const char* dllArg = argc > 1 ? argv[1] : "wrsr_rail_mod.dll";

    char selfPath[MAX_PATH];
    GetModuleFileNameA(nullptr, selfPath, sizeof(selfPath));
    DirOf(selfPath, g_baseDir, sizeof(g_baseDir));

    char dllFull[MAX_PATH];
    if (!GetFullPathNameA(dllArg, sizeof(dllFull), dllFull, nullptr))
        snprintf(dllFull, sizeof(dllFull), "%s", dllArg);
    DirOf(dllFull, g_pluginDir, sizeof(g_pluginDir));

    char logPath[MAX_PATH * 2];
    snprintf(logPath, sizeof(logPath), "%s\\fake_host.log", g_baseDir);
    g_log = fopen(logPath, "w");

    HostLog("fake_host: loading %s", dllFull);

    HMODULE exe = GetModuleHandleA(nullptr);
    auto* dos = (IMAGE_DOS_HEADER*)exe;
    auto* nt  = (IMAGE_NT_HEADERS64*)((BYTE*)exe + dos->e_lfanew);

    static TsmHost host = {};
    host.apiVersion        = TSM_API_VERSION;
    host.structSize        = sizeof(TsmHost);
    host.exeModule         = exe;
    host.exeBase           = (unsigned char*)exe;
    host.exeSize           = nt->OptionalHeader.SizeOfImage;
    host.engineModule      = nullptr;
    host.baseDir           = g_baseDir;
    host.pluginDir         = g_pluginDir;
    host.log               = HostLog;
    host.findIatSlot       = FindIatSlotStub;
    host.patchIat          = PatchIatStub;
    host.installInlineHook = InstallInlineHookStub;
    host.allocNear         = AllocNearImpl;
    host.readablePtr       = ReadablePtrImpl;
    host.faultFilter       = FaultFilterImpl;
    host.configInt         = ConfigIntImpl;
    host.configString      = ConfigStringImpl;
    host.provide           = ProvideImpl;
    host.consume           = ConsumeImpl;
    host.vfsRoot           = g_baseDir;

    HMODULE mod = LoadLibraryA(dllFull);
    if (!mod)
    {
        HostLog("FAIL: LoadLibrary error %lu (126 = a dependency DLL is missing)", GetLastError());
        return 2;
    }

    auto apiFn   = (TsmPluginApiVersionFn)(void*)GetProcAddress(mod, TSM_EXPORT_APIVERSION);
    auto initFn  = (TsmPluginInitFn)(void*)GetProcAddress(mod, TSM_EXPORT_INIT);
    auto startFn = (TsmPluginStartFn)(void*)GetProcAddress(mod, TSM_EXPORT_START);
    if (!apiFn || !initFn)
    {
        HostLog("FAIL: not a tesmioloader plugin (missing %s or %s)", TSM_EXPORT_APIVERSION, TSM_EXPORT_INIT);
        return 3;
    }

    unsigned api = apiFn();
    if (api < TSM_API_VERSION_MIN || api > TSM_API_VERSION)
    {
        HostLog("FAIL: plugin reports API %u, host takes %u..%u", api, TSM_API_VERSION_MIN, TSM_API_VERSION);
        return 4;
    }
    HostLog("plugin API %u ok", api);

    TsmPluginInfo info = {};
    int rc = initFn(&host, &info);
    HostLog("Init -> %d  (name %s, version %s)", rc, info.name ? info.name : "?",
            info.version ? info.version : "?");
    if (rc != 0) { HostLog("plugin declined"); return 5; }

    if (startFn)
    {
        rc = startFn();
        HostLog("Start -> %d", rc);
        if (rc != 0) return 6;
    }

    HostLog("OK - now check wrsr_rail_mod.log beside fake_host.exe");
    // No FreeLibrary: a plugin that hooked anything may never be unloaded.
    return 0;
}
