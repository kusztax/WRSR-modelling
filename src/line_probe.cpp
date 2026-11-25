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
constexpr int    kMaxCand  = 4096;       // pages kept in full

struct PageHash { uintptr_t addr; uint64_t hash; };

PageHash* g_prev  = nullptr;   // VirtualAlloc'd: never inside what we scan
PageHash* g_cur   = nullptr;
size_t    g_nPrev = 0, g_nCur = 0;
BYTE*     g_chunk = nullptr;   // 1 MiB read buffer

// Candidates: page address, its content in the "on" and "off" states seen.
struct Cand { uintptr_t addr; bool alive; int hits; BYTE last[4096]; BYTE t[3][4096]; BYTE bad[4096]; };
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

// Thread stacks (any allocation holding a guard page): never candidates,
// never written. The first run wrote into the game's main stack and crashed it.
uintptr_t g_stackBase[256];
int       g_nStack = 0;

void FindStacks()
{
    g_nStack = 0;
    MEMORY_BASIC_INFORMATION mi;
    for (BYTE* p = nullptr; VirtualQuery(p, &mi, sizeof(mi)) == sizeof(mi); p = (BYTE*)mi.BaseAddress + mi.RegionSize)
        if (mi.State == MEM_COMMIT && (mi.Protect & PAGE_GUARD) && g_nStack < 256)
            g_stackBase[g_nStack++] = (uintptr_t)mi.AllocationBase;
}

bool IsStack(const MEMORY_BASIC_INFORMATION& mi)
{
    for (int i = 0; i < g_nStack; ++i)
        if ((uintptr_t)mi.AllocationBase == g_stackBase[i]) return true;
    return false;
}

