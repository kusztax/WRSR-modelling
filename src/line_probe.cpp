#include "line_probe.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "logger.h"

namespace wrsr::line_probe {
namespace {

constexpr int    kBefore = 0x800, kAfter = 0x800, kSpan = kBefore + kAfter;
constexpr int    kMaxHits = 96;
float  g_len[4];
int    g_nLen = 0;

struct Hit
{
    BYTE* at;          // the length value
    int   which;       // index into g_len; -1 name string, -2 pointer to a name
    bool  dbl;
    BYTE  snap[kSpan]; // memory at [at - kBefore, at + kAfter)
};
Hit g_hit[kMaxHits];
int g_nHit = 0;

bool ReadMem(const void* at, void* out, size_t n)
{
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), at, out, n, &got) && got == n;
}

void Scan()
{
    const SIZE_T kChunk = 1 << 20;
    static BYTE* chunk = (BYTE*)VirtualAlloc(nullptr, kChunk, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!chunk) return;
    MEMORY_BASIC_INFORMATION mi;
    for (BYTE* p = nullptr; g_nHit < 48 && VirtualQuery(p, &mi, sizeof(mi)) == sizeof(mi);
         p = (BYTE*)mi.BaseAddress + mi.RegionSize)
    {
        if (mi.State != MEM_COMMIT || mi.Type != MEM_PRIVATE || (mi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) continue;
        if (!(mi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE))) continue;
        BYTE* base = (BYTE*)mi.BaseAddress;
        for (SIZE_T off = 0; g_nHit < 48 && off < mi.RegionSize; off += kChunk)
        {
            SIZE_T want = mi.RegionSize - off < kChunk ? mi.RegionSize - off : kChunk;
            SIZE_T got = 0;
            if (base + off == chunk || !ReadProcessMemory(GetCurrentProcess(), base + off, chunk, want, &got)) continue;
            for (SIZE_T i = 0; i + 8 <= got && g_nHit < 48; i += 4)
                for (int w = 0; w < g_nLen; ++w)
                {
                    float f;
                    memcpy(&f, chunk + i, 4);
                    double d = 0;
                    if ((i & 7) == 0) memcpy(&d, chunk + i, 8);
                    bool isF = f > g_len[w] - 0.02f && f < g_len[w] + 0.02f;
                    bool isD = (i & 7) == 0 && d > g_len[w] - 0.02 && d < g_len[w] + 0.02;
                    if (!isF && !isD) continue;
                    Hit& h = g_hit[g_nHit];
                    h.at = base + off + i;
                    h.which = w;
                    h.dbl = isD;
                    if (!ReadMem(h.at - kBefore, h.snap, kSpan)) continue;
                    ++g_nHit;
                    Log("line probe: hit %d - %.2f m (%s) at %p", g_nHit - 1, g_len[w], isD ? "double" : "float", (void*)h.at);
                }
        }
    }
}


// The lines' names, as the game shows them (wide strings): unique to a line,
// unlike its length. "Kromňová Buszmegálló - Krom..." / "Lenin Ltp. megálló ..(2).. Vas..."
const wchar_t* const kNames[] = {L"Kromňová Buszmegálló - Krom",
                                 L"Lenin Ltp. megálló ..(2).. Vas"};

void AddHit(BYTE* at, int which, const char* what)
{
    if (g_nHit >= kMaxHits) return;
    Hit& h = g_hit[g_nHit];
    h.at = at;
    h.which = which;
    h.dbl = false;
    if (!ReadMem(at - kBefore, h.snap, kSpan)) return;
    ++g_nHit;
    Log("line probe: hit %d - %s at %p", g_nHit - 1, what, (void*)at);
}

