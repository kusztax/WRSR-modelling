// signal_priority.h - the hook on the game's signal check, and what it does.
//
// The game asks `char maySignalPass(lead)` (60-120 times a second per train
// near a signal) whether a train may pass the signal at the node it heads
// for. We hook it through the loader (chains with Rail Operations under RML),
// call the original first, and then:
//
//   census   (ini signal_census = 1): log every WAIT/GO change per train,
//            with the signal's flags and the identity of the block behind it.
//   priority (ini priority = 1): when the game says GO to a WAITING freight
//            train while a passenger train waits for the same block at another
//            signal, answer WAIT instead - for at most `max_hold_s`.
//            The decision itself is priority_policy.cpp.
//
// Nothing is written to game memory and nothing to the save: the only effect
// is a different answer from one function, so a save made with the mod loads
// unchanged without it.
#pragma once

#include <windows.h>

struct TsmHost;

namespace wrsr::signal_priority {

struct Config
{
    bool census     = false;
    bool priority   = false;
    int  maxHoldSec = 60;
    int  logLimit   = 3000;
};

// Call from TsmPluginStart. True when the hook is live.
bool Start(const TsmHost* host, HMODULE exe, const Config& cfg);

}  // namespace wrsr::signal_priority
