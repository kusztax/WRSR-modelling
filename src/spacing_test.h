// spacing_test.h - TEST ONLY (spacing_test = 1). Game 1.2 keeps a line's
// "spacing" switch per vehicle at vehicle+0x658, but offers it only for road
// lines. This test turns it on for passenger rail vehicles that carry their
// own passengers (trams, railcars: category 4, passenger class 7), and lets
// their lines through the game's road-only spacing gate, to see whether the
// game's own spacing (a speed cap at vehicle+0x670) then spaces them out.
#pragma once

#include <windows.h>
#include <stdint.h>

namespace wrsr::spacing_test {

void Start(HMODULE exe);
// Any train vehicle seen at a signal: it shows where the vehicle array is.
void NoteVehicle(const void* veh);

}  // namespace wrsr::spacing_test
