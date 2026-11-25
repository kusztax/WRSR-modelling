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

// 1.1.1.13 layout; other versions are found at run time (Calibrate).
uintptr_t g_scriptsOff = 0x13BB8;   // world: std::vector<script*>
uintptr_t g_fileOff    = 0xB8;      // script: wchar_t* file name (".txt")
bool      g_calibrated = false;
// Priority is ON only while this runs (v1.2: started by the ON button once
// the research is done). RailPriority_OffFlag.txt from 1.0/1.1 no longer matters.
const wchar_t kFlag[] = L"RailPriority_OnFlag";
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
    const uintptr_t at[2] = {g_fileOff, g_fileOff + 8};
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


bool ReadRaw(const void* at, void* out, size_t n)
{
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), at, out, n, &got) && got == n;
}

// A wide string that ends in ".txt" (a script file name), at most 260 chars.
bool IsTxtName(const wchar_t* p)
{
    if (!p) return false;
    wchar_t buf[264];
    if (!ReadRaw(p, buf, 16)) return false;
    size_t n = 0;
    for (; n < 260; ++n)
    {
        if (n % 8 == 0 && n && !ReadRaw(p + n, buf + n, 16)) return false;
        if (!buf[n]) break;
        if (buf[n] < 32 || buf[n] > 0x2FFF) return false;
    }
    return n >= 5 && n < 260 && _wcsnicmp(buf + n - 4, L".txt", 4) == 0;
}

// Where in a script object is its file name? -1 when nowhere in 0x40..0x200.
int FileNameOffset(BYTE* script)
{
    BYTE o[0x200];
    if (!ReadRaw(script, o, sizeof(o))) return -1;
    for (int off = 0x40; off < 0x200; off += 8)
    {
        wchar_t* p;
        memcpy(&p, o + off, 8);
        if ((uintptr_t)p > 0x10000 && ((uintptr_t)p & 1) == 0 && IsTxtName(p)) return off;
    }
    return -1;
}

// Is world+off the running-scripts vector? Its elements all name a ".txt"
// file at the same offset, and one of them is a running flag or watcher of
// this mod (so the Scripts menu's list of buttons cannot be mistaken for it:
// that holds RailPriority_On.txt etc., never a *Flag.txt / *Watch.txt).
bool LooksLikeScripts(uintptr_t off, int* fileOff)
{
    BYTE* v[3];
    if (!ReadRaw(g_world + off, v, sizeof(v))) return false;
    BYTE* b = v[0]; BYTE* e = v[1]; BYTE* c = v[2];
    if (!b || e <= b || c < e || ((e - b) & 7) || (e - b) / 8 > 4096) return false;
    size_t n = (size_t)(e - b) / 8;
    BYTE* obj;
    if (!ReadRaw(b, &obj, 8) || !obj) return false;
    int fo = FileNameOffset(obj);
    if (fo < 0) return false;
    bool ours = false;
    for (size_t k = 0; k < n && k < 64; ++k)
    {
        wchar_t* p;
        wchar_t name[264];
        if (!ReadRaw(b + 8 * k, &obj, 8) || !obj || !ReadRaw(obj + fo, &p, 8) || !IsTxtName(p)) return false;
        if (!ReadRaw(p, name, sizeof(name) - 2)) continue;
        name[263] = 0;
        if (wcsstr(name, L"RailPriority_") && (wcsstr(name, L"Flag.txt") || wcsstr(name, L"Watch.txt"))) ours = true;
    }
    *fileOff = fo;
    return ours;
}

// The running-scripts list moves between game versions: check the known
// place first, then look through the first 256 KiB of the world. Retried
// (every 2 s) until some script runs.
void Calibrate()
{
    static ULONGLONG last = 0;
    ULONGLONG now = GetTickCount64();
    if (g_calibrated || now - last < 5000) return;
    last = now;
    int fo;
    if (LooksLikeScripts(g_scriptsOff, &fo))
    {
        g_fileOff = (uintptr_t)fo;
        g_calibrated = true;
        Log("prio  running scripts at world+0x%llX, file name at +0x%X", (unsigned long long)g_scriptsOff, fo);
        return;
    }
    // One bulk read, then only plausible (begin <= end <= cap, small) triples
    // are followed: this runs on the game thread, so keep it cheap.
    static BYTE block[0x40000];
    static bool readable[0x40000 / 0x1000];
    for (size_t pg = 0; pg < sizeof(readable); ++pg)
        readable[pg] = ReadRaw(g_world + pg * 0x1000, block + pg * 0x1000, 0x1000);
    for (uintptr_t off = 0; off + 24 <= sizeof(block); off += 8)
    {
        if (off == g_scriptsOff || !readable[off / 0x1000] || !readable[(off + 23) / 0x1000]) continue;
        uintptr_t b, e, c;
        memcpy(&b, block + off, 8);
        memcpy(&e, block + off + 8, 8);
        memcpy(&c, block + off + 16, 8);
        if (b < 0x10000 || e <= b || c < e || ((e - b) & 7) || e - b > 8 * 256 || (b >> 47)) continue;
        if (LooksLikeScripts(off, &fo))
        {
            g_scriptsOff = off;
            g_fileOff    = (uintptr_t)fo;
            g_calibrated = true;
            Log("prio  running scripts found at world+0x%llX (not the 1.1.1.13 place), file name at +0x%X",
                (unsigned long long)off, fo);
            return;
        }
    }
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
    Calibrate();

    BYTE** b = nullptr;
    BYTE** e = nullptr;
    bool   on   = false;
    int    hold = 0;
    if (g_calibrated && Read(g_world, g_scriptsOff, b) && Read(g_world, g_scriptsOff + 8, e) && b && e > b && e - b <= 4096 &&
        Readable(b, (size_t)(e - b) * sizeof(BYTE*)))
    {
        for (BYTE** p = b; p != e; ++p)
        {
            if (!*p) continue;
            wchar_t text[480];
            ScriptText(*p, text, sizeof(text) / sizeof(text[0]));
            if (wcsstr(text, kFlag)) on = true;
            for (int v : kHoldValues)
            {
                wchar_t name[48];
                _snwprintf(name, 47, L"RailPriority_Hold%dFlag", v);
                name[47] = 0;
                if (wcsstr(text, name)) hold = v;
            }
        }
    }
    bool off = !on;
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
