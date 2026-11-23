#include "office_probe.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "logger.h"
#include "script_switch.h"

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


// Through the script object: world+0x13BB8 holds the running scripts; the one
// whose file name contains "TestOffices" is ours. Each pointer field of it (and
// one level deeper) is checked for the marker within 64 KiB: logs the path.
bool PathProbe()
{
    BYTE* world = (BYTE*)script_switch::World();
    if (!world) { Log("office probe: no world"); return false; }
    BYTE* b = nullptr; BYTE* e = nullptr;
    if (!ReadMem(world + 0x13BB8, &b, 8) || !ReadMem(world + 0x13BC0, &e, 8) || !b || e <= b) return false;
    size_t n = (size_t)(e - b) / 8;
    if (n > 4096) return false;
    for (size_t k = 0; k < n; ++k)
    {
        BYTE* obj = nullptr;
        if (!ReadMem(b + 8 * k, &obj, 8) || !obj) continue;
        wchar_t* fileP = nullptr;
        wchar_t name[128] = L"";
        if (!ReadMem(obj + 0xB8, &fileP, 8) || !fileP || !ReadMem(fileP, name, sizeof(name) - 2)) continue;
        name[127] = 0;
        if (!wcsstr(name, L"TestOffices")) continue;
        Log("office probe: script object %p (%ls)", (void*)obj, name);
        static BYTE big[0x10000];
        int found = 0;
        for (int f = 0; f < 0x400 && found < 8; f += 8)
        {
            BYTE* p1 = nullptr;
            if (!ReadMem(obj + f, &p1, 8) || (uintptr_t)p1 < 0x10000 || ((uintptr_t)p1 & 7)) continue;
            BYTE* level[2] = {p1, nullptr};
            for (int depth = 0; depth < 2 && found < 8; ++depth)
            {
                BYTE* base = level[depth];
                if (!base) break;
                SIZE_T got = 0;
                ReadProcessMemory(GetCurrentProcess(), base, big, sizeof(big), &got);
                if (got < 16)
                {
                    // partially readable: try 4 KiB
                    if (!ReadMem(base, big, 0x1000)) break;
                    got = 0x1000;
                }
                for (SIZE_T i = 0; i + 8 <= got && found < 8; i += 4)
                {
                    float fv; memcpy(&fv, big + i, 4);
                    double dv; memcpy(&dv, big + i, 8);
                    if (fv == kA || ((i & 7) == 0 && dv == kAd))
                    {
                        ++found;
                        Log("office probe: marker A (%s) at obj+0x%X -> %s+0x%zX = %p", fv == kA ? "float" : "double", f,
                            depth ? "*(*(obj+f))" : "*(obj+f)", (size_t)i, (void*)(base + i));
                        char line[120];
                        for (int row = 0; row < 0x80 && i + row + 32 <= got; row += 32)
                        {
                            int c = 0;
                            for (int j = 0; j < 32; ++j) c += snprintf(line + c, sizeof(line) - c, "%02X ", big[i + row + j]);
                            Log("office probe     +%03X %s", row, line);
                        }
                    }
                }
                // one level deeper: the first pointer inside this block
                if (depth == 0) { BYTE* p2 = nullptr; memcpy(&p2, big, 8); if ((uintptr_t)p2 > 0x10000 && !((uintptr_t)p2 & 7)) level[1] = p2; }
            }
        }
        if (!found)
        {
            Log("office probe: marker not behind any pointer of the script object; its first 0x200 bytes:");
            BYTE raw[0x200];
            if (ReadMem(obj, raw, sizeof(raw)))
                for (int row = 0; row < 0x200; row += 32)
                {
                    char line[120];
                    int c = 0;
                    for (int j = 0; j < 32; ++j) c += snprintf(line + c, sizeof(line) - c, "%02X ", raw[row + j]);
                    Log("office probe   obj+%03X %s", row, line);
                }
        }
        return true;
    }
    Log("office probe: TestOffices script not among the %zu running scripts", n);
    return false;
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
                if (++misses % 6 == 1)
                {
                    Log("office probe: no markers in a plain scan - checking the script object");
                    PathProbe();
                }
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
