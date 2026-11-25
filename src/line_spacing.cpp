// line_spacing.cpp - see line_spacing.h.
#include "line_spacing.h"

#include <stdint.h>
#include <string.h>

#include "aob_scan.h"
#include "hooks.h"
#include "logger.h"
#include "tesmio_api.h"

namespace wrsr::line_spacing {
namespace {

// Game 1.2.0.8 layout (docs/10-line-spacing-findings.md).
constexpr uintptr_t kLineKind     = 0x4;     // line -> 1 = road
constexpr uintptr_t kLineVehBegin = 0x240;   // line -> vector<vehicle*>
constexpr uintptr_t kVehType      = 0x1748;  // vehicle -> type
constexpr uintptr_t kTypeCat      = 0x2B8;   // type -> category (4 = locomotive)
constexpr uintptr_t kTypeClass    = 0x8650;  // type -> transport class (7 = passengers)

// `bool lineMaySpace(line)`: the gate (per tick for every line, and in the
// line window). Only road lines pass.
const char kSigGate[] =
    "40 53 48 83 EC 20 48 8B 81 48 02 00 00 45 32 C9 48 2B 81 40 02 00 00 48 8B D9 48 C1 F8 03 48 85";
const unsigned char kGateExpect[16] = {
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20,        // push rbx; sub rsp,20h
    0x48, 0x8B, 0x81, 0x48, 0x02, 0x00, 0x00,  // mov rax,[rcx+248h]
    0x45, 0x32, 0xC9};                         // xor r9b,r9b

// The line window's spacing block: `cmp dword [rsi+4],1; jne <skip>` (rsi =
// line), right before it asks the gate.
const char kSigWindow[] =
    "F3 0F 10 35 ?? ?? ?? ?? 83 7E 04 01 0F 85 ?? ?? ?? ?? 0F 28 FE F3 0F 59 3D ?? ?? ?? ?? "
    "F3 41 0F 58 F8 F3 41 0F 59 F4 F3 41 0F 58 F2 48 8B CE E8";
constexpr size_t kWindowSiteOff = 8;   // the cmp
const unsigned char kWindowSite[6] = {0x83, 0x7E, 0x04, 0x01, 0x0F, 0x85};

typedef char (*GateFn)(void* line);
GateFn g_origGate = nullptr;

// Every vehicle on the line carries its own passengers on rails.
bool RailPassengerLine(const BYTE* line)
{
    BYTE* const* b = *(BYTE* const* const*)(line + kLineVehBegin);
    BYTE* const* e = *(BYTE* const* const*)(line + kLineVehBegin + 8);
    if (!b || e <= b || e - b > 256) return false;
    for (BYTE* const* p = b; p != e; ++p)
    {
        const BYTE* v = *p;
        if (!v) return false;
        const BYTE* t = *(BYTE* const*)(v + kVehType);
        if (!t || *(const int*)(t + kTypeCat) != 4 || *(const int*)(t + kTypeClass) != 7) return false;
    }
    return true;
}

char GateDetour(void* line)
{
    char r = g_origGate(line);
    if (r || !line) return r;
    return RailPassengerLine((const BYTE*)line) ? 1 : 0;
}

// Called from the code cave in place of `cmp dword [line+4],1`: show the
// spacing block for road lines (as before) and for tram/railcar lines.
extern "C" char ShowSpacingBlock(const BYTE* line)
{
    if (!line) return 0;
    return *(const int*)(line + kLineKind) == 1 || RailPassengerLine(line);
}

bool PatchWindow(const TsmHost* host, HMODULE exe)
{
    aob::Hit hit = aob::ScanModule(exe, ".text", kSigWindow, "line window spacing block");
    if (hit.result != aob::Result::Unique) return false;
    BYTE* site = hit.addr + kWindowSiteOff;
    if (memcmp(site, kWindowSite, sizeof(kWindowSite)) != 0) return false;
    int32_t rel;
    memcpy(&rel, site + 6, 4);
    BYTE* skip = site + 10 + rel;   // where the jne went

    if (!host || !host->allocNear) { Log("spacing no allocNear in this loader - line window unchanged"); return false; }
    BYTE* cave = host->allocNear(site, 64);
    if (!cave) return false;
    DWORD oldCave;
    if (!VirtualProtect(cave, 64, PAGE_EXECUTE_READWRITE, &oldCave)) return false;
    // sub rsp,28h; mov rcx,rsi; call [ShowSpacingBlock]; add rsp,28h;
    // test al,al; jz hide; ret;  hide: add rsp,8; jmp [skip]
    static const BYTE kCave[32] = {
        0x48, 0x83, 0xEC, 0x28,              //  0 sub  rsp,28h
        0x48, 0x89, 0xF1,                    //  4 mov  rcx,rsi
        0xFF, 0x15, 0x1B, 0x00, 0x00, 0x00,  //  7 call [rip+1Bh] -> +40
        0x48, 0x83, 0xC4, 0x28,              // 13 add  rsp,28h
        0x84, 0xC0,                          // 17 test al,al
        0x74, 0x01,                          // 19 jz   +1
        0xC3,                                // 21 ret  (show: back after the patch)
        0x48, 0x83, 0xC4, 0x08,              // 22 add  rsp,8 (drop the return address)
        0xFF, 0x25, 0x00, 0x00, 0x00, 0x00}; // 26 jmp  [rip+0] -> +32
    memcpy(cave, kCave, sizeof(kCave));
    void* fn = (void*)&ShowSpacingBlock;
    memcpy(cave + 32, &skip, 8);
    memcpy(cave + 40, &fn, 8);
    FlushInstructionCache(GetCurrentProcess(), cave, 64);

    int64_t d = (int64_t)(cave - (site + 5));
    if (d != (int32_t)d) return false;
    BYTE patch[10] = {0xE8, 0, 0, 0, 0, 0x0F, 0x1F, 0x44, 0x00, 0x00};   // call cave; nop5
    int32_t d32 = (int32_t)d;
    memcpy(patch + 1, &d32, 4);
    DWORD old;
    if (!VirtualProtect(site, sizeof(patch), PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy(site, patch, sizeof(patch));
    VirtualProtect(site, sizeof(patch), old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, sizeof(patch));
    return true;
}

}  // namespace

bool Start(const TsmHost* host, HMODULE exe)
{
    aob::Hit gate = aob::ScanModule(exe, ".text", kSigGate, "line spacing gate");
    if (gate.result != aob::Result::Unique ||
        !hooks::InstallHostHook("line spacing gate", gate.addr, (void*)&GateDetour, (void**)&g_origGate,
                                kGateExpect, sizeof(kGateExpect)))
    {
        Log("spacing gate not found (other game version?) - tram line spacing OFF, game unchanged");
        return false;
    }
    if (!PatchWindow(host, exe))
    {
        Log("spacing line window not recognised - spacing works for trams already switched on, but the switch is not shown");
        return true;
    }
    Log("spacing tram/railcar lines: the line window has the spacing switch");
    return true;
}

}  // namespace wrsr::line_spacing
