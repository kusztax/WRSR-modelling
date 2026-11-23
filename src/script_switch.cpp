#include "script_switch.h"

#include <string.h>
#include <wchar.h>

#include "aob_scan.h"
#include "logger.h"
#include "tesmio_api.h"

namespace wrsr::script_switch {
namespace {

// cmp edi,3; jge; mov dl,1; lea rcx,[rip+world]; call; mov dl,1; lea rcx,[rip+..]
// (Rail Operations, matched on 1.1.1.9 and 1.1.1.13). The world is the lea target.
const char kSigWorld[] =
    "83 FF 03 7D ?? B2 01 48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? B2 01 48 8D 0D ?? ?? ?? ??";

constexpr uintptr_t WORLD_SCRIPTS = 0x13BB8;
constexpr uintptr_t SCRIPT_FILE   = 0xB8;
constexpr uintptr_t SCRIPT_NAME   = 0xC0;
const wchar_t kFlag[] = L"RailPriority_OffFlag";

const TsmHost* g_host  = nullptr;
BYTE*          g_world = nullptr;

bool Readable(const void* p, size_t n)
{
    if (!p) return false;
    if (g_host && g_host->readablePtr) return g_host->readablePtr(p, n) != 0;
    return false;
}

template <class T>
bool Read(const BYTE* base, uintptr_t off, T& out)
{
    if (!base || !Readable(base + off, sizeof(T))) return false;
    memcpy(&out, base + off, sizeof(T));
    return true;
}

bool ScriptIsFlag(BYTE* script)
{
    const uintptr_t at[2] = {SCRIPT_FILE, SCRIPT_NAME};
    for (uintptr_t off : at)
    {
        wchar_t* p = nullptr;
        if (!Read(script, off, p) || !p) continue;
        wchar_t buf[200];
        size_t  n = 0;
        while (n + 1 < 200 && Readable(p + n, sizeof(wchar_t)) && p[n]) { buf[n] = p[n]; ++n; }
        buf[n] = 0;
        if (wcsstr(buf, kFlag)) return true;
    }
    return false;
}

ULONGLONG g_lastScan = 0;
bool      g_off      = false;
int       g_logged   = -1;

}  // namespace

bool Init(const TsmHost* host, HMODULE exe)
{
    g_host = host;
    aob::Hit hit = aob::ScanModule(exe, ".text", kSigWorld, "world (scripts)");
    if (hit.result != aob::Result::Unique)
    {
        Log("prio  Scripts menu switch unavailable (world not found) - priority follows the ini only");
        return false;
    }
    g_world = aob::RipTarget(hit.addr + 10, hit.addr + 14);
    Log("prio  Scripts menu switch ready (world at +0x%llX)", (unsigned long long)(g_world - (BYTE*)exe));
    return true;
}

void* World() { return g_world; }

bool PriorityOffByScript()
{
    if (!g_world) return false;
    ULONGLONG now = GetTickCount64();
    if (now - g_lastScan < 500) return g_off;
    g_lastScan = now;

    BYTE** b = nullptr;
    BYTE** e = nullptr;
    bool   off = false;
    if (Read(g_world, WORLD_SCRIPTS, b) && Read(g_world, WORLD_SCRIPTS + 8, e) && b && e > b && e - b <= 4096 &&
        Readable(b, (size_t)(e - b) * sizeof(BYTE*)))
    {
        for (BYTE** p = b; p != e && !off; ++p)
            if (*p && ScriptIsFlag(*p)) off = true;
    }
    g_off = off;
    if ((int)off != g_logged)
    {
        g_logged = (int)off;
        Log("prio  Scripts menu: priority %s", off ? "OFF" : "ON");
    }
    return g_off;
}

}  // namespace wrsr::script_switch
