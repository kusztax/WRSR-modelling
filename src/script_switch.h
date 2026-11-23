// script_switch.h - the Options -> Scripts on/off buttons.
//
// The game's script system cannot call into a DLL, so the switch is a script
// that only sleeps (RailPriority_OffFlag.txt): while it runs, the priority is
// OFF. The two buttons start / stop it (same technique as Rail Operations by
// pautov-ya, GPL-3.0). The game keeps running scripts in the save, so the
// choice is per save; with no flag running - a new game, or any save from
// before the mod - the priority is ON.
//
// Running scripts: world +0x13BB8 is a vector of script*, each with its file
// and name as wide strings at *(+0xB8) / *(+0xC0).
#pragma once

#include <windows.h>

struct TsmHost;

namespace wrsr::script_switch {

bool Init(const TsmHost* host, HMODULE exe);

// True while the OFF flag script runs. Cheap: rescans at most every 500 ms.
// Call from the game thread (the signal hook).
bool PriorityOffByScript();

// The jam-guard time chosen with the Scripts menu button (30, 60, 90, 120,
// 180 s: a running RailPriority_Hold<N>Flag script), or 0 when none runs and
// the ini's max_hold_s applies. Same scan as above.
int HoldSecondsByScript();

// The game's world object (what the game's own functions take), or null.
void* World();

}  // namespace wrsr::script_switch
