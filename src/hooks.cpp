#include "hooks.h"

#include <windows.h>

#include <MinHook.h>

#include "logger.h"
#include "tesmio_api.h"

namespace wrsr::hooks {
namespace {

const TsmHost* g_host     = nullptr;
bool           g_mhInited = false;

// ---- self-test target ------------------------------------------------------
// Big enough (>= 5 bytes before any ret) for MinHook's rel32 jump, never
// inlined, and only ever called through a volatile pointer so the compiler
// cannot fold the call away or clone it.
volatile int g_sink = 0;

__attribute__((noinline, used)) int SelfTestTarget(int a, int b)
{
    g_sink = a;
    int r  = a * 31 + b;
    g_sink = r;
    return r;
}

using SelfTestFn = int (*)(int, int);
SelfTestFn g_selfTestOrig = nullptr;

int SelfTestDetour(int a, int b) { return g_selfTestOrig(a, b) + 1000000; }

}  // namespace

void SetHost(const TsmHost* host) { g_host = host; }

bool InitMinHook()
{
    if (g_mhInited) return true;
    MH_STATUS s = MH_Initialize();
    if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED)
    {
        Log("hook  MH_Initialize failed: %s", MH_StatusToString(s));
        return false;
    }
    g_mhInited = true;
    Log("hook  MinHook initialised");
    return true;
}

bool InstallMinHook(const char* label, void* target, void* detour, void** original)
{
    if (!InitMinHook()) return false;

    MH_STATUS s = MH_CreateHook(target, detour, original);
    if (s != MH_OK)
    {
        Log("hook  %-24s MH_CreateHook(%p) failed: %s", label, target, MH_StatusToString(s));
        return false;
    }
    s = MH_EnableHook(target);
    if (s != MH_OK)
    {
        Log("hook  %-24s MH_EnableHook failed: %s", label, MH_StatusToString(s));
        MH_RemoveHook(target);
        return false;
    }
    Log("hook  %-24s ok (minhook) at %p", label, target);
    return true;
}

bool InstallHostHook(const char* label, void* target, void* detour, void** original,
                     const unsigned char* expect, size_t stolen)
{
    if (!g_host || !g_host->installInlineHook)
    {
        Log("hook  %-24s no loader host - refusing (game code must go through the host)", label);
        return false;
    }
    int ok = g_host->installInlineHook(target, detour, original, expect, stolen, label);
    Log("hook  %-24s %s (host) at %p", label, ok ? "ok" : "REFUSED", target);
    return ok != 0;
}

bool RunMinHookSelfTest()
{
    Log("self  MinHook self-test: start");
    if (!InitMinHook()) return false;

    SelfTestFn volatile call = &SelfTestTarget;
    const int expected = 7 * 31 + 5;

    int before = call(7, 5);
    if (before != expected)
    {
        Log("self  FAIL: unhooked call returned %d, expected %d", before, expected);
        return false;
    }

    void* target = (void*)&SelfTestTarget;
    if (!InstallMinHook("selftest", target, (void*)&SelfTestDetour, (void**)&g_selfTestOrig))
    {
        Log("self  FAIL: could not install");
        return false;
    }

    int hooked = call(7, 5);
    bool okHooked = hooked == expected + 1000000;
    Log("self  hooked call   -> %d (%s)", hooked, okHooked ? "ok" : "FAIL");

    MH_STATUS s = MH_DisableHook(target);
    int after   = call(7, 5);
    bool okAfter = s == MH_OK && after == expected;
    Log("self  unhooked call -> %d (%s)", after, okAfter ? "ok" : "FAIL");

    MH_RemoveHook(target);

    bool ok = okHooked && okAfter;
    Log("self  MinHook self-test: %s", ok ? "PASSED" : "FAILED");
    return ok;
}

}  // namespace wrsr::hooks
