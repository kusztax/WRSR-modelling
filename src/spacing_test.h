// spacing_test.h - TEST ONLY (spacing_test = 1). Game 1.2 keeps a line's
// "spacing" switch per vehicle at vehicle+0x658, but offers it only for road
// lines. This test turns it on for passenger rail vehicles that carry their
// own passengers (trams, railcars: category 4, passenger class 7) to see
// whether the game then spaces them out on their line.
#pragma once

#include <stdint.h>

namespace wrsr::spacing_test {

void Start();
// Any train vehicle seen at a signal: it shows where the vehicle array is.
void NoteVehicle(const void* veh);

}  // namespace wrsr::spacing_test
