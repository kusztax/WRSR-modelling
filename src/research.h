// research.h - the "Rail priority" research, added to the game's research tree
// in memory (v1.2, work in progress).
//
// The tree is media_soviet/research/research.ini (or a research mod's copy).
// When the game opens a research.ini, we hand it a copy with one more entry:
//   $RESEARCH rail_priority, after logistic_optimization, party university,
//   cost 750, texts 1957021/1957022 (served through GetString),
//   picture research/rail_priority.png (redirected to the plugin folder).
// Nothing on disk is changed; the copy lives in the temp folder.
// Whether it is completed is asked by the scripts (Research_IsCompleted): the
// ON button and RailPriority_Watch.txt start RailPriority_OnFlag.txt, which the
// plugin sees among the running scripts.
#pragma once

#include <windows.h>

namespace wrsr::research {

constexpr int ID_NAME = 1957021;
constexpr int ID_DESC = 1957022;

void Init(HMODULE self, bool enabled);

// File-open filters, called by the lang hooks before the real open: return
// the path to open instead (the same pointer when nothing changes).
const char*    MapA(const char* path);
const wchar_t* MapW(const wchar_t* path);

}  // namespace wrsr::research
