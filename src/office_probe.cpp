#include "office_probe.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "logger.h"

namespace wrsr::office_probe {
namespace {

// Computed by the script at run time (731.25 * 3, + 1957.5, + 100), so they
// are only in its variables, not in its constant table.
const float  kA = 2193.75f, kB = 4151.25f, kC = 4251.25f;
const double kAd = 2193.75, kBd = 4151.25;

bool ReadMem(const void* at, void* out, size_t n)
{
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), at, out, n, &got) && got == n;
}

// Finds marker A followed by marker B within 64 bytes, as float or double.
// Returns the address of A; *stride = distance A->B, *isDouble set.
const BYTE* Find(int* stride, bool* isDouble)
{
    const SIZE_T kChunk = 1 << 20;
    static BYTE* chunk = (BYTE*)VirtualAlloc(nullptr, kChunk, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!chunk) return nullptr;
    MEMORY_BASIC_INFORMATION mi;
    for (BYTE* p = nullptr; VirtualQuery(p, &mi, sizeof(mi)) == sizeof(mi); p = (BYTE*)mi.BaseAddress + mi.RegionSize)
    {
        if (mi.State != MEM_COMMIT || mi.Type != MEM_PRIVATE || mi.Protect != PAGE_READWRITE) continue;
        BYTE* base = (BYTE*)mi.BaseAddress;
        for (SIZE_T off = 0; off < mi.RegionSize; off += kChunk)
        {
            SIZE_T want = mi.RegionSize - off < kChunk ? mi.RegionSize - off : kChunk;
            SIZE_T got = 0;
            if (base + off == chunk || !ReadProcessMemory(GetCurrentProcess(), base + off, chunk, want, &got)) continue;
            for (SIZE_T i = 0; i + 520 <= got; i += 4)
            {
                float f;
                memcpy(&f, chunk + i, 4);
                if (f == kA)
                    for (int d = 4; d <= 512; d += 4)
                    {
                        float g;
                        memcpy(&g, chunk + i + d, 4);
                        if (g == kB) { *stride = d; *isDouble = false; return base + off + i; }
                    }
                if ((i & 7) == 0)
                {
                    double x;
                    memcpy(&x, chunk + i, 8);
                    if (x == kAd)
                        for (int d = 8; d <= 512; d += 8)
                        {
                            double y;
                            memcpy(&y, chunk + i + d, 8);
                            if (y == kBd) { *stride = d; *isDouble = true; return base + off + i; }
                        }
                }
            }
        }
    }
    return nullptr;
}

DWORD WINAPI Thread(void*)
{
    char last[600] = "";
    const BYTE* at = nullptr;
    int  stride = 0;
    bool dbl = false;
    for (;;)
    {
        Sleep(10000);
        // Still there? (the script may have been stopped, the save reloaded)
        if (at)
        {
            float f = 0;
            double x = 0;
            bool ok = dbl ? (ReadMem(at, &x, 8) && x == kAd) : (ReadMem(at, &f, 4) && f == kA);
            if (!ok) { Log("office probe: markers gone"); at = nullptr; }
        }
        if (!at)
        {
            at = Find(&stride, &dbl);
            if (!at)
            {
                static int misses = 0;
                if (++misses % 6 == 1) Log("office probe: no markers yet (is the TEST offices script running?)");
                continue;
            }
            Log("office probe: markers at %p, %s, %d bytes per variable", (const void*)at, dbl ? "double" : "float", stride);
            BYTE raw[0x140];
            if (ReadMem(at - 0x20, raw, sizeof(raw)))
                for (int row = 0; row < (int)sizeof(raw); row += 32)
                {
                    char line[120];
                    int k = 0;
                    for (int j = 0; j < 32; ++j) k += snprintf(line + k, sizeof(line) - k, "%02X ", raw[row + j]);
                    Log("office probe   %c%03X %s", row < 0x20 ? '-' : '+', row < 0x20 ? 0x20 - row : row - 0x20, line);
                }
        }
        // Variables in declaration order: A B count x0 z0 w0 ... x3 z3 w3 C
        double v[16];
        bool ok = true;
        for (int k = 0; k < 16 && ok; ++k)
        {
            if (dbl) ok = ReadMem(at + k * stride, &v[k], 8);
            else { float f; ok = ReadMem(at + k * stride, &f, 4); v[k] = f; }
        }
        if (!ok) { at = nullptr; continue; }
        char now[600];
        snprintf(now, sizeof(now), "count %.0f | (%.1f, %.1f) w%.0f | (%.1f, %.1f) w%.0f | (%.1f, %.1f) w%.0f | (%.1f, %.1f) w%.0f | end marker %s",
                 v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9], v[10], v[11], v[12], v[13], v[14],
                 v[15] == kC ? "ok" : "MISSING");
        if (strcmp(now, last) != 0)
        {
            strcpy(last, now);
            Log("office probe: %s", now);
        }
    }
    return 0;
}

}  // namespace

void Start()
{
    HANDLE h = CreateThread(nullptr, 0, Thread, nullptr, 0, nullptr);
    if (h) CloseHandle(h);
    Log("office probe on: start the TEST offices script, build technical offices");
}

}  // namespace wrsr::office_probe
