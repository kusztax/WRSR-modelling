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
const int     kHoldValues[] = {30, 60, 90, 120, 180};

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

// The script's file and name, as one ASCII-ish string (empty on failure).
void ScriptText(BYTE* script, wchar_t* out, size_t cap)
{
    out[0] = 0;
    size_t len = 0;
    const uintptr_t at[2] = {SCRIPT_FILE, SCRIPT_NAME};
    for (uintptr_t off : at)
    {
        wchar_t* p = nullptr;
        if (!Read(script, off, p) || !p) continue;
        for (size_t n = 0; len + 2 < cap && Readable(p + n, sizeof(wchar_t)) && p[n] && n < 200; ++n)
            out[len++] = p[n];
        out[len++] = L'|';
    }
    out[len < cap ? len : cap - 1] = 0;
}

ULONGLONG g_lastScan  = 0;
bool      g_off       = false;
int       g_hold      = 0;
int       g_logged    = -1;
int       g_holdLogged = -1;

void Scan()
{
    if (!g_world) return;
    ULONGLONG now = GetTickCount64();
    if (now - g_lastScan < 500) return;
    g_lastScan = now;

    BYTE** b = nullptr;
    BYTE** e = nullptr;
    bool   off  = false;
    int    hold = 0;
    if (Read(g_world, WORLD_SCRIPTS, b) && Read(g_world, WORLD_SCRIPTS + 8, e) && b && e > b && e - b <= 4096 &&
        Readable(b, (size_t)(e - b) * sizeof(BYTE*)))
    {
        for (BYTE** p = b; p != e; ++p)
        {
            if (!*p) continue;
            wchar_t text[480];
            ScriptText(*p, text, sizeof(text) / sizeof(text[0]));
            if (wcsstr(text, kFlag)) off = true;
            for (int v : kHoldValues)
            {
                wchar_t name[48];
                _snwprintf(name, 47, L"RailPriority_Hold%dFlag", v);
                name[47] = 0;
                if (wcsstr(text, name)) hold = v;
            }
        }
    }
    g_off  = off;
    g_hold = hold;
    if ((int)off != g_logged)
    {
        g_logged = (int)off;
        Log("prio  Scripts menu: priority %s", off ? "OFF" : "ON");
    }
    if (hold != g_holdLogged)
    {
        g_holdLogged = hold;
        if (hold) Log("prio  Scripts menu: maximum wait %d s", hold);
        else      Log("prio  Scripts menu: maximum wait from the ini");
    }
}

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
    Scan();
    return g_off;
}

int HoldSecondsByScript()
{
    Scan();
    return g_hold;
}

}  // namespace wrsr::script_switch
