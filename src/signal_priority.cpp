#include "signal_priority.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aob_scan.h"
#include "game_offsets.h"
#include "hooks.h"
#include "logger.h"
#include "priority_policy.h"
#include "script_switch.h"
#include "tesmio_api.h"

namespace wrsr::signal_priority {
namespace {

using namespace wrsr::off;

// ---------------------------------------------------------------- pattern
// `char maySignalPass(lead)` (Rail Operations by pautov-ya, GPL-3.0;
// FUN_1406ac6e0 on 1.1.1.9, +0x6AC880 on 1.1.1.13): push rbx; sub rsp,70h;
// cmp byte [rcx+5E0h],0; mov rbx,rcx; je +8; mov al,1; add rsp,70h; pop rbx;
// ret; mov rax,[rcx+6A8h]
const char kSigSignalFn[] =
    "40 53 48 83 EC 70 80 B9 E0 05 00 00 00 48 8B D9 74 08 B0 01 48 83 C4 70 5B C3 48 8B 81 A8 06 00";
// The same function after another plugin hooked it through the loader (Rail
// Operations does): a 14-byte absolute jump `jmp [rip+0]; dq target` over the
// prologue, then the untouched body. We then chain in front of that jump.
const char kSigSignalHooked[] =
    "FF 25 00 00 00 00 ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? 74 08 B0 01 48 83 C4 70 5B C3 48 8B 81 A8 06 00";
const unsigned char kSignalExpect[16] = {
    0x40, 0x53,                                // push rbx
    0x48, 0x83, 0xEC, 0x70,                    // sub  rsp,70h
    0x80, 0xB9, 0xE0, 0x05, 0x00, 0x00, 0x00,  // cmp  byte [rcx+5E0h],0
    0x48, 0x8B, 0xD9};                         // mov  rbx,rcx

// Track graph (Rail Operations RE): a segment's end nodes at +0x20 / +0x28,
// a node's segments as {begin,end} at +0x28 / +0x30.
constexpr uintptr_t SEG_NODE_A    = 0x20;
constexpr uintptr_t SEG_NODE_B    = 0x28;
constexpr uintptr_t NODE_SEGS     = 0x28;
constexpr uintptr_t V_ROUTE_SEGS  = 0x6B8;   // {begin,end} of segment*, parallel to the nodes
constexpr int       kMaxBlockSegs = 128;

// ---------------------------------------------------------------- memory

const TsmHost* g_host = nullptr;

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
    if (!base || !Readable(base + off, sizeof(T))) return false;
    memcpy(&out, base + off, sizeof(T));
    return true;
}

bool SignalAtNode(BYTE* node)
{
    BYTE* sig  = nullptr;
    int   kind = SIGNAL_KIND_NONE;
    return node && Read(node, N_SIGNAL, sig) && sig && Read(sig, S_KIND, kind) && kind != SIGNAL_KIND_NONE;
}

// ---------------------------------------------------------------- the block

struct Ahead
{
    BYTE* node = nullptr;   // the node the train heads for (holds the signal)
    BYTE* seg  = nullptr;   // the segment past it
    BYTE* sig  = nullptr;
    int   kind = -1;
    unsigned char freeA = 0, freeB = 0, chain = 0;
};

Ahead ReadAhead(BYTE* lead)
{
    Ahead a;
    BYTE** nb = nullptr;
    BYTE** ne = nullptr;
    BYTE** sb = nullptr;
    BYTE** se = nullptr;
    int    idx = -1;
    if (!Read(lead, V_ROUTE_NODES, nb) || !Read(lead, V_ROUTE_NODES + 8, ne) ||
        !Read(lead, V_ROUTE_SEGS, sb) || !Read(lead, V_ROUTE_SEGS + 8, se) || !Read(lead, V_ROUTE_INDEX, idx))
        return a;
    if (!nb || ne <= nb || ne - nb > 100000 || idx < 0 || idx >= ne - nb) return a;
    Read((BYTE*)(nb + idx), 0, a.node);
    if (sb && se > sb && idx < se - sb) Read((BYTE*)(sb + idx), 0, a.seg);
    if (a.node && Read(a.node, N_SIGNAL, a.sig) && a.sig)
    {
        Read(a.sig, S_KIND, a.kind);
        Read(a.sig, S_FREE_A, a.freeA);
        Read(a.sig, S_FREE_B, a.freeB);
        Read(a.sig, S_CHAIN, a.chain);
    }
    return a;
}

struct BlockId
{
    uint64_t key   = 0;   // lowest segment address in the block: same set -> same key
    int      segs  = 0;
    bool     capped = false;
};

// Every segment reachable from `start` without passing a node that has a
// signal. Two signals on different branches in front of one junction see the
// same set, so the same key.
BlockId WalkBlock(BYTE* start)
{
    BlockId id;
    if (!start) return id;
    BYTE* block[kMaxBlockSegs];
    int   n = 0;
    block[n++] = start;
    for (int q = 0; q < n; ++q)
    {
        for (int end = 0; end < 2; ++end)
        {
            BYTE* node = nullptr;
            if (!Read(block[q], end ? SEG_NODE_B : SEG_NODE_A, node) || !node) continue;
            if (SignalAtNode(node)) continue;   // a block boundary
            BYTE** b = nullptr;
            BYTE** e = nullptr;
            if (!Read(node, NODE_SEGS, b) || !Read(node, NODE_SEGS + 8, e) || !b || e <= b || e - b > 64 ||
                !Readable(b, (size_t)(e - b) * sizeof(BYTE*)))
                continue;
            for (BYTE** p = b; p < e; ++p)
            {
                BYTE* s = *p;
                if (!s) continue;
                bool seen = false;
                for (int k = 0; k < n && !seen; ++k) seen = block[k] == s;
                if (seen) continue;
                if (n == kMaxBlockSegs) { id.capped = true; break; }
                block[n++] = s;
            }
        }
    }
    uint64_t lowest = UINT64_MAX;
    for (int k = 0; k < n; ++k)
        if ((uint64_t)(uintptr_t)block[k] < lowest) lowest = (uint64_t)(uintptr_t)block[k];
    id.key  = lowest;
    id.segs = n;
    return id;
}

// ---------------------------------------------------------------- trains
// Open-addressing table keyed by the lead vehicle's address. A vehicle that
// was sold and whose address got reused shows a different type: reset.

struct Train
{
    BYTE*     veh;
    BYTE*     type;
    int       id;
    bool      pax;
    bool      waiting;      // last EFFECTIVE answer was WAIT
    BYTE*     node;         // node of the signal it last asked about
    uint64_t  blockKey;
    int       blockSegs;
    ULONGLONG lastSeen;
    ULONGLONG waitSince;
    ULONGLONG wentAt;
    ULONGLONG holdStart;    // we are holding it since (0 = not)
    int       holdFor;      // id of the passenger train it yields to
};

constexpr int kSlots = 8192;   // power of two
Train g_train[kSlots];
int   g_trainCount = 0;
bool  g_fullLogged = false;

SRWLOCK g_lock      = SRWLOCK_INIT;
int     g_linesLeft = 0;
bool    g_capLogged = false;

Config            g_cfg;
priority::Config  g_pcfg;

bool TakeLine()
{
    if (g_linesLeft > 0) { --g_linesLeft; return true; }
    if (!g_capLogged)
    {
        g_capLogged = true;
        Log("prio  log limit reached - silent from now on (raise census_log_limit)");
    }
    return false;
}

bool IsPassengerTrain(BYTE* lead)
{
    auto unitIsPax = [](BYTE* unit) {
        BYTE* type = nullptr;
        int   cls  = -1;
        return Read(unit, V_TYPE, type) && Read(type, T_TRANSPORT, cls) && cls == TRANSPORT_PASSENGERS;
    };
    if (unitIsPax(lead)) return true;
    BYTE** b = nullptr;
    BYTE** e = nullptr;
    if (!Read(lead, V_WAGON_VEC, b) || !Read(lead, V_WAGON_VEC + 8, e)) return false;
    if (!b || e <= b || e - b > 512 || !Readable(b, (size_t)(e - b) * sizeof(BYTE*))) return false;
    for (BYTE** p = b; p != e; ++p)
        if (unitIsPax(*p)) return true;
    return false;
}

Train* Find(BYTE* veh)
{
    BYTE* type = nullptr;
    if (!Read(veh, V_TYPE, type)) return nullptr;

    uint64_t h = (uint64_t)(uintptr_t)veh;
    h ^= h >> 17; h *= 0xED5AD4BBull; h ^= h >> 11;
    for (int probe = 0; probe < kSlots; ++probe)
    {
        Train& t = g_train[(h + probe) & (kSlots - 1)];
        if (t.veh == veh)
        {
            if (t.type != type)   // address reused by another vehicle
            {
                t = Train{};
                t.veh = veh;
                t.type = type;
                t.id = ++g_trainCount;
                t.pax = IsPassengerTrain(veh);
            }
            return &t;
        }
        if (!t.veh)
        {
            if (g_trainCount >= kSlots / 2)   // keep the table sparse
            {
                if (!g_fullLogged && TakeLine()) Log("prio  train table full - new trains not tracked");
                g_fullLogged = true;
                return nullptr;
            }
            t = Train{};
            t.veh  = veh;
            t.type = type;
            t.id   = ++g_trainCount;
            t.pax  = IsPassengerTrain(veh);
            return &t;
        }
    }
    return nullptr;
}

const char* Kind(const Train& t) { return t.pax ? "PAX" : "FRT"; }

// ---------------------------------------------------------------- decision

using SignalFn = char (*)(void*);
SignalFn g_origSignal = nullptr;

char Decide(Train& t, char gameAnswer, const Ahead& a, ULONGLONG now)
{
    if (!g_cfg.priority || !gameAnswer || t.pax) return gameAnswer;
    // Cheap exits before scanning the table: only a waiting (or held)
    // freight train in front of a known block can be held.
    if ((!t.waiting && !t.holdStart) || !t.blockKey) return gameAnswer;

    // Switched off in the Scripts menu (per save): let a held train go.
    if (script_switch::PriorityOffByScript())
    {
        if (t.holdStart && TakeLine())
            Log("prio  GO     train #%d FRT released: priority switched off in the Scripts menu", t.id);
        t.holdStart = 0;
        return gameAnswer;
    }

    // Gather the passenger trains that might compete for this block.
    priority::PaxView view[64];
    int n = 0;
    for (int i = 0; i < kSlots && n < 64; ++i)
    {
        const Train& p = g_train[i];
        if (!p.veh || !p.pax || p.blockKey != t.blockKey || !p.blockKey) continue;
        view[n++] = priority::PaxView{p.blockKey, p.node, p.waiting, p.lastSeen, p.wentAt, p.id};
    }

    priority::Request req{false, t.waiting, t.blockKey, t.node, t.holdStart, now};
    int who = 0;
    priority::Verdict v = priority::Decide(req, view, n, g_pcfg, &who);

    switch (v)
    {
    case priority::Verdict::Hold:
        // The timeout guards against a deadlock, not against starvation: it
        // runs per passenger train. A new passenger train going first means
        // traffic is flowing, so the clock starts again - a freight train on
        // a busy passenger line can wait indefinitely, by design.
        if (!t.holdStart || who != t.holdFor)
        {
            t.holdStart = now;
            t.holdFor   = who;
            if (TakeLine())
                Log("prio  HOLD   train #%d FRT at signal %p (block %llx) - passenger train #%d goes first",
                    t.id, (void*)a.sig, (unsigned long long)t.blockKey, who);
        }
        return 0;

    case priority::Verdict::Timeout:
        if (TakeLine())
            Log("prio  TIMEOUT train #%d FRT released: passenger train #%d made no progress for %.0f s",
                t.id, who, (double)(now - t.holdStart) / 1000.0);
        t.holdStart = 0;
        return gameAnswer;

    case priority::Verdict::Go:
        if (t.holdStart && TakeLine())
            Log("prio  GO     train #%d FRT after being held %.1f s for passenger train #%d", t.id,
                (double)(now - t.holdStart) / 1000.0, t.holdFor);
        t.holdStart = 0;
        return gameAnswer;
    }
    return gameAnswer;
}

void Observe(BYTE* lead, char& answer)
{
    AcquireSRWLockExclusive(&g_lock);
    Train* t = Find(lead);
    if (t)
    {
        ULONGLONG now = GetTickCount64();
        Ahead a = ReadAhead(lead);

        // The block only matters while it is in front of the train; work it
        // out once per signal, not once per frame.
        if (a.node != t->node)
        {
            t->node = a.node;
            BlockId b = WalkBlock(a.seg);
            t->blockKey  = b.key;
            t->blockSegs = b.segs;
        }

        bool wasWaiting = t->waiting;
        answer = Decide(*t, answer, a, now);
        bool waiting = answer == 0;

        if (waiting != wasWaiting)
        {
            if (waiting) t->waitSince = now;
            else         t->wentAt = now;
            if (g_cfg.census && TakeLine())
            {
                float v = -1;
                Read(lead, V_SPEED, v);
                if (waiting)
                    Log("rail  sig   train #%d %s WAIT signal %p kind %d free %u/%u chain %u, block %llx (%d segs), v %.1f",
                        t->id, Kind(*t), (void*)a.sig, a.kind, a.freeA, a.freeB, a.chain,
                        (unsigned long long)t->blockKey, t->blockSegs, v);
                else
                    Log("rail  sig   train #%d %s GO after %.1f s, block %llx, v %.1f", t->id, Kind(*t),
                        (double)(now - t->waitSince) / 1000.0, (unsigned long long)t->blockKey, v);
            }
        }
        t->waiting  = waiting;
        t->lastSeen = now;
    }
    ReleaseSRWLockExclusive(&g_lock);
}

char SignalDetour(void* lead)
{
    // Installed from a background thread: in the instant before the loader
    // has filled in the trampoline, the safe answer is "wait one more frame".
    SignalFn orig = g_origSignal;
    if (!orig) return 0;
    char answer = orig(lead);   // the game's own answer first, always
    if (lead) Observe((BYTE*)lead, answer);
    return answer;
}

HMODULE g_exe = nullptr;

void LogLive(const char* how)
{
    Log("prio  live (%s): priority %s in the ini (deadlock guard %d s), census %s", how,
        g_cfg.priority ? "ON" : "off", g_cfg.maxHoldSec, g_cfg.census ? "on" : "off");
}

// In front of another plugin's 14-byte jump. True when hooked.
bool InstallChained()
{
    aob::Hit hit = aob::ScanModule(g_exe, ".text", kSigSignalHooked, "signal check (hooked by another plugin)");
    if (hit.result != aob::Result::Unique) return false;
    unsigned char expect[14];
    memcpy(expect, hit.addr, sizeof(expect));   // exactly that jump, nothing else
    if (!hooks::InstallHostHook("rail signal check (chained)", hit.addr, (void*)&SignalDetour,
                                (void**)&g_origSignal, expect, sizeof(expect)))
        return false;
    LogLive("chained in front of another plugin");
    return true;
}

bool InstallNow()
{
    aob::Hit hit = aob::ScanModule(g_exe, ".text", kSigSignalFn, "signal check fn");
    if (hit.result == aob::Result::Unique)
    {
        if (!hooks::InstallHostHook("rail signal check", hit.addr, (void*)&SignalDetour, (void**)&g_origSignal,
                                    kSignalExpect, sizeof(kSignalExpect)))
            return false;
        LogLive("first owner");
        return true;
    }
    if (InstallChained()) return true;
    Log("prio  signal check changed in a way we do not know - priority OFF, game unchanged");
    return false;
}

DWORD WINAPI DeferredInstall(LPVOID)
{
    Sleep(5000);
    InstallNow();
    return 0;
}

}  // namespace

