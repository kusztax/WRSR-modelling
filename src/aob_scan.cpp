#include "aob_scan.h"

#include <cstring>

#include "logger.h"

namespace wrsr::aob {

bool FindSection(HMODULE module, const char* name, Section& out)
{
    out = Section{};
    if (!module || !name) return false;

    BYTE* base = (BYTE*)module;
    auto* dos  = (IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto* nt = (IMAGE_NT_HEADERS64*)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    if (nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) return false;

    IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    const size_t nameLen = strlen(name);
    if (nameLen > IMAGE_SIZEOF_SHORT_NAME) return false;

    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i)
    {
        if (memcmp(sec[i].Name, name, nameLen) == 0 &&
            (nameLen == IMAGE_SIZEOF_SHORT_NAME || sec[i].Name[nameLen] == 0))
        {
            out.start = base + sec[i].VirtualAddress;
            out.size  = sec[i].Misc.VirtualSize;
            return true;
        }
    }
    return false;
}

Hit ScanModule(HMODULE module, const char* section, const char* pattern, const char* label)
{
    Hit hit;

    Pattern p;
    if (!Parse(pattern, p))
    {
        Log("aob   %-28s malformed pattern \"%s\"", label, pattern);
        return hit;
    }

    Section s;
    if (!FindSection(module, section, s))
    {
        Log("aob   %-28s section %s not found", label, section);
        return hit;
    }

    Match m   = FindUnique(s.start, s.size, p);
    hit.result = m.result;
    hit.count  = m.count;
    if (m.result == Result::Unique)
    {
        hit.addr = s.start + m.offset;
        Log("aob   %-28s unique at +0x%08llX", label,
            (unsigned long long)(hit.addr - (BYTE*)module));
    }
    else
    {
        Log("aob   %-28s %s (%zu hit%s) - feature must stay off", label,
            ToString(m.result), m.count, m.count == 1 ? "" : "s");
    }
    return hit;
}

}  // namespace wrsr::aob
