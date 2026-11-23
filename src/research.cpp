#include "research.h"

#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include <string>

#include "logger.h"

namespace wrsr::research {
namespace {

bool    g_enabled = false;
bool    g_probe   = false;
volatile LONG g_probeStarted = 0;
char    g_pluginDir[MAX_PATH * 2] = "";
char    g_tempIni[MAX_PATH * 2]   = "";
wchar_t g_tempIniW[MAX_PATH * 2]  = L"";
char    g_png[MAX_PATH * 2]       = "";
wchar_t g_pngW[MAX_PATH * 2]      = L"";
SRWLOCK g_lock = SRWLOCK_INIT;
int     g_probeLines = 60;

const char kIdent[]  = "rail_priority";
const char kParent[] = "logistic_optimization";

const char kBlockAfter[] =
    "\r\n\r\n$RESEARCH rail_priority\r\n"
    "+logistic_optimization\r\n"
    "-----------------------------------------------\r\n\r\n"
    "$TYPE_SOVIET\r\n"
    "$COST 750\r\n\r\n"
    "$NAME 1957021\r\n"
    "$DESC 1957022\r\n\r\n"
    "$RESEARCH_ADD\r\n\r\n";

// No parent in this tree (a research mod without it): available from the start.
const char kBlockAlone[] =
    "\r\n\r\n$RESEARCH rail_priority\r\n"
    "-----------------------------------------------\r\n"
    "$AVAILABLE\r\n\r\n"
    "$TYPE_SOVIET\r\n"
    "$COST 750\r\n\r\n"
    "$NAME 1957021\r\n"
    "$DESC 1957022\r\n\r\n"
    "$RESEARCH_ADD\r\n\r\n";

bool EndsWithI(const char* s, const char* tail)
{
    size_t a = strlen(s), b = strlen(tail);
    return a >= b && _stricmp(s + a - b, tail) == 0;
}

void ToNarrow(const wchar_t* w, char* out, size_t cap)
{
    size_t i = 0;
    for (; w[i] && i + 1 < cap; ++i) out[i] = w[i] < 128 ? (char)w[i] : '?';
    out[i] = 0;
}

bool IsResearchIni(const char* p)
{
    return EndsWithI(p, "\\research\\research.ini") || EndsWithI(p, "/research/research.ini") ||
           EndsWithI(p, "\\research/research.ini") || EndsWithI(p, "/research\\research.ini");
}

bool IsOurPng(const char* p)
{
    return EndsWithI(p, "\\rail_priority.png") || EndsWithI(p, "/rail_priority.png");
}

void Probe(const char* p)
{
    if (g_probeLines <= 0) return;
    if (!strstr(p, "rail_priority") && !EndsWithI(p, "research.ini")) return;
    --g_probeLines;
    Log("research open \"%s\"", p);
}

bool ReadAll(const char* path, std::string& out)
{
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz;
    bool ok = GetFileSizeEx(h, &sz) && sz.QuadPart > 0 && sz.QuadPart < (64 << 20);
    if (ok)
    {
        out.resize((size_t)sz.QuadPart);
        DWORD got = 0;
        ok = ReadFile(h, &out[0], (DWORD)out.size(), &got, nullptr) && got == out.size();
    }
    CloseHandle(h);
    return ok;
}

// "$RESEARCH <name>" at a line start, followed by the end of the word.
size_t FindEntry(const std::string& s, const char* name)
{
    std::string key = std::string("$RESEARCH ") + name;
    for (size_t at = s.find(key); at != std::string::npos; at = s.find(key, at + 1))
    {
        bool lineStart = at == 0 || s[at - 1] == '\n';
        size_t end     = at + key.size();
        bool wordEnd   = end >= s.size() || s[end] == '\r' || s[end] == '\n' || s[end] == ' ' || s[end] == '\t';
        if (lineStart && wordEnd) return at;
    }
    return std::string::npos;
}

// Writes the extended copy; returns false (open the original) on any problem.
bool BuildCopy(const char* src)
{
    std::string s;
    if (!ReadAll(src, s))
    {
        Log("research cannot read \"%s\" - tree left as it is", src);
        return false;
    }
    if (FindEntry(s, kIdent) != std::string::npos)
    {
        Log("research \"%s\" already has %s - left as it is", src, kIdent);
        return false;
    }

    size_t parent = FindEntry(s, kParent);
    bool   linked = false;
    if (parent != std::string::npos)
    {
        // "$UNLOCK_RESEARCH rail_priority" into the parent, before its
        // "$RESEARCH_ADD" (at a line start; "---$RESEARCH_ADD" is commented out).
        for (size_t add = s.find("$RESEARCH_ADD", parent); add != std::string::npos;
             add = s.find("$RESEARCH_ADD", add + 1))
        {
            if (add > 0 && s[add - 1] != '\n') continue;
            s.insert(add, "$UNLOCK_RESEARCH rail_priority\r\n\r\n");
            linked = true;
            break;
        }
    }
    s += linked ? kBlockAfter : kBlockAlone;

    HANDLE h = CreateFileA(g_tempIni, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
    {
        Log("research cannot write \"%s\" - tree left as it is", g_tempIni);
        return false;
    }
    DWORD wrote = 0;
    bool ok = WriteFile(h, s.data(), (DWORD)s.size(), &wrote, nullptr) && wrote == s.size();
    CloseHandle(h);
    if (!ok) return false;
    Log("research added %s to \"%s\" (%s)", kIdent, src,
        linked ? "after logistic_optimization" : "no logistic_optimization in this tree: available from the start");
    return true;
}

// ---- memory probe (v1.2 research state): where do the research objects
// live, and which byte says "completed"? Runs once, 5 s after the game opened
// research.ini, on its own thread; reads through ReadProcessMemory, so a page
// freed meanwhile is skipped instead of crashing the game.

const char* const kProbeIdents[] = {"phone_tapping", "chemistry_init", "logistic_optimization",
                                    "distribution_office", "rail_priority"};

constexpr size_t kObj = 0xE8;   // one research object (1.1.1.13): char ident[64], ..., +0x58 name id

bool ReadMem(const BYTE* at, void* out, size_t n)
{
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), at, out, n, &got) && got == n;
}

// A research identifier: [a-z0-9_]+, zero-padded to 64 bytes.
bool ValidIdent(const BYTE* o, char* out)
{
    BYTE b[64];
    if (!ReadMem(o, b, 64)) return false;
    if (b[0] < 'a' || b[0] > 'z') return false;
    int n = 0;
    while (n < 64 && b[n]) { if (!((b[n] >= 'a' && b[n] <= 'z') || (b[n] >= '0' && b[n] <= '9') || b[n] == '_')) return false; ++n; }
    if (n >= 64) return false;
    for (int i = n; i < 64; ++i) if (b[i]) return false;
    memcpy(out, b, n + 1);
    return true;
}

void Hex(const BYTE* b, int n, char* out, size_t cap)
{
    size_t k = 0;
    for (int i = 0; i < n && k + 4 < cap; ++i) k += snprintf(out + k, cap - k, "%02X ", b[i]);
}

// A research object: valid identifier at +0, and its two text ids next to
// each other at +0x58 / +0x5C (name, description = name + 1).
bool LooksLikeResearch(const BYTE* chunk, SIZE_T got, SIZE_T i)
{
    if (i + kObj > got) return false;
    const BYTE* o = chunk + i;
    if (o[0] < 'a' || o[0] > 'z') return false;
    int n = 0;
    while (n < 64 && o[n])
    {
        BYTE c = o[n];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
        ++n;
    }
    if (n < 3 || n >= 64) return false;
    for (int k = n; k < 64; ++k) if (o[k]) return false;
    int name, desc;
    memcpy(&name, o + 0x58, 4);
    memcpy(&desc, o + 0x5C, 4);
    return name >= 1000 && name < 3000000 && desc == name + 1;
}

DWORD WINAPI ProbeThread(void*)
{
    Sleep(5000);
    Log("probe research objects: scanning for identifier + name/desc ids");
    const SIZE_T kChunk = 1 << 20;
    BYTE* chunk = (BYTE*)VirtualAlloc(nullptr, kChunk + kObj, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!chunk) return 0;
    int n = 0, full = 0;
    MEMORY_BASIC_INFORMATION mi;
    for (BYTE* p = nullptr; n < 600 && VirtualQuery(p, &mi, sizeof(mi)) == sizeof(mi);
         p = (BYTE*)mi.BaseAddress + mi.RegionSize)
    {
        if (mi.State != MEM_COMMIT || mi.Type != MEM_PRIVATE || mi.Protect != PAGE_READWRITE) continue;
        BYTE* base = (BYTE*)mi.BaseAddress;
        for (SIZE_T off = 0; n < 600 && off < mi.RegionSize; off += kChunk)
        {
            SIZE_T want = mi.RegionSize - off < kChunk ? mi.RegionSize - off : kChunk;
            SIZE_T got = 0;
            if (base + off == chunk || !ReadProcessMemory(GetCurrentProcess(), base + off, chunk, want, &got)) continue;
            for (SIZE_T i = 0; i + kObj <= got && n < 600; i += 8)   // objects are 8-aligned
            {
                if (!LooksLikeResearch(chunk, got, i)) continue;
                const BYTE* b = chunk + i;
                char h1[200], h2[200];
                Hex(b + 0x40, 0x20, h1, sizeof(h1));
                Hex(b + 0xB8, 0x30, h2, sizeof(h2));
                Log("probe #%03d %p %-30s +40: %s| +B8: %s", n, (const void*)(base + off + i), (const char*)b, h1, h2);
                ++n;
                bool target = false;
                for (const char* t : kProbeIdents) if (strcmp(t, (const char*)b) == 0) target = true;
                if (!target || full >= 12) continue;
                ++full;
                Log("probe full %s:", (const char*)b);
                for (int row = 0; row < (int)kObj; row += 32)
                {
                    char line[200];
                    Hex(b + row, row + 32 <= (int)kObj ? 32 : (int)kObj - row, line, sizeof(line));
                    Log("probe   +%03X %s", row, line);
                }
            }
        }
    }
    VirtualFree(chunk, 0, MEM_RELEASE);
    Log("probe done: %d research objects", n);
    return 0;
}

const char* Map(const char* p, bool* changed)
{
    *changed = false;
    if (!g_enabled || !p) return p;
    Probe(p);
    if (IsResearchIni(p))
    {
        AcquireSRWLockExclusive(&g_lock);
        bool ok = BuildCopy(p);
        ReleaseSRWLockExclusive(&g_lock);
        if (ok && g_probe && InterlockedExchange(&g_probeStarted, 1) == 0)
        {
            HANDLE th = CreateThread(nullptr, 0, ProbeThread, nullptr, 0, nullptr);
            if (th) CloseHandle(th);
        }
        if (ok) { *changed = true; return g_tempIni; }
    }
    else if (IsOurPng(p) && g_png[0] && GetFileAttributesA(g_png) != INVALID_FILE_ATTRIBUTES)
    {
        static bool logged = false;
        if (!logged) { logged = true; Log("research picture -> \"%s\"", g_png); }
        *changed = true;
        return g_png;
    }
    return p;
}

// ---- texts (English fallback)

struct Texts { const char* token; const wchar_t* name; const wchar_t* desc; };
const Texts kTexts[] = {
    {"english", L"Rail priority",
     L"Dispatchers give passenger trains priority at signals: a waiting freight train lets the passenger "
     L"train go first. Unlocks the Rail Priority switches in the Scripts menu."},
    {"hungarian", L"Vasúti elsőbbség",
     L"A forgalmisták elsőbbséget adnak a személyvonatoknak a jelzőknél: a várakozó tehervonat előreengedi "
     L"a személyvonatot. Feloldja a Rail Priority kapcsolóit a Szkriptek menüben."},
};
wchar_t g_text[2][512];

}  // namespace

