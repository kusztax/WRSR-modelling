// line_probe.cpp - TEST ONLY (line_probe = 1). Differential scan with the game
// PAUSED: every 10 s the whole writable memory is fingerprinted page by page.
// The log tells the player when to toggle the line's spacing switch and when
// to keep still; pages that change in every toggle window and never in a
// still window hold the switch, and their changed bytes are logged.
#include "line_probe.h"

#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "logger.h"

namespace wrsr::line_probe {
namespace {

constexpr size_t kMaxPages = 1u << 21;   // 8 GiB of 4 KiB pages
constexpr int    kMaxCand  = 4096;       // pages kept in full (16 MiB)

struct PageHash { uintptr_t addr; uint64_t hash; };

PageHash* g_prev  = nullptr;   // VirtualAlloc'd: never inside what we scan
PageHash* g_cur   = nullptr;
size_t    g_nPrev = 0, g_nCur = 0;
BYTE*     g_chunk = nullptr;   // 1 MiB read buffer

// Candidates: page address, its content in the "on" and "off" states seen.
struct Cand { uintptr_t addr; bool alive; int hits; BYTE before[4096]; BYTE after[4096]; };
Cand* g_cand  = nullptr;
int   g_nCand = 0;

bool Ours(uintptr_t a)
{
    auto in = [a](const void* base, size_t len) {
        return a >= (uintptr_t)base && a < (uintptr_t)base + len; };
    return in(g_prev, kMaxPages * sizeof(PageHash)) || in(g_cur, kMaxPages * sizeof(PageHash)) ||
           in(g_chunk, 1 << 20) || in(g_cand, kMaxCand * sizeof(Cand));
}

uint64_t Fnv(const BYTE* p, size_t n)
{
    uint64_t h = 1469598103934665603ull;
    const uint64_t* q = (const uint64_t*)p;
    for (size_t i = 0; i < n / 8; ++i) { h ^= q[i]; h *= 1099511628211ull; }
    return h;
}

// Fingerprint every committed private writable page into g_cur.
void Snapshot()
{
    g_nCur = 0;
    MEMORY_BASIC_INFORMATION mi;
    NT_TIB* tib = (NT_TIB*)NtCurrentTeb();
    uintptr_t stackLo = (uintptr_t)tib->StackLimit, stackHi = (uintptr_t)tib->StackBase;
    for (BYTE* p = nullptr; VirtualQuery(p, &mi, sizeof(mi)) == sizeof(mi); p = (BYTE*)mi.BaseAddress + mi.RegionSize)
    {
        if (mi.State != MEM_COMMIT || mi.Type != MEM_PRIVATE || (mi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) continue;
        if (!(mi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE))) continue;
        uintptr_t base = (uintptr_t)mi.BaseAddress;
        if (Ours(base) || (base >= stackLo && base < stackHi)) continue;
        for (SIZE_T off = 0; off < mi.RegionSize; off += 1 << 20)
        {
            SIZE_T want = mi.RegionSize - off < (1u << 20) ? mi.RegionSize - off : (1u << 20);
            SIZE_T got = 0;
            if (!ReadProcessMemory(GetCurrentProcess(), (void*)(base + off), g_chunk, want, &got)) continue;
            for (SIZE_T pg = 0; pg + 4096 <= got && g_nCur < kMaxPages; pg += 4096)
                g_cur[g_nCur++] = PageHash{base + off + pg, Fnv(g_chunk + pg, 4096)};
        }
    }
}

// Pages whose fingerprint differs between g_prev and g_cur (both sorted by
// address); calls f(addr) for each.
template <class F>
int Changed(F f)
{
    size_t i = 0, j = 0;
    int n = 0;
    while (i < g_nPrev && j < g_nCur)
    {
        if (g_prev[i].addr < g_cur[j].addr) ++i;
        else if (g_prev[i].addr > g_cur[j].addr) ++j;
        else
        {
            if (g_prev[i].hash != g_cur[j].hash) { ++n; f(g_cur[j].addr); }
            ++i; ++j;
        }
    }
    return n;
}

bool ReadPage(uintptr_t a, BYTE* out) { SIZE_T got = 0; return ReadProcessMemory(GetCurrentProcess(), (void*)a, out, 4096, &got) && got == 4096; }

DWORD WINAPI Thread(void*)
{
    Sleep(20000);
    g_prev  = (PageHash*)VirtualAlloc(nullptr, kMaxPages * sizeof(PageHash), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    g_cur   = (PageHash*)VirtualAlloc(nullptr, kMaxPages * sizeof(PageHash), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    g_chunk = (BYTE*)VirtualAlloc(nullptr, 1 << 20, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    g_cand  = (Cand*)VirtualAlloc(nullptr, kMaxCand * sizeof(Cand), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!g_prev || !g_cur || !g_chunk || !g_cand) { Log("line probe: out of memory"); return 0; }

    Log("line probe: PAUSE THE GAME now and open the bus line's window. Starting in 20 s.");
    Sleep(20000);
    Snapshot();
    Log("line probe: baseline %zu pages", g_nCur);

    // still, still, TOGGLE, still, TOGGLE, still, TOGGLE, still
    const bool toggle[] = {false, false, true, false, true, false, true, false};
    const int  nSteps   = (int)(sizeof(toggle) / sizeof(toggle[0]));
    for (int s = 0; s < nSteps; ++s)
    {
        memcpy(g_prev, g_cur, g_nCur * sizeof(PageHash));
        g_nPrev = g_nCur;
        if (toggle[s]) Log("line probe: step %d/%d - CLICK THE SPACING SWITCH ONCE, NOW (you have 10 s)", s + 1, nSteps);
        else           Log("line probe: step %d/%d - do nothing for 10 s (game paused, mouse still)", s + 1, nSteps);
        Sleep(10000);
        Snapshot();

        if (toggle[s] && g_nCand == 0)
        {
            // first toggle: every changed page is a candidate
            int n = Changed([](uintptr_t a) {
                if (g_nCand >= kMaxCand) return;
                Cand& c = g_cand[g_nCand];
                c.addr = a; c.alive = true; c.hits = 1;
                if (ReadPage(a, c.after)) ++g_nCand;
            });
            Log("line probe: step %d: %d pages changed, %d kept as candidates", s + 1, n, g_nCand);
            // "before" = the state before this click: not known; filled at the next toggle
            for (int k = 0; k < g_nCand; ++k) memcpy(g_cand[k].before, g_cand[k].after, 4096);
            continue;
        }
        // later steps: a toggle must change the candidate, a still step must not
        int alive = 0, n = Changed([](uintptr_t) {});
        for (int k = 0; k < g_nCand; ++k)
        {
            Cand& c = g_cand[k];
            if (!c.alive) continue;
            BYTE now[4096];
            if (!ReadPage(c.addr, now)) { c.alive = false; continue; }
            bool diff = memcmp(now, c.after, 4096) != 0;
            if (toggle[s] != diff) { c.alive = false; continue; }
            if (toggle[s]) { memcpy(c.before, c.after, 4096); memcpy(c.after, now, 4096); ++c.hits; }
            ++alive;
        }
        Log("line probe: step %d: %d pages changed in all; %d candidates left", s + 1, n, alive);
    }

    // Report: the bytes that differ between the last two toggle states.
    int shown = 0;
    for (int k = 0; k < g_nCand && shown < 40; ++k)
    {
        Cand& c = g_cand[k];
        if (!c.alive) continue;
        for (int i = 0; i < 4096 && shown < 40; ++i)
            if (c.before[i] != c.after[i])
            {
                ++shown;
                Log("line probe: RESULT page %p +0x%03X: %02X -> %02X  (byte at %p)", (void*)c.addr, i, c.before[i],
                    c.after[i], (void*)(c.addr + i));
            }
    }
    Log("line probe: done (%d result bytes). You can unpause.", shown);
    return 0;
}

}  // namespace

void Start(const char*)
{
    HANDLE h = CreateThread(nullptr, 0, Thread, nullptr, 0, nullptr);
    if (h) CloseHandle(h);
    Log("line probe on: load the save; then follow the 'line probe:' lines in this log");
}

}  // namespace wrsr::line_probe
