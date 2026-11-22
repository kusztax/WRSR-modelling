// Host-native unit tests for the signal-priority decision.

#include <cstdio>
#include <cstdlib>

#include "priority_policy.h"

using namespace wrsr::priority;

static int g_failed = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failed;                                                 \
        }                                                               \
    } while (0)

static const int kNodeA = 0, kNodeB = 0;   // two distinct addresses
static const void* A = &kNodeA;
static const void* B = &kNodeB;

static Request Freight(uint64_t now, uint64_t holdStart = 0, bool waiting = true)
{
    return Request{false, waiting, 0x1000, B, holdStart, now};
}

static PaxView Pax(bool waiting, uint64_t lastSeen, uint64_t wentAt = 0, uint64_t block = 0x1000,
                   const void* node = A, int id = 7)
{
    return PaxView{block, node, waiting, lastSeen, wentAt, id};
}

int main()
{
    Config cfg;
    int who = -99;

    // The census case: freight and passenger both waiting on different
    // branches for the same block; the block frees -> freight must wait.
    {
        PaxView p[1] = {Pax(true, 10000)};
        CHECK(Decide(Freight(10010), p, 1, cfg, &who) == Verdict::Hold && who == 7);
    }
    // A passenger train is never held.
    {
        PaxView p[1] = {Pax(true, 10000)};
        Request r = Freight(10010);
        r.isPax = true;
        CHECK(Decide(r, p, 1, cfg, &who) == Verdict::Go && who == 0);
    }
    // A moving freight (was not waiting, not held) is never stopped.
    {
        PaxView p[1] = {Pax(true, 10000)};
        CHECK(Decide(Freight(10010, 0, false), p, 1, cfg, &who) == Verdict::Go);
    }
    // ...but once we hold it, we keep holding while the passenger train waits.
    {
        PaxView p[1] = {Pax(true, 10000)};
        CHECK(Decide(Freight(10010, 9000, false), p, 1, cfg, &who) == Verdict::Hold);
    }
    // Different block: no competition.
    {
        PaxView p[1] = {Pax(true, 10000, 0, 0x2000)};
        CHECK(Decide(Freight(10010), p, 1, cfg, &who) == Verdict::Go);
    }
    // Same signal: that is a queue, the game's order stands.
    {
        PaxView p[1] = {Pax(true, 10000, 0, 0x1000, B)};
        CHECK(Decide(Freight(10010), p, 1, cfg, &who) == Verdict::Go);
    }
    // Unknown block on either side: never hold.
    {
        PaxView p[1] = {Pax(true, 10000, 0, 0)};
        CHECK(Decide(Freight(10010), p, 1, cfg, &who) == Verdict::Go);
        Request r = Freight(10010);
        r.blockKey = 0;
        PaxView q[1] = {Pax(true, 10000)};
        CHECK(Decide(r, q, 1, cfg, &who) == Verdict::Go);
    }
    // Stale passenger train (not seen for > staleMs): ignored.
    {
        PaxView p[1] = {Pax(true, 10000)};
        CHECK(Decide(Freight(10000 + cfg.staleMs + 1), p, 1, cfg, &who) == Verdict::Go);
        CHECK(Decide(Freight(10000 + cfg.staleMs), p, 1, cfg, &who) == Verdict::Hold);
    }
    // Passenger train just given GO in the same frame race: still protected
    // for graceMs, then not any more.
    {
        PaxView p[1] = {Pax(false, 20000, 20000)};
        CHECK(Decide(Freight(20000 + 500), p, 1, cfg, &who) == Verdict::Hold);
        PaxView q[1] = {Pax(false, 20000 + cfg.graceMs + 1, 20000)};
        CHECK(Decide(Freight(20000 + cfg.graceMs + 1), q, 1, cfg, &who) == Verdict::Go);
        PaxView r[1] = {Pax(false, 20000, 0)};   // never went: not waiting, not protected
        CHECK(Decide(Freight(20010), r, 1, cfg, &who) == Verdict::Go);
    }
    // Deadlock guard: after maxHoldMs the freight goes.
    {
        PaxView p[1] = {Pax(true, 100000)};
        CHECK(Decide(Freight(100000, 100000 - cfg.maxHoldMs), p, 1, cfg, &who) == Verdict::Timeout && who == 7);
        CHECK(Decide(Freight(100000, 100000 - cfg.maxHoldMs + 1), p, 1, cfg, &who) == Verdict::Hold);
    }
    // Several candidates: any matching one holds.
    {
        PaxView p[3] = {Pax(true, 10000, 0, 0x2000, A, 1), Pax(false, 10000, 0, 0x1000, A, 2),
                        Pax(true, 10000, 0, 0x1000, A, 3)};
        CHECK(Decide(Freight(10010), p, 3, cfg, &who) == Verdict::Hold && who == 3);
    }
    // Empty list.
    CHECK(Decide(Freight(10010), nullptr, 0, cfg, &who) == Verdict::Go);

    if (g_failed) { std::printf("%d check(s) failed\n", g_failed); return EXIT_FAILURE; }
    std::printf("priority_tests: all passed\n");
    return EXIT_SUCCESS;
}
