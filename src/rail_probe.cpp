#include "rail_probe.h"

#include <stdio.h>
#include <string.h>

#include "aob_scan.h"
#include "game_offsets.h"
#include "hooks.h"
#include "logger.h"
#include "tesmio_api.h"

namespace wrsr::rail_probe {
namespace {

using namespace wrsr::off;

const TsmHost* g_host = nullptr;

// ---------------------------------------------------------------- patterns
// From RailPhysics (GPL-3.0), verified there on 1.1.1.9; re-verified here at
// runtime: unique match, and the two independent ways to the divisor
// function must agree.

// divisor fn prologue + body: push rbx; sub rsp,30h; movaps [rsp+20h],xmm6;
// mov rbx,rcx; call power; mov dl,1; mov rcx,rbx; movaps xmm6,xmm0; call mass;
// mov rax,[rbx+1708h]
const char kSigDivisorFn[] =
    "40 53 48 83 EC 30 0F 29 74 24 20 48 8B D9 E8 ?? ?? ?? ?? B2 01 48 8B CB "
    "0F 28 F0 E8 ?? ?? ?? ?? 48 8B 83 08 17 00 00";

// in the rail update: mov rcx,rsi; call divisor; movaps xmm15,xmm0; ...
const char kSigCallDivisor[] =
    "48 8B CE E8 ?? ?? ?? ?? 44 0F 28 F8 45 0F 28 CE 45 0F 28 EC";

// mov rax,[rip+&chainVec]; mov rcx,[rax+rcx*8]; add rcx,320h - several sites,
// all of which must resolve to the same global.
const char kSigChainVec[] = "48 8B 05 ?? ?? ?? ?? 48 8B 0C C8 48 81 C1 20 03 00 00";

// What installInlineHook must find at the divisor entry: the first 14 bytes,
// ending on an instruction boundary, nothing rip-relative.
const unsigned char kDivisorExpect[14] = {
    0x40, 0x53,                         // push rbx
    0x48, 0x83, 0xEC, 0x30,             // sub  rsp,30h
    0x0F, 0x29, 0x74, 0x24, 0x20,       // movaps [rsp+20h],xmm6
    0x48, 0x8B, 0xD9};                  // mov  rbx,rcx

// ---------------------------------------------------------------- memory

bool Readable(const void* p, size_t n)
{
    if (!p) return false;
    if (g_host && g_host->readablePtr) return g_host->readablePtr(p, n) != 0;
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) return false;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    return (const BYTE*)p + n <= (const BYTE*)mbi.BaseAddress + mbi.RegionSize;
}

template <class T>
bool Read(const BYTE* base, uintptr_t off, T& out)
{
    if (!Readable(base + off, sizeof(T))) return false;
    memcpy(&out, base + off, sizeof(T));
    return true;
}

int ChainType(BYTE* chain)
{
    BYTE* desc = nullptr;
    int   id   = -1;
    if (!chain || !Read(chain, C_TYPE_DESC, desc) || !Read(desc, D_TYPE_ID, id)) return -1;
    return id;
}

// ---------------------------------------------------------------- state

BYTE*** g_chainVec = nullptr;   // &{begin,end} of chain*

using DivisorFn = float (*)(void*);
DivisorFn g_origDivisor = nullptr;

SRWLOCK   g_lock       = SRWLOCK_INIT;
int       g_linesLeft  = 0;
bool      g_capLogged  = false;
ULONGLONG g_lastChains = 0;

struct Seen
{
    BYTE* veh;
    BYTE* type;
    BYTE* chainNext;
    BYTE* chainAt;
    int   id;
};
constexpr int kMaxSeen = 1024;
Seen g_seen[kMaxSeen];
int  g_seenCount = 0;
bool g_fullLogged = false;

bool TakeLine()
{
    if (g_linesLeft > 0) { --g_linesLeft; return true; }
    if (!g_capLogged)
    {
        g_capLogged = true;
        Log("rail  census log limit reached - silent from now on (raise census_log_limit)");
    }
    return false;
}

// ---------------------------------------------------------------- census

struct Consist
{
    int  units = 0, locos = 0, wagons = 0, service = 0;
    int  paxUnits = 0, reversed = 0, inactive = 0, unreadable = 0;
    bool ok = false;
};

void ScanUnit(BYTE* unit, Consist& c)
{
    BYTE* type = nullptr;
    int   cat = -1, cls = -1, inactive = 0;
    unsigned char rev = 0;
    if (!Read(unit, V_TYPE, type) || !Read(type, T_CATEGORY, cat) || !Read(type, T_TRANSPORT, cls))
    {
        ++c.unreadable;
        return;
    }
    Read(unit, V_INACTIVE, inactive);
    Read(unit, V_REVERSED, rev);
    ++c.units;
    if (cat == CAT_LOCO) ++c.locos;
    else if (cat == CAT_WAGON) ++c.wagons;
    else if (cat == CAT_SERVICE) ++c.service;
    if (cls == TRANSPORT_PASSENGERS) ++c.paxUnits;
    if (rev) ++c.reversed;
    if (inactive) ++c.inactive;
}

Consist ReadConsist(BYTE* lead)
{
    Consist c;
    ScanUnit(lead, c);
    BYTE** b = nullptr;
    BYTE** e = nullptr;
    if (!Read(lead, V_WAGON_VEC, b) || !Read(lead, V_WAGON_VEC + 8, e)) return c;
    if (e < b || e - b > 512 || (b != e && !Readable(b, (size_t)(e - b) * sizeof(BYTE*)))) return c;
    for (BYTE** p = b; p != e; ++p) ScanUnit(*p, c);
    c.ok = c.unreadable == 0;
    return c;
}

void LogNewTrain(const Seen& s)
{
    BYTE* v = s.veh;
    int   cat = -1, cls = -1;
    float top = -1, speed = -1, target = -1;
    BYTE* leader = nullptr;
    Read(s.type, T_CATEGORY, cat);
    Read(s.type, T_TRANSPORT, cls);
    Read(s.type, T_TOPSPEED, top);
    Read(v, V_SPEED, speed);
    Read(v, V_TARGET, target);
    Read(v, V_LEADER, leader);

    Consist c = ReadConsist(v);

    Log("rail  train #%d %p NEW  lead: cat %d cls %d top %.0f, leader %p | v %.1f -> %.1f km/h",
        s.id, (void*)v, cat, cls, top, (void*)leader, speed, target);
    Log("rail  train #%d consist%s: %d units = %d loco + %d wagon + %d service; "
        "pax-units %d, reversed %d, inactive %d => %s",
        s.id, c.ok ? "" : " (PARTIAL)", c.units, c.locos, c.wagons, c.service,
        c.paxUnits, c.reversed, c.inactive, c.paxUnits > 0 ? "PASSENGER" : "freight/other");
    Log("rail  train #%d chains: next %p (type %d), at %p (type %d)", s.id,
        (void*)s.chainNext, ChainType(s.chainNext), (void*)s.chainAt, ChainType(s.chainAt));
}

void ChainCensus()
{
    if (!g_chainVec) return;
    BYTE** b = nullptr;
    BYTE** e = nullptr;
    if (!Read((BYTE*)g_chainVec, 0, b) || !Read((BYTE*)g_chainVec, 8, e)) return;
    long n = (e >= b) ? (long)(e - b) : -1;
    if (n <= 0 || n > 200000 || !Readable(b, (size_t)n * sizeof(BYTE*)))
    {
        if (TakeLine()) Log("rail  chains: vector empty or implausible (n=%ld)", n);
        return;
    }

    struct Bin { int type; int count; int occupied; };
    Bin bins[32];
    int nb = 0, other = 0;
    for (long i = 0; i < n; ++i)
    {
        BYTE* ch = b[i];
        int   t  = ChainType(ch);
        BYTE* vb = nullptr;
        BYTE* ve = nullptr;
        bool  occ = Read(ch, C_VEHICLES, vb) && Read(ch, C_VEHICLES + 8, ve) && ve > vb;
        int k = 0;
        while (k < nb && bins[k].type != t) ++k;
        if (k == nb)
        {
            if (nb == 32) { ++other; continue; }
            bins[nb++] = Bin{t, 0, 0};
        }
        ++bins[k].count;
        if (occ) ++bins[k].occupied;
    }

    char line[900];
    int  len = snprintf(line, sizeof(line), "rail  chains: %ld total; type x count (occupied):", n);
    for (int k = 0; k < nb && len < (int)sizeof(line) - 32; ++k)
        len += snprintf(line + len, sizeof(line) - len, " %d x%d(%d)", bins[k].type, bins[k].count,
                        bins[k].occupied);
    if (other) snprintf(line + len, sizeof(line) - len, " +%d more types", other);
    if (TakeLine()) Log("%s", line);
}

void Observe(BYTE* v)
{
    BYTE* type = nullptr;
    BYTE* next = nullptr;
    BYTE* at   = nullptr;
    // `v` comes straight from the game, so its own fields are readable; the
    // checks are for a game update that moved them.
    if (!Read(v, V_TYPE, type) || !Read(v, V_CHAIN_NEXT, next) || !Read(v, V_CHAIN_AT, at)) return;

    AcquireSRWLockExclusive(&g_lock);

    ULONGLONG now = GetTickCount64();
    if (now - g_lastChains >= 60000)
    {
        g_lastChains = now;
        ChainCensus();
    }

    int i = 0;
    while (i < g_seenCount && !(g_seen[i].veh == v && g_seen[i].type == type)) ++i;

    if (i == g_seenCount)
    {
        if (g_seenCount == kMaxSeen)
        {
            if (!g_fullLogged && TakeLine()) Log("rail  census table full (%d trains)", kMaxSeen);
            g_fullLogged = true;
        }
        else
        {
            Seen& s = g_seen[g_seenCount];
            s = Seen{v, type, next, at, g_seenCount + 1};
            ++g_seenCount;
            if (g_linesLeft >= 3) { g_linesLeft -= 3; LogNewTrain(s); }
            else TakeLine();
        }
    }
    else
    {
        Seen& s = g_seen[i];
        if (s.chainNext != next || s.chainAt != at)
        {
            if (TakeLine())
            {
                float speed = -1;
                Read(v, V_SPEED, speed);
                Log("rail  train #%d next %p(t%d) -> %p(t%d), at %p(t%d) -> %p(t%d), v %.1f", s.id,
                    (void*)s.chainNext, ChainType(s.chainNext), (void*)next, ChainType(next),
                    (void*)s.chainAt, ChainType(s.chainAt), (void*)at, ChainType(at), speed);
            }
            s.chainNext = next;
            s.chainAt   = at;
        }
    }

    ReleaseSRWLockExclusive(&g_lock);
}

float DivisorDetour(void* veh)
{
    float r = g_origDivisor(veh);   // the game's own result, untouched
    if (veh) Observe((BYTE*)veh);
    return r;
}

// ---------------------------------------------------------------- locate

BYTE*** LocateChainVector(HMODULE exe)
{
    std::vector<BYTE*> hits = aob::ScanAll(exe, ".text", kSigChainVec, 64);
    BYTE* target = nullptr;
    for (BYTE* p : hits)
    {
        BYTE* t = aob::RipTarget(p + 3, p + 7);
        if (!target) target = t;
        else if (t != target)
        {
            Log("rail  chain vector: sites disagree (%p vs %p) - off", (void*)target, (void*)t);
            return nullptr;
        }
    }
    if (!target)
    {
        Log("rail  chain vector: pattern not found - off");
        return nullptr;
    }
    Log("rail  chain vector at +0x%llX (%zu sites agree)",
        (unsigned long long)(target - (BYTE*)exe), hits.size());
    return (BYTE***)target;
}

BYTE* LocateDivisor(HMODULE exe)
{
    aob::Hit fn   = aob::ScanModule(exe, ".text", kSigDivisorFn, "divisor fn");
    aob::Hit call = aob::ScanModule(exe, ".text", kSigCallDivisor, "divisor call site");
    if (fn.result != aob::Result::Unique || call.result != aob::Result::Unique) return nullptr;

    BYTE* viaCall = aob::RipTarget(call.addr + 4, call.addr + 8);   // E8 rel32
    if (viaCall != fn.addr)
    {
        Log("rail  divisor: call site targets +0x%llX, signature says +0x%llX - off",
            (unsigned long long)(viaCall - (BYTE*)exe), (unsigned long long)(fn.addr - (BYTE*)exe));
        return nullptr;
    }
    Log("rail  divisor fn confirmed by its call site");
    return fn.addr;
}

}  // namespace

bool Start(const TsmHost* host, HMODULE exe, const Config& cfg)
{
    g_host      = host;
    g_linesLeft = cfg.logLimit > 0 ? cfg.logLimit : 0;

    g_chainVec = LocateChainVector(exe);
    if (g_chainVec) ChainCensus();   // at the main menu this is normally empty

    if (!cfg.trainCensus)
    {
        Log("rail  train census off (train_census = 0) - nothing hooked");
        return false;
    }

    BYTE* divisor = LocateDivisor(exe);
    if (!divisor) return false;

    bool ok = hooks::InstallHostHook("rail divisor (census)", divisor, (void*)&DivisorDetour,
                                     (void**)&g_origDivisor, kDivisorExpect, sizeof(kDivisorExpect));
    if (ok) Log("rail  train census live: load a save with trains and let them run");
    return ok;
}

}  // namespace wrsr::rail_probe
