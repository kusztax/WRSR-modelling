// env_info.h - what are we running inside? Logged once at init, so every bug
// report says which game build, which Wine/CrossOver and which host OS.
#pragma once

#include <windows.h>

namespace wrsr::env {

struct Info
{
    char wineVersion[64] = "";   // empty on real Windows
    char hostSystem[64]  = "";   // "Darwin" under CrossOver on macOS
    char hostRelease[64] = "";
    char exeVersion[32]  = "";   // SOVIET64.exe file version, e.g. 1.1.1.13
    DWORD exeTimeStamp   = 0;    // PE header TimeDateStamp: identifies the build
    DWORD exeSizeOfImage = 0;
};

Info Collect(HMODULE exe);
void LogAll(HMODULE exe, HMODULE self);

}  // namespace wrsr::env
