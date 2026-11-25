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
#include "spacing_test.h"
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
    int       holdFor;      // id of the train it yields to
    ULONGLONG starveSince;  // freight: first held at this signal (0 = never); the maximum wait counts from here
    BYTE*     redSig;       // signal we turned red for this hold (signal_aspect)
    bool      redA, redB;   // which of its two block flags we cleared
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
        return Read(unit, g_layout.vType, type) && Read(type, g_layout.tTransport, cls) &&
               cls == TRANSPORT_PASSENGERS;
    };
    if (!g_layoutKnown) return false;   // unknown game layout: never hold anyone
    if (unitIsPax(lead)) return true;
    BYTE** b = nullptr;
    BYTE** e = nullptr;
    if (!Read(lead, V_WAGON_VEC, b) || !Read(lead, V_WAGON_VEC + 8, e)) return false;
    if (!b || e <= b || e - b > 512 || !Readable(b, (size_t)(e - b) * sizeof(BYTE*))) return false;
    for (BYTE** p = b; p != e; ++p)
        if (unitIsPax(*p)) return true;
    return false;
}


// ---- layout probe (census on): game updates move vehicle fields. For a new
// train, find which pointer field of the vehicle leads to its type (the lead
// a locomotive, category 4; every car 3 or 5), trying type-category offsets
// around the known one too, and dump the types' bytes around the transport
// class so the passenger value can be read off.
bool ReadI(BYTE* p, uintptr_t off, int& v) { return Read(p, off, v); }

void ProbeLayout(BYTE* lead, int id)
{
    static int probes = 0;
    if (probes >= 6) return;
    ++probes;
    BYTE** b = nullptr;
    BYTE** e = nullptr;
    BYTE*  units[16];
    int    n = 0;
    if (Read(lead, V_WAGON_VEC, b) && Read(lead, V_WAGON_VEC + 8, e) && b && e > b && e - b <= 512 &&
        Readable(b, (size_t)(e - b) * sizeof(BYTE*)))
        for (BYTE** p = b; p != e && n < 16; ++p) if (*p && *p != lead) units[n++] = *p;
    Log("probe train #%d lead %p: %d other units in the consist vector (+0x%X)", id, (void*)lead, n, (unsigned)V_WAGON_VEC);
    int found = 0;
    for (uintptr_t k = 0x1400; k < 0x1E00 && found < 6; k += 8)
        for (uintptr_t c = 0x240; c < 0x300 && found < 6; c += 4)
        {
            BYTE* t = nullptr;
            int   cat = 0;
            if (!Read(lead, k, t) || !t || ((uintptr_t)t & 7) || !ReadI(t, c, cat) || cat != CAT_LOCO) continue;
            bool ok = n > 0;
            for (int i = 0; i < n && ok; ++i)
            {
                BYTE* wt = nullptr;
                int   wc = 0;
                ok = Read(units[i], k, wt) && wt && ReadI(wt, c, wc) && (wc == CAT_WAGON || wc == CAT_SERVICE);
            }
            if (!ok) continue;
            ++found;
            Log("probe   type at vehicle+0x%X, category at type+0x%X (known: +0x%X / +0x%X)", (unsigned)k, (unsigned)c,
                (unsigned)g_layout.vType, (unsigned)g_layout.tCategory);
            if (found > 1) continue;
            // the transport class: dump ints of the lead's and two cars' types
            BYTE* types[3] = {nullptr, nullptr, nullptr};
            Read(lead, k, types[0]);
            if (n > 0) Read(units[0], k, types[1]);
            if (n > 1) Read(units[n - 1], k, types[2]);
            for (int u = 0; u < 3; ++u)
            {
                if (!types[u]) continue;
                for (uintptr_t base = 0x8580; base < 0x86C0; base += 0x40)
                {
                    char line[200];
                    int  len = 0;
                    for (uintptr_t o = base; o < base + 0x40; o += 4)
                    {
                        int v = 0;
                        ReadI(types[u], o, v);
                        len += snprintf(line + len, sizeof(line) - len, "%d ", v);
                    }
                    Log("probe   %s type +0x%X: %s", u == 0 ? "lead" : (u == 1 ? "car1" : "carN"), (unsigned)base, line);
                }
            }
        }
    if (!found) Log("probe   no vehicle field leads to a type with category 4 (lead) / 3,5 (cars)");
}

// Which vehicle layout is this game? Decided on the first train with cars:
// the layout where the lead is a locomotive and every car a wagon/service
// car. Up to 8 trains with cars are tried; none matching -> passenger
// detection stays off (and the layout probe logs what it can see).
int g_layoutTries = 0;

