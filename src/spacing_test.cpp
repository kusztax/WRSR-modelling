// spacing_test.cpp - TEST ONLY (spacing_test = 1), see spacing_test.h.
// Vehicles live in one fixed-stride array (0x1C40-byte slots; game 1.2.0.8).
// The first train seen at a signal gives a slot; the array is walked from it.
#include "spacing_test.h"

#include <windows.h>
#include <string.h>

#include "logger.h"

namespace wrsr::spacing_test {
namespace {

constexpr uintptr_t kSlot    = 0x1C40;   // vehicle slot size
constexpr uintptr_t kSpacing = 0x658;    // per-vehicle spacing switch
constexpr uintptr_t kType    = 0x1748;   // vehicle -> type
constexpr uintptr_t kCat     = 0x2B8;    // type -> category (4 = locomotive)
constexpr uintptr_t kClass   = 0x8650;   // type -> transport class (7 = passengers)

volatile uintptr_t g_anchor = 0;
volatile LONG      g_on = 0;

template <class T> bool Rd(uintptr_t a, T& v)
{
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), (void*)a, &v, sizeof(v), &got) && got == sizeof(v);
}

bool VehicleAt(uintptr_t v, uintptr_t* type, int* cat, int* cls)
{
    uintptr_t t = 0;
    if (!Rd(v + kType, t) || t < 0x10000 || (t & 7) || (t >> 47)) return false;
    if (!Rd(t + kCat, *cat) || !Rd(t + kClass, *cls)) return false;
    *type = t;
    return *cat >= 1 && *cat <= 12 && *cls >= 0 && *cls <= 40;
}

struct Target { uintptr_t v, type; };
Target g_t[128];
int    g_nt = 0;

DWORD WINAPI Thread(void*)
{
    Log("spacing test: waiting for any train to reach a signal (load the save and let the game run)");
    while (!g_anchor) Sleep(1000);
    const uintptr_t anchor = g_anchor;
    Log("spacing test: train vehicle %p seen. Spacing goes ON for trams/railcars in 20 s.", (void*)anchor);
    Sleep(20000);

    MEMORY_BASIC_INFORMATION m;
    if (VirtualQuery((void*)anchor, &m, sizeof(m)) != sizeof(m)) { Log("spacing test: pool query failed"); return 0; }
    const uintptr_t ab = (uintptr_t)m.AllocationBase;
    int nVeh = 0, nRoad = 0;
    for (uintptr_t r = ab; VirtualQuery((void*)r, &m, sizeof(m)) == sizeof(m) && (uintptr_t)m.AllocationBase == ab;
         r = (uintptr_t)m.BaseAddress + m.RegionSize)
    {
        if (m.State != MEM_COMMIT || !(m.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE))) continue;
        uintptr_t rb = (uintptr_t)m.BaseAddress, re = rb + m.RegionSize;
        uintptr_t first = anchor >= rb ? rb + (anchor - rb) % kSlot
                                       : rb + (kSlot - (rb - anchor) % kSlot) % kSlot;
        for (uintptr_t v = first; v + kSlot <= re; v += kSlot)
        {
            uintptr_t t = 0;
            int cat = 0, cls = 0;
            if (!VehicleAt(v, &t, &cat, &cls)) continue;
            ++nVeh;
            BYTE f = 0;
            if (!Rd(v + kSpacing, f)) continue;
            if (cat == 1 && cls == 7 && f == 1) ++nRoad;
            if (cat == 4 && cls == 7 && f <= 1 && g_nt < 128) g_t[g_nt++] = Target{v, t};
        }
    }
    Log("spacing test: %d vehicles in the array, %d buses with spacing on, %d trams/railcars", nVeh, nRoad, g_nt);
    if (nVeh < 20 || g_nt == 0)
    {
        Log("spacing test: this does not look like the vehicle array - nothing written, test stops");
        return 0;
    }
    // types, so the player can match them to the vehicle list (e.g. 10 trams, 2 railcars)
    for (int i = 0; i < g_nt; ++i)
    {
        bool seen = false;
        for (int j = 0; j < i; ++j) if (g_t[j].type == g_t[i].type) seen = true;
        if (seen) continue;
        int n = 0;
        for (int j = 0; j < g_nt; ++j) if (g_t[j].type == g_t[i].type) ++n;
        Log("spacing test:   type %p: %d vehicle(s)", (void*)g_t[i].type, n);
    }
    int wrote = 0;
    for (int i = 0; i < g_nt; ++i)
    {
        BYTE one = 1;
        SIZE_T put = 0;
        if (WriteProcessMemory(GetCurrentProcess(), (void*)(g_t[i].v + kSpacing), &one, 1, &put) && put == 1) ++wrote;
    }
    Log("spacing test: spacing switched ON for %d tram(s)/railcar(s). Watch the 3-tram line for 10-15 minutes.", wrote);
    Log("spacing test: DO NOT SAVE the game during this test.");

    // Does the game keep it, or reset it from the line every tick?
    int resets = 0;
    for (int tick = 1; tick <= 180; ++tick)   // 15 minutes, every 5 s
    {
        Sleep(5000);
        int on = 0, off = 0, gone = 0;
        for (int i = 0; i < g_nt; ++i)
        {
            uintptr_t t = 0;
            int cat = 0, cls = 0;
            BYTE f = 0;
            if (!g_t[i].v || !VehicleAt(g_t[i].v, &t, &cat, &cls) || t != g_t[i].type) { g_t[i].v = 0; ++gone; continue; }
            if (!Rd(g_t[i].v + kSpacing, f)) continue;
            if (f == 1) { ++on; continue; }
            ++off;
            BYTE one = 1;
            SIZE_T put = 0;
            if (f == 0) WriteProcessMemory(GetCurrentProcess(), (void*)(g_t[i].v + kSpacing), &one, 1, &put);
        }
        resets += off;
        if (off || tick % 12 == 0)
            Log("spacing test: %d min %d s - on %d, reset by the game %d (set again), gone %d, resets so far %d",
                tick * 5 / 60, tick * 5 % 60, on, off, gone, resets);
    }
    Log("spacing test: done (switches stay as they are until the save is reloaded)");
    return 0;
}

}  // namespace

void NoteVehicle(const void* veh)
{
    if (g_on && !g_anchor && veh) g_anchor = (uintptr_t)veh;
}

void Start()
{
    if (InterlockedExchange(&g_on, 1)) return;
    HANDLE h = CreateThread(nullptr, 0, Thread, nullptr, 0, nullptr);
    if (h) CloseHandle(h);
    Log("spacing test on");
}

}  // namespace wrsr::spacing_test
