#include "lang.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "logger.h"
#include "tesmio_api.h"

namespace wrsr::lang {
namespace {

// ---------------------------------------------------------------- texts
// Matched against the lower-cased <Language> of soviet<Language>.btf, in this
// order (so "brazil" wins over "portug", "tchinese" over "chinese").

struct Texts
{
    const char*    token;
    const wchar_t* title;
    const wchar_t* on;
    const wchar_t* off;
};

const Texts kTexts[] = {
    {"english", L"Rail priority",
     L"ON: passenger trains go first at signals.",
     L"OFF: the game decides who goes first at signals."},
    {"hungarian", L"Vasúti elsőbbség",
     L"BE: a személyvonatok elsőbbséget kapnak a jelzőknél.",
     L"KI: a jelzőknél a játék dönti el a sorrendet."},
    {"german", L"Bahnvorrang",
     L"EIN: Personenzüge haben an Signalen Vorrang.",
     L"AUS: An Signalen entscheidet das Spiel die Reihenfolge."},
    {"czech", L"Přednost na železnici",
     L"ZAP: osobní vlaky mají u návěstidel přednost.",
     L"VYP: o pořadí u návěstidel rozhoduje hra."},
    {"slovak", L"Prednosť na železnici",
     L"ZAP: osobné vlaky majú pri návestidlách prednosť.",
     L"VYP: o poradí pri návestidlách rozhoduje hra."},
    {"polish", L"Pierwszeństwo kolejowe",
     L"WŁ.: pociągi pasażerskie mają pierwszeństwo na semaforach.",
     L"WYŁ.: o kolejności na semaforach decyduje gra."},
    {"russian", L"Приоритет на железной дороге",
     L"ВКЛ: пассажирские поезда имеют приоритет на светофорах.",
     L"ВЫКЛ: порядок на светофорах определяет игра."},
    {"ukrain", L"Пріоритет на залізниці",
     L"УВІМК: пасажирські поїзди мають пріоритет на світлофорах.",
     L"ВИМК: порядок на світлофорах визначає гра."},
    {"bulgar", L"Железопътен приоритет",
     L"ВКЛ.: пътническите влакове минават първи на сигналите.",
     L"ИЗКЛ.: редът на сигналите се определя от играта."},
    {"french", L"Priorité ferroviaire",
     L"ACTIVÉE : les trains de voyageurs passent en premier aux signaux.",
     L"DÉSACTIVÉE : le jeu décide de l'ordre aux signaux."},
    {"spanish", L"Prioridad ferroviaria",
     L"ACTIVADA: los trenes de pasajeros pasan primero en las señales.",
     L"DESACTIVADA: el juego decide el orden en las señales."},
    {"italian", L"Precedenza ferroviaria",
     L"ATTIVA: i treni passeggeri passano per primi ai segnali.",
     L"DISATTIVA: l'ordine ai segnali lo decide il gioco."},
    {"brazil", L"Prioridade ferroviária",
     L"LIGADA: os trens de passageiros passam primeiro nos sinais.",
     L"DESLIGADA: o jogo decide a ordem nos sinais."},
    {"portug", L"Prioridade ferroviária",
     L"LIGADA: os comboios de passageiros passam primeiro nos sinais.",
     L"DESLIGADA: o jogo decide a ordem nos sinais."},
    {"dutch", L"Spoorvoorrang",
     L"AAN: reizigerstreinen gaan bij seinen voor.",
     L"UIT: het spel bepaalt de volgorde bij seinen."},
    {"swed", L"Järnvägsprioritet",
     L"PÅ: persontåg går först vid signaler.",
     L"AV: spelet avgör ordningen vid signaler."},
    {"finn", L"Rautatien etuajo-oikeus",
     L"PÄÄLLÄ: matkustajajunat menevät opastimilla ensin.",
     L"POIS: peli päättää järjestyksen opastimilla."},
    {"turk", L"Demiryolu önceliği",
     L"AÇIK: yolcu trenleri sinyallerde önce geçer.",
     L"KAPALI: sinyallerdeki sırayı oyun belirler."},
    {"romanian", L"Prioritate feroviară",
     L"PORNIT: trenurile de călători trec primele la semnale.",
     L"OPRIT: ordinea la semnale o decide jocul."},
    {"greek", L"Σιδηροδρομική προτεραιότητα",
     L"ΕΝΕΡΓΟ: τα επιβατικά τρένα περνούν πρώτα στα σήματα.",
     L"ΑΝΕΝΕΡΓΟ: τη σειρά στα σήματα την αποφασίζει το παιχνίδι."},
    {"tchinese", L"鐵路優先權",
     L"開：客運列車在號誌處優先通過。",
     L"關：號誌處的通行順序由遊戲決定。"},
    {"traditional", L"鐵路優先權",
     L"開：客運列車在號誌處優先通過。",
     L"關：號誌處的通行順序由遊戲決定。"},
    {"chinese", L"铁路优先权",
     L"开：客运列车在信号机处优先通过。",
     L"关：信号机处的通行顺序由游戏决定。"},
    {"japan", L"鉄道の優先通行",
     L"オン：信号で旅客列車が優先されます。",
     L"オフ：信号での順番はゲームが決めます。"},
    {"korean", L"철도 우선권",
     L"켜짐: 신호에서 여객 열차가 먼저 통과합니다.",
     L"꺼짐: 신호에서의 순서는 게임이 정합니다."},
};

const Texts* volatile g_current = &kTexts[0];

// GetString returns a non-const pointer, so hand out our own writable copies.
wchar_t g_title[128], g_on[256], g_off[256];

void Apply(const Texts* t)
{
    wcsncpy(g_title, t->title, 127);
    wcsncpy(g_on, t->on, 255);
    wcsncpy(g_off, t->off, 255);
    g_current = t;
}

// "...\sovietHungarian.btf" -> pick the table entry; ignore sovietComments.btf.
void NoteFile(const char* path)
{
    if (!path) return;
    const char* slash = strrchr(path, '\\');
    const char* fwd   = strrchr(path, '/');
    const char* name  = slash > fwd ? slash + 1 : (fwd ? fwd + 1 : path);
    if (_strnicmp(name, "soviet", 6) != 0) return;
    const char* dot = strrchr(name, '.');
    if (!dot || _stricmp(dot, ".btf") != 0) return;

    char lang[64];
    size_t n = (size_t)(dot - (name + 6));
    if (n == 0 || n >= sizeof(lang)) return;
    for (size_t i = 0; i < n; ++i) lang[i] = (char)tolower((unsigned char)name[6 + i]);
    lang[n] = 0;
    if (strcmp(lang, "comments") == 0) return;

    const Texts* pick = &kTexts[0];
    bool known = false;
    for (const Texts& t : kTexts)
        if (strstr(lang, t.token)) { pick = &t; known = true; break; }

    // The game opens the chosen language and then sovietEnglish.btf as the
    // fallback for missing texts: English right after another language is
    // that fallback, not a choice.
    static ULONGLONG lastOther = 0;
    ULONGLONG now = GetTickCount64();
    bool english = strcmp(pick->token, "english") == 0 && known;
    if (!english) lastOther = now;
    else if (lastOther && now - lastOther < 3000) return;

    if (pick != g_current || !known)
    {
        Apply(pick);
        Log("lang  game language file \"%s\" -> %s%s", name, pick->token, known ? "" : " (not translated: English)");
    }
}

void NoteFileW(const wchar_t* path)
{
    if (!path) return;
    char buf[MAX_PATH * 2];
    size_t i = 0;
    for (; path[i] && i + 1 < sizeof(buf); ++i) buf[i] = path[i] < 128 ? (char)path[i] : '?';
    buf[i] = 0;
    NoteFile(buf);
}

// ---------------------------------------------------------------- hooks

using GetStringFn = wchar_t* (*)(void*, int);
using FopenFn     = FILE* (*)(const char*, const char*);
using WfopenFn    = FILE* (*)(const wchar_t*, const wchar_t*);
using FopenSFn    = errno_t (*)(FILE**, const char*, const char*);
using WfopenSFn   = errno_t (*)(FILE**, const wchar_t*, const wchar_t*);
using ReadFileFn  = int (*)(const char*, char**, unsigned*, bool);

GetStringFn o_getString;
FopenFn     o_fopen[2];
WfopenFn    o_wfopen[2];
FopenSFn    o_fopen_s[2];
WfopenSFn   o_wfopen_s[2];
ReadFileFn  o_readFile;

wchar_t* h_getString(void* self, int id)
{
    switch (id)
    {
    case ID_TITLE:    return g_title;
    case ID_TEXT_ON:  return g_on;
    case ID_TEXT_OFF: return g_off;
    }
    return o_getString(self, id);
}

template <int M> FILE* h_fopen(const char* p, const char* m) { NoteFile(p); return o_fopen[M](p, m); }
template <int M> FILE* h_wfopen(const wchar_t* p, const wchar_t* m) { NoteFileW(p); return o_wfopen[M](p, m); }
template <int M> errno_t h_fopen_s(FILE** f, const char* p, const char* m) { NoteFile(p); return o_fopen_s[M](f, p, m); }
template <int M> errno_t h_wfopen_s(FILE** f, const wchar_t* p, const wchar_t* m) { NoteFileW(p); return o_wfopen_s[M](f, p, m); }
int h_readFile(const char* p, char** out, unsigned* size, bool b) { NoteFile(p); return o_readFile(p, out, size, b); }

const char kEngine[] = "C3DDLL64.dll";
const char kStdio[]  = "api-ms-win-crt-stdio-l1-1-0.dll";

// Ask for a hook only on an import the module really has: a request for a
// missing one is harmless, but Republic Mod Loader then marks the whole
// plugin DEGRADED in its window.
int Patch(const TsmHost* h, HMODULE mod, const char* dll, const char* fn, void* detour, void** orig,
          const char* label)
{
    if (!h->findIatSlot(mod, dll, fn)) return 0;
    return h->patchIat(mod, dll, fn, detour, orig, label) != 0;
}

template <int M>
int WatchOpens(const TsmHost* h, HMODULE mod, const char* who)
{
    int n = 0;
    char label[64];
    snprintf(label, sizeof(label), "lang fopen (%s)", who);
    n += Patch(h, mod, kStdio, "fopen", (void*)&h_fopen<M>, (void**)&o_fopen[M], label);
    snprintf(label, sizeof(label), "lang _wfopen (%s)", who);
    n += Patch(h, mod, kStdio, "_wfopen", (void*)&h_wfopen<M>, (void**)&o_wfopen[M], label);
    snprintf(label, sizeof(label), "lang fopen_s (%s)", who);
    n += Patch(h, mod, kStdio, "fopen_s", (void*)&h_fopen_s<M>, (void**)&o_fopen_s[M], label);
    snprintf(label, sizeof(label), "lang _wfopen_s (%s)", who);
    n += Patch(h, mod, kStdio, "_wfopen_s", (void*)&h_wfopen_s<M>, (void**)&o_wfopen_s[M], label);
    return n;
}

}  // namespace

bool Init(const TsmHost* host, HMODULE exe, HMODULE engine)
{
    Apply(&kTexts[0]);

    if (!host->patchIat(exe, kEngine, "?GetString@C3D_LANGUAGE@@QEAAPEA_WH@Z", (void*)&h_getString,
                        (void**)&o_getString, "lang GetString"))
    {
        Log("lang  GetString import not found - notifications will show no text");
        return false;
    }

    int watched = WatchOpens<0>(host, exe, "exe");
    if (engine) watched += WatchOpens<1>(host, engine, "engine");
    watched += Patch(host, exe, kEngine, "?C3DHelp_ReadFileIntoBuffer@@YAHPEBDPEAPEADPEAI_N@Z",
                     (void*)&h_readFile, (void**)&o_readFile, "lang ReadFileIntoBuffer");
    Log("lang  texts ready (English until the game opens its language file); %d file hooks", watched);
    return true;
}

}  // namespace wrsr::lang