bool LayoutFits(const VehicleLayout& L, BYTE* lead, BYTE** units, int n)
{
    BYTE* t = nullptr;
    int   c = 0;
    if (!Read(lead, L.vType, t) || !t || !Read(t, L.tCategory, c) || c != CAT_LOCO) return false;
    for (int i = 0; i < n; ++i)
    {
        BYTE* wt = nullptr;
        int   wc = 0;
        if (!Read(units[i], L.vType, wt) || !wt || !Read(wt, L.tCategory, wc) || (wc != CAT_WAGON && wc != CAT_SERVICE))
            return false;
    }
    return true;
}

void ChooseLayout(BYTE* lead)
{
    if (g_layoutKnown || g_layoutTries >= 8) return;
    BYTE** b = nullptr;
    BYTE** e = nullptr;
    BYTE*  units[16];
    int    n = 0;
    if (Read(lead, V_WAGON_VEC, b) && Read(lead, V_WAGON_VEC + 8, e) && b && e > b && e - b <= 512 &&
        Readable(b, (size_t)(e - b) * sizeof(BYTE*)))
        for (BYTE** p = b; p != e && n < 16; ++p) if (*p && *p != lead) units[n++] = *p;
    if (n == 0) return;   // a lone locomotive proves nothing
    ++g_layoutTries;
    for (const VehicleLayout& L : kLayouts)
        if (LayoutFits(L, lead, units, n))
        {
            g_layout      = L;
            g_layoutKnown = true;
            Log("prio  vehicle layout: game %s (type at +0x%X, passengers class at type+0x%X)", L.game,
                (unsigned)L.vType, (unsigned)L.tTransport);
            // trains seen before this were classified with no layout
            for (Train& t : g_train)
                if (t.veh) t.pax = IsPassengerTrain(t.veh);
            return;
        }
    if (g_layoutTries == 8)
        Log("prio  vehicle layout UNKNOWN (newer game?) - passenger trains cannot be told apart, priority does nothing");
    ProbeLayout(lead, -g_layoutTries);
}

Train* Find(BYTE* veh)
{
    ChooseLayout(veh);
    BYTE* type = nullptr;
    if (!Read(veh, g_layout.vType, type)) return nullptr;

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
                if (g_cfg.census) ProbeLayout(veh, t.id);
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
            if (g_cfg.census) ProbeLayout(veh, t.id);
            return &t;
        }
    }
    return nullptr;
}

const char* Kind(const Train& t) { return t.pax ? "PAX" : "FRT"; }

// ---------------------------------------------------------------- decision

using SignalFn = char (*)(void*);
SignalFn g_origSignal = nullptr;

// ---------------------------------------------------------------- aspect
// The game keeps two "block free" flags on a signal (+0x1A8 / +0x1A9, 1 =
// free) and works them out again with FUN_14041ec00(world, signal, 1, 1)
// whenever a train passes (Rail Operations' finding). While a freight train
// is held, the block in front of it is really free, so its signal shows
// green. signal_aspect = 1 clears the flag that says "free" for the held
// train's signal, and has the game work the flags out afresh on release.
// Experimental (v1.1): whether the drawn aspect follows these flags is what
// the in-game test decides.
const char kSigBlockState[] =
    "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 41 56 48 83 EC 30 4C 8B 72 08 33 F6";
using BlockStateFn = void (*)(void* world, BYTE* signal, char a, char b);
BlockStateFn g_blockState = nullptr;

void TurnRed(Train& t, const Ahead& a)
{
    if (!g_cfg.aspect || !a.sig || t.redSig) return;
    unsigned char fa = 0, fb = 0;
    Read(a.sig, S_FREE_A, fa);
    Read(a.sig, S_FREE_B, fb);
    t.redSig = a.sig;
    t.redA   = fa == 1;
    t.redB   = fb == 1;
    if (t.redA) *(a.sig + S_FREE_A) = 0;
    if (t.redB) *(a.sig + S_FREE_B) = 0;
    if (TakeLine())
        Log("aspect RED   signal %p flags %u/%u -> %u/%u (train #%d held)", (void*)a.sig, fa, fb,
            t.redA ? 0 : fa, t.redB ? 0 : fb, t.id);
}

// The game may work the flags out again while we hold (a train passing the
// shared block does that): clear them again.
void KeepRed(Train& t)
{
    if (!t.redSig || !Readable(t.redSig + S_FREE_A, 2)) return;
    if (t.redA && *(t.redSig + S_FREE_A) != 0) *(t.redSig + S_FREE_A) = 0;
    if (t.redB && *(t.redSig + S_FREE_B) != 0) *(t.redSig + S_FREE_B) = 0;
}

