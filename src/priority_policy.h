// priority_policy.h - the signal-priority decision, as a pure function.
//
// No game memory, no Windows: unit-tested natively (tests/priority_tests.cpp).
// signal_priority.cpp gathers the facts from the game and asks this.
//
// What the census showed (1.1.1.13, docs/04-m1-census.md): when a shared
// block frees, the signal check says GO to every train waiting for it in the
// same frame; the trains then race for the block and the freight won every
// time, although the passenger train had waited longer. So the lever is: keep
// a waiting freight train's answer at WAIT while a passenger train waits for
// (or has just been given) the same block from another signal.
#pragma once

#include <cstdint>

namespace wrsr::priority {

struct Config
{
    uint32_t maxHoldMs = 60000;  // a freight is never held longer: the deadlock guard
    uint32_t staleMs   = 1500;   // a passenger train not seen for this long does not count
    uint32_t graceMs   = 1500;   // after a passenger train was given GO, still protect its block
};

// What we know about one passenger train.
struct PaxView
{
    uint64_t blockKey;   // the block behind the signal it heads for (0 = unknown)
    const void* node;    // that signal's node
    bool     waiting;    // its last answer was WAIT
    uint64_t lastSeenMs; // when its signal check last ran
    uint64_t wentAtMs;   // when its answer last turned to GO (0 = never)
    int      id;
};

// The train asking right now.
struct Request
{
    bool        isPax;
    bool        wasWaiting;  // its previous effective answer was WAIT
    uint64_t    blockKey;
    const void* node;
    uint64_t    holdStartMs; // when we started holding it (0 = not held)
    uint64_t    nowMs;
};

enum class Verdict
{
    Go,       // pass the game's GO through
    Hold,     // answer WAIT instead
    Timeout,  // would hold, but it has waited maxHoldMs already: GO
};

// Called only when the game itself answered GO. `who` receives the id of the
// passenger train the request yields to (Hold / Timeout), else 0.
Verdict Decide(const Request& r, const PaxView* pax, int n, const Config& cfg, int* who);

const char* ToString(Verdict v);

}  // namespace wrsr::priority