// Fingerprint every committed private writable page into g_cur.
void Snapshot()
{
    FindStacks();
    g_nCur = 0;
    MEMORY_BASIC_INFORMATION mi;
    NT_TIB* tib = (NT_TIB*)NtCurrentTeb();
    uintptr_t stackLo = (uintptr_t)tib->StackLimit, stackHi = (uintptr_t)tib->StackBase;
    for (BYTE* p = nullptr; VirtualQuery(p, &mi, sizeof(mi)) == sizeof(mi); p = (BYTE*)mi.BaseAddress + mi.RegionSize)
    {
        if (mi.State != MEM_COMMIT || mi.Type != MEM_PRIVATE || (mi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) continue;
        if (!(mi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE))) continue;
        uintptr_t base = (uintptr_t)mi.BaseAddress;
        if (Ours(base) || (base >= stackLo && base < stackHi) || IsStack(mi)) continue;
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

    Log("line probe: load the save, PAUSE the game, open the bus line's window. The scan starts in 60 s.");
    for (int left = 45; left > 0; left -= 15)
    {
        Sleep(15000);
        Log("line probe: ... %d s left (game paused, bus line window open?)", left);
    }
    Sleep(15000);
    Snapshot();
    Log("line probe: baseline %zu pages", g_nCur);

    // still, still, TOGGLE, still, TOGGLE, still, TOGGLE, still. Byte level:
    // a byte that moves in any still step is out, its page stays in.
    const bool toggle[] = {false, false, true, false, true, false, true, false};
    const int  nSteps   = (int)(sizeof(toggle) / sizeof(toggle[0]));
    int clicks = 0;
    for (int s = 0; s < nSteps; ++s)
    {
        memcpy(g_prev, g_cur, g_nCur * sizeof(PageHash));
        g_nPrev = g_nCur;
        if (toggle[s]) Log("line probe: step %d/%d - CLICK THE SPACING SWITCH ONCE, NOW (you have 10 s)", s + 1, nSteps);
        else           Log("line probe: step %d/%d - do nothing for 10 s (game paused, mouse still)", s + 1, nSteps);
        Sleep(10000);
        Snapshot();

        if (toggle[s] && clicks == 0)
        {
            int n = Changed([](uintptr_t a) {
                if (g_nCand >= kMaxCand) return;
                Cand& c = g_cand[g_nCand];
                c.addr = a; c.alive = true; c.hits = 1;
                memset(c.bad, 0, 4096);
                if (ReadPage(a, c.last)) { memcpy(c.t[0], c.last, 4096); ++g_nCand; }
            });
            clicks = 1;
            Log("line probe: step %d: %d pages changed, %d kept", s + 1, n, g_nCand);
            continue;
        }
        if (g_nCand == 0) continue;
        int alive = 0;
        for (int k = 0; k < g_nCand; ++k)
        {
            Cand& c = g_cand[k];
            if (!c.alive) continue;
            BYTE now[4096];
            if (!ReadPage(c.addr, now)) { c.alive = false; continue; }
            if (!toggle[s])
                for (int i = 0; i < 4096; ++i) { if (now[i] != c.last[i]) c.bad[i] = 1; }
            else if (c.hits < 3)
                memcpy(c.t[c.hits++], now, 4096);
            memcpy(c.last, now, 4096);
            ++alive;
        }
        if (toggle[s]) ++clicks;
        Log("line probe: step %d done (%d pages watched)", s + 1, alive);
    }

    // Report: bytes never moving while still, with state1 == state3 != state2.
    struct Flip { BYTE* at; BYTE a, b; };
    static Flip flips[512];
    int nFlips = 0, shown = 0, total = 0;
    for (int k = 0; k < g_nCand; ++k)
    {
        Cand& c = g_cand[k];
        if (!c.alive || c.hits < 3) continue;
        for (int i = 0; i < 4096; ++i)
        {
            if (c.bad[i]) continue;
            BYTE a = c.t[0][i], b = c.t[1][i], d = c.t[2][i];
            if (a != d || a == b) continue;
            ++total;
            bool flag = (a <= 3 && b <= 3);
            if (flag && nFlips < 512) flips[nFlips++] = Flip{(BYTE*)(c.addr + i), a, b};
            if (shown < 60 && flag)
            {
                ++shown;
                Log("line probe: FLAG %p: %02X / %02X / %02X after clicks 1/2/3", (void*)(c.addr + i), a, b, d);
            }
        }
    }
    Log("line probe: %d alternating bytes, %d of them small flags (0..3)", total, nFlips);
    if (!nFlips) { Log("line probe: no 0/1 flag - done. You can unpause."); return 0; }

    // Persistent (the line) or the window's? Close the window, flip the
    // flags ourselves, reopen: if the box shows the other state, it is the line.
    Log("line probe: now CLOSE the bus line's window (stay paused). In 20 s the probe checks which flags survive.");
    Sleep(20000);
    // what survives the window closing belongs to the line, not to the window
    // Game 1.2 vehicle layout: type at +0x1748, category at type+0x2B8,
    // transport class at type+0x8650 (see game_offsets.h).
    auto vehicleAt = [](uintptr_t base, int* cat, int* cls) -> bool {
        uintptr_t type = 0;
        SIZE_T got = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), (void*)(base + 0x1748), &type, 8, &got)) return false;
        if (type < 0x10000 || (type & 7)) return false;
        if (!ReadProcessMemory(GetCurrentProcess(), (void*)(type + 0x2B8), cat, 4, &got)) return false;
        if (!ReadProcessMemory(GetCurrentProcess(), (void*)(type + 0x8650), cls, 4, &got)) return false;
        return *cat >= 1 && *cat <= 12 && *cls >= 0 && *cls <= 40;
    };
    int kept = 0;
    for (int k = 0; k < nFlips; ++k)
    {
        BYTE now = 0;
        SIZE_T got = 0;
        bool same = ReadProcessMemory(GetCurrentProcess(), flips[k].at, &now, 1, &got) && now == flips[k].a;
        if (!same) continue;
        ++kept;
        // inside a vehicle? try every 8-aligned start up to 0x2000 back
        char where[160] = "";
        int  found = 0, len = 0;
        for (uintptr_t off = 0; off <= 0x2000 && found < 3; off += 4)
        {
            uintptr_t base = (uintptr_t)flips[k].at - off;
            if (base & 7) continue;
            int cat = 0, cls = 0;
            if (!vehicleAt(base, &cat, &cls)) continue;
            ++found;
            len += snprintf(where + len, sizeof(where) - len, " vehicle+0x%X (cat %d, class %d)", (unsigned)off, cat, cls);
        }
        Log("line probe: KEPT %p  %02X/%02X/%02X%s", (void*)flips[k].at, flips[k].a, flips[k].b, flips[k].a,
            found ? where : "  (not inside a vehicle)");
    }
    Log("line probe: %d flag(s) survived the window closing.", kept);

    // The spacing switch is vehicle+0x658. Vehicles sit in a pool (the buses
    // were 0x1C40 apart): census the allocation(s) holding the line's buses,
    // grouped by vehicle type. No writes.
    uintptr_t pools[8];
    int nPools = 0;
    for (int k = 0; k < nFlips; ++k)
    {
        uintptr_t base = (uintptr_t)flips[k].at - 0x658;
        int cat = 0, cls = 0;
        if (!vehicleAt(base, &cat, &cls) || cls != 7) continue;
        MEMORY_BASIC_INFORMATION m;
        if (VirtualQuery((void*)base, &m, sizeof(m)) != sizeof(m)) continue;
        uintptr_t ab = (uintptr_t)m.AllocationBase;
        bool seen = false;
        for (int i = 0; i < nPools; ++i) if (pools[i] == ab) seen = true;
        if (!seen && nPools < 8) pools[nPools++] = ab;
    }
    if (!nPools) { Log("line probe: no bus vehicle with the flag at +0x658 - done, you can unpause."); return 0; }

    struct Group { uintptr_t type; int cat, cls, n, f0, f1, other; uintptr_t sample; char name[40]; };
    static Group g[96];
    int ng = 0, nVeh = 0;
    // type pointer verdict cache: 0 unknown, 1 good, 2 bad
    static uintptr_t cacheKey[8192];
    static BYTE      cacheVal[8192];
    memset(cacheKey, 0, sizeof(cacheKey));
    for (int pi = 0; pi < nPools; ++pi)
    {
        // walk every region of this allocation
        MEMORY_BASIC_INFORMATION m;
        for (uintptr_t r = pools[pi]; VirtualQuery((void*)r, &m, sizeof(m)) == sizeof(m) &&
                                      (uintptr_t)m.AllocationBase == pools[pi];
             r = (uintptr_t)m.BaseAddress + m.RegionSize)
        {
            if (m.State != MEM_COMMIT) continue;
            uintptr_t rb = (uintptr_t)m.BaseAddress;
            Log("line probe: vehicle pool region %p, %zu KiB", (void*)rb, (size_t)(m.RegionSize >> 10));
            for (SIZE_T off = 0; off < m.RegionSize; off += 1 << 20)
            {
                SIZE_T want = m.RegionSize - off < (1u << 20) ? m.RegionSize - off : (1u << 20);
                SIZE_T got = 0;
                if (!ReadProcessMemory(GetCurrentProcess(), (void*)(rb + off), g_chunk, want, &got)) continue;
                for (SIZE_T i = 0; i + 0x1750 <= got; i += 8)
                {
                    uintptr_t t;
                    memcpy(&t, g_chunk + i + 0x1748, 8);
                    if (t < 0x10000 || (t & 7) || (t >> 47)) continue;
                    size_t h = (t >> 3) & 8191;
                    if (cacheKey[h] != t)
                    {
                        int cat = 0, cls = 0;
                        cacheKey[h] = t;
                        cacheVal[h] = vehicleAt(rb + off + i, &cat, &cls) ? 1 : 2;
                    }
                    if (cacheVal[h] != 1) continue;
                    int cat = 0, cls = 0;
                    if (!vehicleAt(rb + off + i, &cat, &cls)) continue;
                    BYTE f = g_chunk[i + 0x658];
                    ++nVeh;
                    int j = 0;
                    while (j < ng && g[j].type != t) ++j;
                    if (j == ng && ng < 96)
                    {
                        g[ng] = Group{t, cat, cls, 0, 0, 0, 0, rb + off + i, ""};
                        // a readable name inside the type, if any
                        BYTE tb[0x300];
                        SIZE_T g2 = 0;
                        if (ReadProcessMemory(GetCurrentProcess(), (void*)t, tb, sizeof(tb), &g2))
                            for (int x = 0; x + 6 < (int)sizeof(tb); ++x)
                            {
                                int y = x;
                                while (y < (int)sizeof(tb) && tb[y] >= 32 && tb[y] < 127) ++y;
                                if (y - x >= 5 && y < (int)sizeof(tb) && tb[y] == 0)
                                {
                                    int L = y - x < 39 ? y - x : 39;
                                    memcpy(g[ng].name, tb + x, L);
                                    g[ng].name[L] = 0;
                                    break;
                                }
                                x = y;
                            }
                        ++ng;
                    }
                    if (j < ng)
                    {
                        ++g[j].n;
                        if (f == 0) ++g[j].f0; else if (f == 1) ++g[j].f1; else ++g[j].other;
                    }
                    i += 0x1C38;   // objects do not overlap: jump to the next slot
                }
            }
        }
    }
    Log("line probe: %d vehicles in %d pool(s), %d types:", nVeh, nPools, ng);
    for (int j = 0; j < ng; ++j)
        Log("line probe:   type %p \"%s\": cat %d, class %d, %d vehicles (spacing 0: %d, 1: %d, other: %d), e.g. %p",
            (void*)g[j].type, g[j].name, g[j].cat, g[j].cls, g[j].n, g[j].f0, g[j].f1, g[j].other, (void*)g[j].sample);
    Log("line probe: nothing was written. Done - you can unpause.");
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
