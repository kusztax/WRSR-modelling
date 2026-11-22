#include "priority_policy.h"

namespace wrsr::priority {

Verdict Decide(const Request& r, const PaxView* pax, int n, const Config& cfg, int* who)
{
    if (who) *who = 0;

    // Passenger trains are never held. Between two passenger trains (or two
    // freight trains) the game's own order stands.
    if (r.isPax) return Verdict::Go;

    // Only ever keep a waiting train waiting: a train that is moving towards
    // a green signal is never turned red in front of its nose.
    if (!r.wasWaiting && r.holdStartMs == 0) return Verdict::Go;

    // Without knowing the block we cannot tell who competes for it.
    if (r.blockKey == 0) return Verdict::Go;

    int found = 0;
    for (int i = 0; i < n && !found; ++i)
    {
        const PaxView& p = pax[i];
        if (p.blockKey != r.blockKey) continue;   // a different block
        if (p.node == r.node) continue;           // same signal: a queue, not a junction
        if (r.nowMs - p.lastSeenMs > cfg.staleMs) continue;   // gone, sold, or far away
        bool justWent = p.wentAtMs != 0 && r.nowMs - p.wentAtMs <= cfg.graceMs;
        if (p.waiting || justWent) found = p.id ? p.id : -1;
    }
    if (!found) return Verdict::Go;

    if (who) *who = found;
    if (r.holdStartMs != 0 && r.nowMs - r.holdStartMs >= cfg.maxHoldMs) return Verdict::Timeout;
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
