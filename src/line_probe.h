// line_probe.h - TEST ONLY (line_probe = 1): where does game 1.2 keep a
// line's "spacing" switch? Finds the line by its length (line_lengths in the
// ini, as shown in the line window), snapshots the memory around it and logs
// every byte that changes, so toggling the switch in the game shows its place.
#pragma once

namespace wrsr::line_probe {

void Start(const char* lengthsCsv);

}  // namespace wrsr::line_probe
