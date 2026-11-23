#include "research.h"

#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include <string>

#include "logger.h"

namespace wrsr::research {
namespace {

bool    g_enabled = false;
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

void Init(HMODULE self, bool enabled)
{
    g_enabled = enabled;
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