bool Start(const TsmHost* host, HMODULE exe, const Config& cfg)
{
    g_host      = host;
    g_cfg       = cfg;
    g_linesLeft = cfg.logLimit > 0 ? cfg.logLimit : 0;
    // The offered values; anything else snaps to the nearest one.
    static const int kHold[] = {30, 60, 90, 120, 180};
    int hold = kHold[0];
    for (int v : kHold)
        if (abs(v - cfg.maxHoldSec) < abs(hold - cfg.maxHoldSec)) hold = v;
    if (hold != cfg.maxHoldSec) Log("prio  max_hold_s = %d is not offered, using %d", cfg.maxHoldSec, hold);
    g_cfg.maxHoldSec = hold;
    g_pcfg.maxHoldMs = (uint32_t)hold * 1000u;

    if (!cfg.census && !cfg.priority)
    {
        Log("prio  signal census and priority both off - nothing hooked");
        return false;
    }

    if (cfg.priority) script_switch::Init(host, exe);

    g_exe = exe;
    // Another plugin got there first: chain in front of it now.
    if (InstallChained()) return true;

    // Untouched: wait a few seconds before hooking, so that a plugin which
    // starts after us and expects the original bytes (Rail Operations) still
    // gets its own hook in - then we chain in front of it. The game is at its
    // loading screen by then; no train asks the signal check yet.
    if (aob::ScanModule(exe, ".text", kSigSignalFn, "signal check fn").result != aob::Result::Unique)
    {
        Log("prio  signal check not found - priority OFF, game unchanged");
        return false;
    }
    HANDLE th = CreateThread(nullptr, 0, DeferredInstall, nullptr, 0, nullptr);
    if (!th)
    {
        Log("prio  could not start the deferred hook thread - hooking now");
        return InstallNow();
    }
    CloseHandle(th);
    Log("prio  signal check found; hooking in 5 s (after the other plugins)");
    return true;   // a hook is coming: the plugin must stay loaded
}

}  // namespace wrsr::signal_priority
