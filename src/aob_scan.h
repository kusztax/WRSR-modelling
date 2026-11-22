// aob_scan.h - pattern scanning over a loaded PE image (Windows side).
#pragma once

#include <windows.h>
#include <cstdint>

#include "aob_pattern.h"

namespace wrsr::aob {

struct Section
{
    BYTE*  start = nullptr;
    SIZE_T size  = 0;
};

// Finds a section (".text", ".rdata", ...) of a module mapped in this process.
bool FindSection(HMODULE module, const char* name, Section& out);

struct Hit
{
    Result result = Result::NotFound;
    BYTE*  addr   = nullptr;   // valid when result == Unique
    size_t count  = 0;
};

// Parses `pattern`, scans `section` of `module`, and logs the outcome with
// `label` as "+RVA". Never writes anything.
Hit ScanModule(HMODULE module, const char* section, const char* pattern, const char* label);

// Target of a rip-relative operand: `disp32At` is the address of the 4-byte
// displacement, `insnEnd` the address of the next instruction.
inline BYTE* RipTarget(BYTE* disp32At, BYTE* insnEnd)
{
    int32_t d;
    memcpy(&d, disp32At, sizeof(d));
    return insnEnd + d;
}

}  // namespace wrsr::aob