void ReleaseRed(Train& t, const char* why)
{
    if (!t.redSig) return;
    BYTE* sig = t.redSig;
    void* world = script_switch::World();
    bool fixed = false;
    if (g_blockState && world && Readable(sig, S_FREE_B + 1))
    {
        // The game's own answer - but only for the flag(s) we cleared. Asked
        // for both, it also set the flag we never touched (0 -> 1 every time
        // in the 2026-11-23 test), and a passenger train queued behind the
        // held one got green. Leave that flag as the game had it.
        unsigned char ka = *(sig + S_FREE_A), kb = *(sig + S_FREE_B);
        g_blockState(world, sig, 1, 1);
        if (!t.redA && *(sig + S_FREE_A) != ka) { *(sig + S_FREE_A) = ka; fixed = true; }
        if (!t.redB && *(sig + S_FREE_B) != kb) { *(sig + S_FREE_B) = kb; fixed = true; }
    }
    else if (Readable(sig, S_FREE_B + 1))
    {
        if (t.redA) *(sig + S_FREE_A) = 1;         // fallback: put back what we took
        if (t.redB) *(sig + S_FREE_B) = 1;
    }
    unsigned char fa = 0, fb = 0;
    Read(sig, S_FREE_A, fa);
    Read(sig, S_FREE_B, fb);
    if (TakeLine())
        Log("aspect FREE  signal %p flags now %u/%u (%s, %s%s)", (void*)sig, fa, fb, why,
            g_blockState && world ? "worked out by the game" : "restored",
            fixed ? "; the other flag kept as it was" : "");
    t.redSig = nullptr;
    t.redA = t.redB = false;
}

Train* FindById(int id)
{
    if (id <= 0) return nullptr;
    for (Train& t : g_train)
        if (t.veh && t.id == id) return &t;
    return nullptr;
}

char Decide(Train& t, char gameAnswer, const Ahead& a, ULONGLONG now)
{
    if (!g_cfg.priority) return gameAnswer;
    // With the signal turned red by us, the game itself says WAIT: the hold
    // must still be looked at every frame, or it would never end.
    bool ownRed = t.redSig != nullptr;
    if (!gameAnswer && !ownRed) return gameAnswer;
    // Cheap exits before scanning the table: only a waiting (or held) train
    // in front of a known block can be held.
    if ((!t.waiting && !t.holdStart) || !t.blockKey)
    {
        ReleaseRed(t, "no longer held");
        return gameAnswer;
    }

    // The player's "pass the red signal" button sets this flag, and the game's
    // own check then always says go (that test is the first thing in the
    // function we hook): never overrule the player.
    unsigned char passAll = 0;
    if (Read(t.veh, V_PASS_ALL, passAll) && passAll)
    {
        if (t.holdStart && TakeLine())
            Log("prio  GO     train #%d %s released: the player told it to pass the red signal", t.id, Kind(t));
        t.holdStart   = 0;
        t.starveSince = 0;
        if (ownRed) { ReleaseRed(t, "player override"); return 0; }
        return gameAnswer;
    }

    // Switched off in the Scripts menu (per save): let a held train go.
    if (script_switch::PriorityOffByScript())
    {
        if (t.holdStart && TakeLine())
            Log("prio  GO     train #%d %s released: priority switched off in the Scripts menu", t.id, Kind(t));
        t.holdStart   = 0;
        t.starveSince = 0;
        if (ownRed) { ReleaseRed(t, "switched off"); return 0; }   // real answer next frame
        return gameAnswer;
    }

    // Gather the trains of the other kind that might compete for this block.
    priority::TrainView view[64];
    int n = 0;
    for (int i = 0; i < kSlots && n < 64; ++i)
    {
        const Train& p = g_train[i];
        if (!p.veh || p.pax == t.pax || p.blockKey != t.blockKey || !p.blockKey) continue;
        bool due = !p.pax && priority::IsDue(p.starveSince, now, g_pcfg);
        view[n++] = priority::TrainView{p.blockKey, p.node, p.waiting, p.lastSeen, p.wentAt, p.id, due};
    }

    priority::Request req{t.pax, t.waiting, t.blockKey, t.node, t.holdStart, t.starveSince, now};
    int who = 0;
    priority::Verdict v = priority::Decide(req, view, n, g_pcfg, &who);

    switch (v)
    {
    case priority::Verdict::Hold:
        if (!t.pax && !t.starveSince) t.starveSince = now;   // the maximum wait starts here
        if (!t.holdStart || who != t.holdFor)
        {
            if (!t.holdStart) t.holdStart = now;
            t.holdFor = who;
            if (TakeLine())
            {
                if (t.pax)
                    Log("prio  HOLD   train #%d PAX at signal %p (block %llx) - freight train #%d has waited its maximum, it goes first",
                        t.id, (void*)a.sig, (unsigned long long)t.blockKey, who);
                else
                    Log("prio  HOLD   train #%d FRT at signal %p (block %llx) - passenger train #%d goes first (waiting %.0f of %u s)",
                        t.id, (void*)a.sig, (unsigned long long)t.blockKey, who,
                        (double)(now - t.starveSince) / 1000.0, g_pcfg.maxHoldMs / 1000u);
            }
        }
        if (ownRed) KeepRed(t);
        else TurnRed(t, a);
        return 0;

    case priority::Verdict::Timeout:
        if (t.pax)
        {
            // The freight train could not use its turn (its way on is not
            // free): the passenger train goes, the freight starts counting again.
            if (TakeLine())
                Log("prio  TIMEOUT train #%d PAX released after %.0f s: freight train #%d did not take its turn",
                    t.id, (double)(now - t.holdStart) / 1000.0, who);
            if (Train* f = FindById(who)) f->starveSince = now;
        }
        else if (TakeLine())
            Log("prio  MAXWAIT train #%d FRT has waited %.0f s (maximum %u s) - it goes before the passenger trains",
                t.id, (double)(now - t.starveSince) / 1000.0, g_pcfg.maxHoldMs / 1000u);
        t.holdStart = 0;
        if (ownRed) { ReleaseRed(t, "maximum wait"); return 0; }   // real answer next frame
        return gameAnswer;

    case priority::Verdict::Go:
        if (t.holdStart && TakeLine())
            Log("prio  GO     train #%d %s after being held %.1f s for train #%d", t.id, Kind(t),
                (double)(now - t.holdStart) / 1000.0, t.holdFor);
        t.holdStart = 0;
        if (ownRed) { ReleaseRed(t, "competitor gone"); return 0; }   // real answer next frame
        return gameAnswer;
    }
    return gameAnswer;
}

