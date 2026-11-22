// hooks.h - two hook backends, and which one to use when.
//
// HOST (TesmioLoader's installInlineHook, also provided by RML's Tesmio
//   compatibility layer) - USE THIS FOR GAME CODE. It checks the expected
//   bytes before writing, and under RML it CHAINS with other plugins that hook
//   the same function. RailPhysics and Rail Operations (railreverse) both hook
//   the rail update through it; a MinHook patch on the same site is invisible
//   to that chain tracking and can silently break them or us.
//
// MINHOOK - for targets nobody else touches (our own code, the self-test,
//   OS APIs), and for running without a loader. It auto-relocates the
//   prologue, so it needs no expected bytes - which is exactly why it is the
//   wrong default for game code: it will happily hook a site that a game
//   update has changed.
#pragma once

#include <cstddef>

struct TsmHost;

namespace wrsr::hooks {

// Remember the loader's host table (may be null when running standalone).
void SetHost(const TsmHost* host);

// MinHook lifecycle. Init is idempotent.
bool InitMinHook();

// MinHook: create + enable. `original` receives the trampoline.
bool InstallMinHook(const char* label, void* target, void* detour, void** original);

// Host: `expect` must match the first `stolen` bytes of `target` exactly
// (stolen >= 14 and must end on an instruction boundary).
bool InstallHostHook(const char* label, void* target, void* detour, void** original,
                     const unsigned char* expect, size_t stolen);

// Hooks a function inside this DLL, calls it, unhooks it, and checks the
// results at each step. Proves that executable allocation, VirtualProtect and
// instruction-cache flushing behave under Wine + Rosetta 2 before any game
// code is touched. Logs every step; returns true when all of them passed.
bool RunMinHookSelfTest();

}  // namespace wrsr::hooks
