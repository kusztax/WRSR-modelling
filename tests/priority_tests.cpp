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

static Request Freight(uint64_t now, uint64_t holdStart = 0, bool waiting = true, uint64_t starveSince = 0)
{
    return Request{false, waiting, 0x1000, B, holdStart, starveSince, now};
}

static Request Passenger(uint64_t now, uint64_t holdStart = 0, bool waiting = true)
{
    return Request{true, waiting, 0x1000, A, holdStart, 0, now};
}

static TrainView Pax(bool waiting, uint64_t lastSeen, uint64_t wentAt = 0, uint64_t block = 0x1000,
                   const void* node = A, int id = 7)
{
    return TrainView{block, node, waiting, lastSeen, wentAt, id, false};
}

static TrainView Frt(bool waiting, uint64_t lastSeen, bool due, uint64_t wentAt = 0, int id = 3)
{
    return TrainView{0x1000, B, waiting, lastSeen, wentAt, id, due};
}

int main()
{
    Config cfg;
    int who = -99;

    // The census case: freight and passenger both waiting on different
    // branches for the same block; the block frees -> freight must wait.
    {
        TrainView p[1] = {Pax(true, 10000)};
        CHECK(Decide(Freight(10010), p, 1, cfg, &who) == Verdict::Hold && who == 7);
    }
    // A passenger train is not held by a freight train that is not due.
    {
        TrainView f[1] = {Frt(true, 10000, false)};
        CHECK(Decide(Passenger(10010), f, 1, cfg, &who) == Verdict::Go && who == 0);
    }
    // A moving freight (was not waiting, not held) is never stopped.
    {
        TrainView p[1] = {Pax(true, 10000)};
        CHECK(Decide(Freight(10010, 0, false), p, 1, cfg, &who) == Verdict::Go);
    }
    // ...but once we hold it, we keep holding while the passenger train waits.
    {
        TrainView p[1] = {Pax(true, 10000)};
        CHECK(Decide(Freight(10010, 9000, false), p, 1, cfg, &who) == Verdict::Hold);
    }
    // Different block: no competition.
    {
        TrainView p[1] = {Pax(true, 10000, 0, 0x2000)};
        CHECK(Decide(Freight(10010), p, 1, cfg, &who) == Verdict::Go);
    }
    // Same signal: that is a queue, the game's order stands.
    {
        TrainView p[1] = {Pax(true, 10000, 0, 0x1000, B)};
        CHECK(Decide(Freight(10010), p, 1, cfg, &who) == Verdict::Go);
    }
    // Unknown block on either side: never hold.
    {
        TrainView p[1] = {Pax(true, 10000, 0, 0)};
        CHECK(Decide(Freight(10010), p, 1, cfg, &who) == Verdict::Go);
        Request r = Freight(10010);
        r.blockKey = 0;
        TrainView q[1] = {Pax(true, 10000)};
        CHECK(Decide(r, q, 1, cfg, &who) == Verdict::Go);
    }
    // Stale passenger train (not seen for > staleMs): ignored.
    {
        TrainView p[1] = {Pax(true, 10000)};
        CHECK(Decide(Freight(10000 + cfg.staleMs + 1), p, 1, cfg, &who) == Verdict::Go);
        CHECK(Decide(Freight(10000 + cfg.staleMs), p, 1, cfg, &who) == Verdict::Hold);
    }
    // Passenger train just given GO in the same frame race: still protected
    // for graceMs, then not any more.
    {
        TrainView p[1] = {Pax(false, 20000, 20000)};
        CHECK(Decide(Freight(20000 + 500), p, 1, cfg, &who) == Verdict::Hold);
        TrainView q[1] = {Pax(false, 20000 + cfg.graceMs + 1, 20000)};
        CHECK(Decide(Freight(20000 + cfg.graceMs + 1), q, 1, cfg, &who) == Verdict::Go);
        TrainView r[1] = {Pax(false, 20000, 0)};   // never went: not waiting, not protected
        CHECK(Decide(Freight(20010), r, 1, cfg, &who) == Verdict::Go);
    }
    // Maximum wait: counted from the first hold at this signal, not per
    // passenger train. After maxHoldMs the freight is due and goes.
    {
        TrainView p[1] = {Pax(true, 100000)};
        CHECK(Decide(Freight(100000, 99000, true, 100000 - cfg.maxHoldMs), p, 1, cfg, &who) == Verdict::Timeout && who == 7);
        CHECK(Decide(Freight(100000, 99000, true, 100000 - cfg.maxHoldMs + 1), p, 1, cfg, &who) == Verdict::Hold);
        CHECK(Decide(Freight(100000, 0, true, 0), p, 1, cfg, &who) == Verdict::Hold);   // first hold
        CHECK(IsDue(100000 - cfg.maxHoldMs, 100000, cfg) && !IsDue(0, 100000, cfg));
    }
    // A waiting passenger train yields to a due freight train at another signal...
    {
        TrainView f[1] = {Frt(true, 10000, true)};
        CHECK(Decide(Passenger(10010), f, 1, cfg, &who) == Verdict::Hold && who == 3);
        // ...also right after the freight was given GO (the race frame)...
        TrainView g[1] = {Frt(false, 10000, true, 10000)};
        CHECK(Decide(Passenger(10500), g, 1, cfg, &who) == Verdict::Hold);
        // ...but at most paxYieldMaxMs.
        TrainView late[1] = {Frt(true, 10000 + cfg.paxYieldMaxMs, true)};
        CHECK(Decide(Passenger(10000 + cfg.paxYieldMaxMs, 10000), late, 1, cfg, &who) == Verdict::Timeout);
        CHECK(Decide(Passenger(10000 + cfg.paxYieldMaxMs - 1, 10000), late, 1, cfg, &who) == Verdict::Hold);
        // A moving passenger train is never stopped.
        CHECK(Decide(Passenger(10010, 0, false), f, 1, cfg, &who) == Verdict::Go);
        // A stale due freight does not count.
        CHECK(Decide(Passenger(10000 + cfg.staleMs + 1), f, 1, cfg, &who) == Verdict::Go);
        // Same signal: a queue.
        TrainView h[1] = {TrainView{0x1000, A, true, 10000, 0, 3, true}};
        CHECK(Decide(Passenger(10010), h, 1, cfg, &who) == Verdict::Go);
    }
    // Several candidates: any matching one holds.
    {
        TrainView p[3] = {Pax(true, 10000, 0, 0x2000, A, 1), Pax(false, 10000, 0, 0x1000, A, 2),
                        Pax(true, 10000, 0, 0x1000, A, 3)};
        CHECK(Decide(Freight(10010), p, 3, cfg, &who) == Verdict::Hold && who == 3);
    }
    // Empty list.
    CHECK(Decide(Freight(10010), nullptr, 0, cfg, &who) == Verdict::Go);

    if (g_failed) { std::printf("%d check(s) failed\n", g_failed); return EXIT_FAILURE; }
    std::printf("priority_tests: all passed\n");
    return EXIT_SUCCESS;
}