// A held train that is sold, or stops asking, must not leave its signal red.
void SweepRed(ULONGLONG now)
{
    static ULONGLONG last = 0;
    if (now - last < 2000) return;
    last = now;
    for (Train& t : g_train)
        if (t.veh && t.redSig && now - t.lastSeen > 3000)
        {
            t.holdStart = 0;
            ReleaseRed(t, "train no longer asks");
        }
}

void Observe(BYTE* lead, char& answer)
{
    AcquireSRWLockExclusive(&g_lock);
    // Keep the Scripts menu state (and its log lines) current even when no
    // freight train is about to be held. Throttled inside to every 500 ms.
    if (g_cfg.priority)
    {
        script_switch::PriorityOffByScript();
        int hs = script_switch::HoldSecondsByScript();
        g_pcfg.maxHoldMs = (uint32_t)(hs ? hs : g_cfg.maxHoldSec) * 1000u;
    }
    spacing_test::NoteVehicle(lead);
    Train* t = Find(lead);
    if (t)
    {
        ULONGLONG now = GetTickCount64();
        Ahead a = ReadAhead(lead);

        // The block only matters while it is in front of the train; work it
        // out once per signal, not once per frame.
        if (a.node != t->node)
        {
            // Past the signal (or a new route): the maximum wait starts anew.
            if (t->starveSince && t->holdStart == 0 && TakeLine() && !t->pax)
                Log("prio  PASSED train #%d FRT after waiting %.0f s since first held", t->id,
                    (double)(now - t->starveSince) / 1000.0);
            t->starveSince = 0;
            t->node = a.node;
            BlockId b = WalkBlock(a.seg);
            t->blockKey  = b.key;
            t->blockSegs = b.segs;
        }

        SweepRed(now);
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
    Log("prio  live (%s): priority %s in the ini (maximum wait %d s), census %s", how,
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
    if (cfg.priority && cfg.aspect)
    {
        aob::Hit bs = aob::ScanModule(exe, ".text", kSigBlockState, "signal block-state fn");
        if (bs.result == aob::Result::Unique) g_blockState = (BlockStateFn)bs.addr;
        else Log("aspect block-state function not found - flags are put back by hand on release");
    }

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