// Second pass: the names, and every pointer to a found name.
void ScanNames()
{
    BYTE* found[8];
    int   nFound = 0;
    const SIZE_T kChunk = 1 << 20;
    static BYTE* chunk = (BYTE*)VirtualAlloc(nullptr, kChunk, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!chunk) return;
    for (int pass = 0; pass < 2; ++pass)
    {
        MEMORY_BASIC_INFORMATION mi;
        for (BYTE* p = nullptr; g_nHit < kMaxHits && VirtualQuery(p, &mi, sizeof(mi)) == sizeof(mi);
             p = (BYTE*)mi.BaseAddress + mi.RegionSize)
        {
            if (mi.State != MEM_COMMIT || mi.Type != MEM_PRIVATE || (mi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) continue;
            if (!(mi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE))) continue;
            BYTE* base = (BYTE*)mi.BaseAddress;
            for (SIZE_T off = 0; g_nHit < kMaxHits && off < mi.RegionSize; off += kChunk)
            {
                SIZE_T want = mi.RegionSize - off < kChunk ? mi.RegionSize - off : kChunk;
                SIZE_T got = 0;
                if (base + off == chunk || !ReadProcessMemory(GetCurrentProcess(), base + off, chunk, want, &got)) continue;
                if (pass == 0)
                {
                    for (const wchar_t* nm : kNames)
                    {
                        size_t len = wcslen(nm) * 2;
                        for (SIZE_T i = 0; i + len <= got && nFound < 8; i += 2)
                            if (chunk[i] == (BYTE)nm[0] && memcmp(chunk + i, nm, len) == 0)
                            {
                                found[nFound++] = base + off + i;
                                AddHit(base + off + i, -1, nm == kNames[0] ? "bus line name" : "tram line name");
                            }
                    }
                }
                else
                {
                    for (SIZE_T i = 0; i + 8 <= got; i += 8)
                    {
                        BYTE* q;
                        memcpy(&q, chunk + i, 8);
                        for (int k = 0; k < nFound; ++k)
                            if (q == found[k] && base + off + i != (BYTE*)&found[k])
                                AddHit(base + off + i, -2, "pointer to a line name");
                    }
                }
            }
        }
        if (!nFound) { Log("line probe: line names not found in memory"); return; }
    }
}

DWORD WINAPI Thread(void*)
{
    Sleep(15000);
    for (int round = 0;; ++round)
    {
        if (g_nHit == 0)
        {
            Scan();
            ScanNames();
            if (g_nHit == 0)
            {
                if (round % 6 == 0) Log("line probe: no line of that length yet (load the save)");
                Sleep(10000);
                continue;
            }
            Log("line probe: %d hits - now toggle the spacing switch a few times, ~20 s apart", g_nHit);
        }
        Sleep(5000);
        int lines = 0;
        for (int k = 0; k < g_nHit; ++k)
        {
            Hit& h = g_hit[k];
            BYTE now[kSpan];
            if (!ReadMem(h.at - kBefore, now, kSpan)) continue;
            for (int i = 0; i < kSpan; ++i)
            {
                if (now[i] == h.snap[i]) continue;
                // one line per changed 4-byte word
                int w = i & ~3;
                if (lines < 60)
                {
                    unsigned o = 0, n = 0;
                    memcpy(&o, h.snap + w, 4);
                    memcpy(&n, now + w, 4);
                    float fo, fn;
                    memcpy(&fo, &o, 4);
                    memcpy(&fn, &n, 4);
                    Log("line probe: hit %d %c0x%03X: %08X -> %08X (int %d -> %d, float %g -> %g)", k,
                        w < kBefore ? '-' : '+', w < kBefore ? kBefore - w : w - kBefore, o, n, (int)o, (int)n, fo, fn);
                }
                ++lines;
                i = w + 3;
            }
            memcpy(h.snap, now, kSpan);
        }
        if (lines > 60) Log("line probe: ... %d changes in all this round", lines);
    }
    return 0;
}

}  // namespace

void Start(const char* csv)
{
    char buf[128];
    strncpy(buf, csv, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    for (char* t = strtok(buf, ", ;"); t && g_nLen < 4; t = strtok(nullptr, ", ;"))
    {
        float v = (float)atof(t);
        if (v > 1.0f) g_len[g_nLen++] = v;
    }
    if (!g_nLen) { Log("line probe: no line_lengths in the ini - off"); return; }
    HANDLE h = CreateThread(nullptr, 0, Thread, nullptr, 0, nullptr);
    if (h) CloseHandle(h);
    Log("line probe on: looking for lines of %d length(s), first %.2f m", g_nLen, g_len[0]);
}

}  // namespace wrsr::line_probe