void Init(HMODULE self, bool enabled, bool probe)
{
    g_enabled = enabled;
    g_probe   = probe;
    if (!enabled) { Log("research off in the ini"); return; }

    DWORD n = GetModuleFileNameA(self, g_pluginDir, sizeof(g_pluginDir));
    char* slash = n ? strrchr(g_pluginDir, '\\') : nullptr;
    if (slash) *slash = 0;
    snprintf(g_png, sizeof(g_png), "%s\\rail_priority.png", g_pluginDir);
    if (GetFileAttributesA(g_png) == INVALID_FILE_ATTRIBUTES) Log("research no picture at \"%s\"", g_png);

    char tmp[MAX_PATH * 2];
    DWORD t = GetTempPathA(sizeof(tmp), tmp);
    if (!t || t >= sizeof(tmp)) snprintf(tmp, sizeof(tmp), "%s\\", g_pluginDir);
    snprintf(g_tempIni, sizeof(g_tempIni), "%swrsr_rail_research.ini", tmp);
    MultiByteToWideChar(CP_ACP, 0, g_tempIni, -1, g_tempIniW, MAX_PATH * 2);
    MultiByteToWideChar(CP_ACP, 0, g_png, -1, g_pngW, MAX_PATH * 2);
    Log("research ready: %s goes into the next research.ini the game opens", kIdent);
}

const char* MapA(const char* path)
{
    bool changed;
    return Map(path, &changed);
}

const wchar_t* MapW(const wchar_t* path)
{
    if (!g_enabled || !path) return path;
    char narrow[MAX_PATH * 2];
    ToNarrow(path, narrow, sizeof(narrow));
    bool changed;
    const char* to = Map(narrow, &changed);
    if (!changed) return path;
    return to == g_tempIni ? g_tempIniW : g_pngW;
}

wchar_t* Text(int id, const char* langToken)
{
    if (id != ID_NAME && id != ID_DESC) return nullptr;
    const Texts* pick = &kTexts[0];
    for (const Texts& t : kTexts)
        if (langToken && strcmp(t.token, langToken) == 0) pick = &t;
    int i = id == ID_NAME ? 0 : 1;
    wcsncpy(g_text[i], i == 0 ? pick->name : pick->desc, 511);
    g_text[i][511] = 0;
    return g_text[i];
}

}  // namespace wrsr::research
