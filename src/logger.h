// logger.h - our own log file, independent of any loader.
//
// Under CrossOver there is usually no visible console, and the loader's log
// lives somewhere else per loader (tesmioloader.log vs rml/logs/...). So the
// plugin always writes `wrsr_rail_mod.log` next to SOVIET64.exe (the game
// folder), falling back to the DLL's folder and then %TEMP% when that is not
// writable. Every line is written straight through with WriteFile, so it
// survives a game crash. When a loader host is bound, lines are mirrored into
// the loader's log too, so one bug report contains both.
#pragma once

#include <stdarg.h>
#include <stdio.h>  // __MINGW_PRINTF_FORMAT

#ifndef __MINGW_PRINTF_FORMAT
#define __MINGW_PRINTF_FORMAT printf
#endif

namespace wrsr {

using HostLogFn = void (*)(const char* fmt, ...);

namespace log {

// Safe to call from DllMain (kernel32 only). Idempotent.
bool Open(void* selfModule);
void Close();

// Mirror every line into the loader's log as "wrsr_rail  <msg>".
void SetHostSink(HostLogFn fn);

// Full path of the log file actually opened, or L"" when none could be.
const wchar_t* Path();

}  // namespace log

// printf-style. Adds a timestamp and the thread id; no trailing \n needed.
void Log(const char* fmt, ...) __attribute__((format(__MINGW_PRINTF_FORMAT, 1, 2)));
void LogV(const char* fmt, va_list ap);

}  // namespace wrsr
