// spacing_main.cpp - entry points of wrsr_tram_spacing.dll ("Tram & Railcar
// Spacing"): the game's own line spacing for tram and railcar lines.
// Same loader contract as dllmain.cpp (TesmioLoader API 3/4, RML's Tesmio
// layer): Init binds the host and reads the ini, Start installs the hooks.

#include "tesmio_plugin.h"   // TesmioLoader (GPL-3.0): binds H, g_exe, ...

#include "env_info.h"
#include "hooks.h"
#include "line_spacing.h"
#include "logger.h"

using wrsr::Log;

namespace {

HMODULE g_self        = nullptr;
bool    g_initCalled  = false;
bool    g_startCalled = false;

const char kIni[]     = "plugins\\wrsr_tram_spacing.ini";
const char kSection[] = "wrsr_tram_spacing";

}  // namespace

extern "C" __declspec(dllexport) unsigned TsmPluginApiVersion(void)
{
    return TSM_API_VERSION;
}

extern "C" __declspec(dllexport) int TsmPluginInit(const TsmHost* host, TsmPluginInfo* info)
{
    g_initCalled = true;
    TsmBind(host);
    wrsr::log::SetHostSink(host->log);
    wrsr::hooks::SetHost(host);

    info->name    = "wrsr_tram_spacing";
    info->version = WRSR_MOD_VERSION;

    Log("init  host API %u (we were built against %u)", host->apiVersion, TSM_API_VERSION);
    if (!H->configInt(kIni, kSection, "enabled", 1))
    {
        Log("init  enabled = 0 in %s - declining, nothing hooked", kIni);
        return 1;
    }
    wrsr::env::LogAll(g_exe, g_self);
    return 0;
}

extern "C" __declspec(dllexport) int TsmPluginStart(void)
{
    g_startCalled = true;
    bool hooked = wrsr::line_spacing::Start(H, g_exe);
    Log("start done - %s", hooked ? "hooks live" : "no game code patched");
    return hooked ? 0 : 1;   // nothing patched: the loader may unload us
}

// extern "C" is required with MinGW: a C++-mangled DllMain is silently ignored.
extern "C" BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        g_self = (HMODULE)inst;
        DisableThreadLibraryCalls(inst);
        wrsr::log::Open(inst);
        Log("attach wrsr_tram_spacing %s loaded at %p into pid %lu", WRSR_MOD_VERSION,
            (void*)inst, (unsigned long)GetCurrentProcessId());
        break;
    case DLL_PROCESS_DETACH:
        Log("detach (%s) - loader called Init: %s, Start: %s",
            reserved ? "process exit" : "FreeLibrary",
            g_initCalled ? "yes" : "no", g_startCalled ? "yes" : "no");
        wrsr::log::Close();
        break;
    }
    return TRUE;
}
