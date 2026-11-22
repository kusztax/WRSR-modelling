// rail_probe.h - M1: read the game's train and block data, change nothing.
//
// 1. Locates the global chain vector (every block section, station, depot...)
//    by pattern, read-only.
// 2. Optionally (ini `train_census = 1`) hooks the rail acceleration helper
//    `float divisor(vehicle*)` through the LOADER's hook (it chains with
//    RailPhysics / Rail Operations under RML). The detour calls the original
//    first and returns its value untouched; afterwards, on the game thread, it
//    logs each train once (consist, passenger or not) and every change of the
//    station chain it heads for. Every 60 s a histogram of chain types, every
//    30 s per-train call counts and speed.
// The signal check is hooked by signal_priority.cpp.
//
// Purpose: confirm on 1.1.1.13 the offsets in game_offsets.h, and watch how
// trains wait at and pass signals - the input for the signal-priority hook.
#pragma once

#include <windows.h>

struct TsmHost;

namespace wrsr::rail_probe {

struct Config
{
    bool trainCensus  = false;  // log-only hook on the rail acceleration helper
    int  logLimit    = 3000;   // census lines per session, then silence
};

// Call from TsmPluginStart. Returns true if anything was hooked (the plugin
// must then never ask to be unloaded).
bool Start(const TsmHost* host, HMODULE exe, const Config& cfg);

}  // namespace wrsr::rail_probe
