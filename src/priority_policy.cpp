#include "priority_policy.h"

namespace wrsr::priority {
namespace {

// A competitor for the same block, at another signal, recently seen, and
// waiting or just given GO. Freight competitors count only when due.
int FindCompetitor(const Request& r, const TrainView* t, int n, const Config& cfg, bool needDue)
{
    for (int i = 0; i < n; ++i)
    {
        const TrainView& p = t[i];
        if (p.blockKey != r.blockKey) continue;               // a different block
        if (p.node == r.node) continue;                       // same signal: a queue, not a junction
        if (p.lastSeenMs < r.nowMs && r.nowMs - p.lastSeenMs > cfg.staleMs) continue;   // gone, sold, far away
        if (needDue && !p.due) continue;
        bool justWent = p.wentAtMs != 0 && r.nowMs - p.wentAtMs <= cfg.graceMs;
        if (p.waiting || justWent) return p.id ? p.id : -1;
    }
    return 0;
}

}  // namespace

bool IsDue(uint64_t starveSinceMs, uint64_t nowMs, const Config& cfg)
{
    return starveSinceMs != 0 && nowMs - starveSinceMs >= cfg.maxHoldMs;
}

Verdict Decide(const Request& r, const TrainView* others, int n, const Config& cfg, int* who)
{
    if (who) *who = 0;

    // Only ever keep a waiting train waiting: a train that is moving towards
    // a green signal is never turned red in front of its nose.
    if (!r.wasWaiting && r.holdStartMs == 0) return Verdict::Go;

    // Without knowing the block we cannot tell who competes for it.
    if (r.blockKey == 0) return Verdict::Go;

    if (r.isPax)
    {
        // A passenger train yields only to a freight train whose maximum
        // wait has run out, and only for a short while.
        int found = FindCompetitor(r, others, n, cfg, true);
        if (!found) return Verdict::Go;
        if (who) *who = found;
        if (r.holdStartMs != 0 && r.nowMs - r.holdStartMs >= cfg.paxYieldMaxMs) return Verdict::Timeout;
        return Verdict::Hold;
    }

    int found = FindCompetitor(r, others, n, cfg, false);
    if (!found) return Verdict::Go;
    if (who) *who = found;
    if (IsDue(r.starveSinceMs, r.nowMs, cfg)) return Verdict::Timeout;
    return Verdict::Hold;
}

const char* ToString(Verdict v)
{
    switch (v)
    {
    case Verdict::Go:      return "go";
    case Verdict::Hold:    return "hold";
    case Verdict::Timeout: return "timeout";
    }
    return "?";
}

}  // namespace wrsr::priority
