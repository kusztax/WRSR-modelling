// line_spacing.h - the game's own line spacing ("sortávolság") for tram and
// railcar lines. Game 1.2 offers it only on road lines: one gate function
// says no for every other line, and the line window shows the switch only for
// road lines. We widen both to lines whose vehicles all carry their own
// passengers on rails (category 4, passenger class 7: trams, railcars); the
// game's switch, spacing (a speed cap) and saving then work unchanged.
// See docs/10-line-spacing-findings.md.
#pragma once

#include <windows.h>

struct TsmHost;

namespace wrsr::line_spacing {

// Returns true when both changes are in place; logs and changes nothing it
// does not recognise.
bool Start(const TsmHost* host, HMODULE exe);

}  // namespace wrsr::line_spacing
