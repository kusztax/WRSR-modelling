// dllmain.cpp - entry points of wrsr_rail_mod.dll.
//
// Loader contract (TesmioLoader API 3/4; RML hosts the same API through its
// Tesmio compatibility layer):
//
//   TsmPluginApiVersion()      required. Return the constant, nothing else.
//   TsmPluginInit(host, info)  required. Read config, bind the host. No hooks.
//   TsmPluginStart()           optional. Runs after every plugin's Init:
//                              install hooks here.
//
// DllMain only opens our log file: it runs under the loader lock, where
// anything beyond kernel32 (threads, LoadLibrary, MinHook) invites deadlocks.
//
// Return values: non-zero from Init/Start means "nothing hooked, unload me".
// Once any patch is live that would make the game jump into freed memory, so
// Start returns 0 from the moment it has touched code.

#include "tesmio_plugin.h"   // TesmioLoader (GPL-3.0): binds H, g_exeBase, ...

#include "aob_scan.h"
#include "env_info.h"
#include "hooks.h"
#include "lang.h"
#include "logger.h"
#include "rail_probe.h"
#include "signal_priority.h"
#include "research.h"
#include "line_spacing.h"

using wrsr::Log;

namespace {

HMODULE g_self        = nullptr;
bool    g_initCalled  = false;
bool    g_startCalled = false;

const char kIni[]     = "plugins\\wrsr_rail_mod.ini";
const char kSection[] = "wrsr_rail_mod";

bool Readable(const void* p, size_t n)
{
    if (H && H->readablePtr) return H->readablePtr(p, n) != 0;
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) return false;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    return (const BYTE*)p + n <= (const BYTE*)mbi.BaseAddress + mbi.RegionSize;
}

// Read-only end-to-end check of the scanner against the real game: locate the
// infrastructure speed-limit lookup (signature from the railspeed plugin,
// SosseTurner, GPL-3.0) and confirm that its first rip-relative load reads the
// stock 120 km/h tunnel limit. Writes nothing. Not finding it is not an error
// for this skeleton - it is information (different game build, or fake_host).
void ProbeRailSpeedLookup()
{
    static const char kSig[] =
        "0F B6 44 24 28  F3 0F 10 15 ?? ?? ?? ??  F3 0F 10 0D ?? ?? ?? ??";

    wrsr::aob::Hit hit = wrsr::aob::ScanModule(g_exe, ".text", kSig, "probe rail speed lookup");
    if (hit.result != wrsr::aob::Result::Unique) return;

    BYTE* dispAt = hit.addr + 9;    // movss xmm2, [rip+disp32]
    BYTE* target = wrsr::aob::RipTarget(dispAt, dispAt + 4);
    if (!Readable(target, sizeof(float)))
    {
        Log("probe rail speed lookup: constant at %p not readable", (void*)target);
        return;
    }
    float v;
    memcpy(&v, target, sizeof(v));
    Log("probe rail speed lookup: first constant = %.1f km/h (%s)", v,
        v == 120.0f ? "matches stock 120 - offsets and memory reads verified" : "UNEXPECTED");
}

}  // namespace

// ---------------------------------------------------------------- exports

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

    info->name    = "wrsr_rail_mod";
    info->version = WRSR_MOD_VERSION;

    Log("init  host API %u (we were built against %u), host table %u bytes",
        host->apiVersion, TSM_API_VERSION, host->structSize);
    Log("init  loader dir %s", host->baseDir ? host->baseDir : "(null)");

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
    Log("start begin");

    if (H->configInt(kIni, kSection, "minhook_selftest", 1))
        wrsr::hooks::RunMinHookSelfTest();

    ProbeRailSpeedLookup();

    const int logLimit = H->configInt(kIni, kSection, "census_log_limit", 3000);

    wrsr::rail_probe::Config rc;
    rc.trainCensus = H->configInt(kIni, kSection, "train_census", 0) != 0;
    rc.logLimit    = logLimit;
    bool hooked = wrsr::rail_probe::Start(H, g_exe, rc);

    wrsr::signal_priority::Config sc;
    sc.census     = H->configInt(kIni, kSection, "signal_census", 0) != 0;
    sc.priority   = H->configInt(kIni, kSection, "priority", 1) != 0;
    sc.maxHoldSec = H->configInt(kIni, kSection, "max_hold_s", 60);
    sc.aspect     = H->configInt(kIni, kSection, "signal_aspect", 0) != 0;
    sc.logLimit   = logLimit;
    hooked |= wrsr::signal_priority::Start(H, g_exe, sc);

    // Texts for the Scripts menu notifications, in the game's language.
    if (sc.priority)
    {
        wrsr::research::Init(g_self, H->configInt(kIni, kSection, "research", 1) != 0);
        hooked |= wrsr::lang::Init(H, g_exe, g_engine);
    }

    // The game's line spacing for tram and railcar lines.
    if (H->configInt(kIni, kSection, "line_spacing", 1))
        hooked |= wrsr::line_spacing::Start(H, g_exe);

    Log("start done - %s", hooked ? "hooks live (see prio / rail lines above)" : "no game code patched");
    return 0;
}

// ---------------------------------------------------------------- DllMain
// extern "C" is required with MinGW: a C++-mangled DllMain is silently ignored
// and the CRT's empty default runs instead.

extern "C" BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        g_self = (HMODULE)inst;
        DisableThreadLibraryCalls(inst);
        wrsr::log::Open(inst);
        Log("attach wrsr_rail_mod %s loaded at %p into pid %lu", WRSR_MOD_VERSION,
            (void*)inst, (unsigned long)GetCurrentProcessId());
        break;

    case DLL_PROCESS_DETACH:
        // reserved != null: the process is exiting; other threads are gone.
        Log("detach (%s) - loader called Init: %s, Start: %s",
            reserved ? "process exit" : "FreeLibrary",
            g_initCalled ? "yes" : "no", g_startCalled ? "yes" : "no");
        wrsr::log::Close();
        break;
    }
    return TRUE;
}
