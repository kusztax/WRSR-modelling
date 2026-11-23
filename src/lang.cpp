#include "lang.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "logger.h"
#include "research.h"
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
    const wchar_t* hold;   // %d = seconds
    const wchar_t* done;   // research completed, priority switched on
    const wchar_t* notyet; // ON pressed before the research
    const wchar_t* rname;  // the research's name
    const wchar_t* rdesc;  // and description
};

const Texts kTexts[] = {
    {"english", L"Rail priority",
     L"ON: passenger trains go first at signals.",
     L"OFF: the game decides who goes first at signals.",
     L"Maximum wait: %d s. A freight train waits at most this long for passenger trains, then it goes first.",
     L"Research complete: passenger trains now go first at signals.",
     L"Not researched yet. Research Rail priority at the party university; it switches on by itself when the research is done.",
     L"Rail priority",
     L"Dispatchers give passenger trains priority at signals: a waiting freight train lets the passenger train go first. Switch it on with the ON button in Options - Scripts."},
    {"hungarian", L"Vasúti elsőbbség",
     L"BE: a személyvonatok elsőbbséget kapnak a jelzőknél.",
     L"KI: a jelzőknél a játék dönti el a sorrendet.",
     L"Maximális várakozás: %d mp. A tehervonat legfeljebb ennyit vár a személyvonatokra, utána ő megy elsőként.",
     L"A kutatás kész: mostantól a személyvonatok mennek elsőként a jelzőknél.",
     L"Még nem kutattad ki. Kutasd ki a Vasúti elsőbbséget a pártegyetemen; ha kész, magától bekapcsol.",
     L"Vasúti elsőbbség",
     L"A forgalmisták elsőbbséget adnak a személyvonatoknak a jelzőknél: a várakozó tehervonat előreengedi a személyvonatot. Bekapcsolás: Beállítások - Szkriptek, BE gomb."},
    {"german", L"Bahnvorrang",
     L"EIN: Personenzüge haben an Signalen Vorrang.",
     L"AUS: An Signalen entscheidet das Spiel die Reihenfolge.",
     L"Maximale Wartezeit: %d s. Ein Güterzug wartet höchstens so lange auf Personenzüge, dann fährt er zuerst.",
     L"Forschung abgeschlossen: Personenzüge fahren jetzt an Signalen zuerst.",
     L"Noch nicht erforscht. Erforsche Bahnvorrang an der Parteihochschule; danach schaltet er sich von selbst ein.",
     L"Bahnvorrang",
     L"Fahrdienstleiter geben Personenzügen an Signalen Vorrang: ein wartender Güterzug lässt den Personenzug zuerst fahren. Einschalten mit der EIN-Taste unter Optionen - Skripte."},
    {"czech", L"Přednost na železnici",
     L"ZAP: osobní vlaky mají u návěstidel přednost.",
     L"VYP: o pořadí u návěstidel rozhoduje hra.",
     L"Maximální čekání: %d s. Nákladní vlak čeká na osobní vlaky nejvýše tak dlouho, pak jede první.",
     L"Výzkum dokončen: osobní vlaky nyní jedou na návěstidlech první.",
     L"Ještě nevyzkoumáno. Vyzkoumej Přednost na železnici na stranické univerzitě; po dokončení se zapne sama.",
     L"Přednost na železnici",
     L"Výpravčí dávají osobním vlakům přednost na návěstidlech: čekající nákladní vlak pustí osobní vlak napřed. Zapnutí tlačítkem ZAP v Možnosti - Skripty."},
    {"slovak", L"Prednosť na železnici",
     L"ZAP: osobné vlaky majú pri návestidlách prednosť.",
     L"VYP: o poradí pri návestidlách rozhoduje hra.",
     L"Maximálne čakanie: %d s. Nákladný vlak čaká na osobné vlaky najviac tak dlho, potom ide prvý.",
     L"Výskum dokončený: osobné vlaky teraz idú na návestidlách prvé.",
     L"Ešte nevyskúmané. Vyskúmaj Prednosť na železnici na straníckej univerzite; po dokončení sa zapne sama.",
     L"Prednosť na železnici",
     L"Výpravcovia dávajú osobným vlakom prednosť na návestidlách: čakajúci nákladný vlak pustí osobný vlak dopredu. Zapnutie tlačidlom ZAP v Možnosti - Skripty."},
    {"polish", L"Pierwszeństwo kolejowe",
     L"WŁ.: pociągi pasażerskie mają pierwszeństwo na semaforach.",
     L"WYŁ.: o kolejności na semaforach decyduje gra.",
     L"Maksymalne oczekiwanie: %d s. Pociąg towarowy czeka na pociągi pasażerskie najwyżej tyle, potem jedzie pierwszy.",
     L"Badanie ukończone: pociągi pasażerskie jadą teraz pierwsze na semaforach.",
     L"Jeszcze nie zbadano. Zbadaj Pierwszeństwo kolejowe na uczelni partyjnej; po ukończeniu włączy się samo.",
     L"Pierwszeństwo kolejowe",
     L"Dyżurni ruchu dają pociągom pasażerskim pierwszeństwo na semaforach: czekający pociąg towarowy przepuszcza pasażerski. Włącz przyciskiem WŁ. w Opcje - Skrypty."},
    {"russian", L"Приоритет на железной дороге",
     L"ВКЛ: пассажирские поезда имеют приоритет на светофорах.",
     L"ВЫКЛ: порядок на светофорах определяет игра.",
     L"Максимальное ожидание: %d с. Грузовой поезд ждёт пассажирские поезда не дольше этого, затем едет первым.",
     L"Исследование завершено: пассажирские поезда теперь проходят сигналы первыми.",
     L"Ещё не исследовано. Исследуйте «Приоритет на железной дороге» в партийном университете; после завершения он включится сам.",
     L"Приоритет на железной дороге",
     L"Диспетчеры дают пассажирским поездам приоритет у сигналов: ожидающий грузовой поезд пропускает пассажирский. Включение: кнопка ВКЛ в Настройки - Скрипты."},
    {"ukrain", L"Пріоритет на залізниці",
     L"УВІМК: пасажирські поїзди мають пріоритет на світлофорах.",
     L"ВИМК: порядок на світлофорах визначає гра.",
     L"Максимальне очікування: %d с. Вантажний поїзд чекає на пасажирські поїзди не довше цього, потім їде першим.",
     L"Дослідження завершено: пасажирські поїзди тепер проходять сигнали першими.",
     L"Ще не досліджено. Дослідіть «Пріоритет на залізниці» в партійному університеті; після завершення він увімкнеться сам.",
     L"Пріоритет на залізниці",
     L"Диспетчери дають пасажирським поїздам пріоритет біля сигналів: вантажний поїзд, що чекає, пропускає пасажирський. Увімкнення: кнопка УВІМК у Налаштування - Скрипти."},
    {"bulgar", L"Железопътен приоритет",
     L"ВКЛ.: пътническите влакове минават първи на сигналите.",
     L"ИЗКЛ.: редът на сигналите се определя от играта.",
     L"Максимално чакане: %d с. Товарният влак чака пътническите влакове най-много толкова, после тръгва пръв.",
     L"Проучването е завършено: пътническите влакове вече минават първи на сигналите.",
     L"Още не е проучено. Проучи „Железопътен приоритет“ в партийния университет; след това се включва само.",
     L"Железопътен приоритет",
     L"Диспечерите дават приоритет на пътническите влакове на сигналите: чакащият товарен влак пропуска пътническия. Включване с бутона ВКЛ в Настройки - Скриптове."},
    {"french", L"Priorité ferroviaire",
     L"ACTIVÉE : les trains de voyageurs passent en premier aux signaux.",
     L"DÉSACTIVÉE : le jeu décide de l'ordre aux signaux.",
     L"Attente maximale : %d s. Un train de marchandises attend les trains de voyageurs au plus ce temps, puis il passe en premier.",
     L"Recherche terminée : les trains de voyageurs passent désormais en premier aux signaux.",
     L"Pas encore recherché. Recherchez Priorité ferroviaire à l'université du parti ; elle s'activera d'elle-même une fois la recherche terminée.",
     L"Priorité ferroviaire",
     L"Les régulateurs donnent la priorité aux trains de voyageurs aux signaux : un train de marchandises en attente laisse passer le train de voyageurs. Activez-la avec le bouton ACTIVÉ dans Options - Scripts."},
    {"spanish", L"Prioridad ferroviaria",
     L"ACTIVADA: los trenes de pasajeros pasan primero en las señales.",
     L"DESACTIVADA: el juego decide el orden en las señales.",
     L"Espera máxima: %d s. Un tren de mercancías espera a los trenes de pasajeros como máximo este tiempo y luego pasa primero.",
     L"Investigación completada: los trenes de pasajeros ahora pasan primero en las señales.",
     L"Aún no investigado. Investiga Prioridad ferroviaria en la universidad del partido; se activará sola al terminar.",
     L"Prioridad ferroviaria",
     L"Los jefes de circulación dan prioridad a los trenes de pasajeros en las señales: un tren de mercancías en espera deja pasar al de pasajeros. Actívala con el botón ACTIVADO en Opciones - Scripts."},
    {"italian", L"Precedenza ferroviaria",
     L"ATTIVA: i treni passeggeri passano per primi ai segnali.",
     L"DISATTIVA: l'ordine ai segnali lo decide il gioco.",
     L"Attesa massima: %d s. Un treno merci aspetta i treni passeggeri al massimo questo tempo, poi passa per primo.",
     L"Ricerca completata: ora i treni passeggeri passano per primi ai segnali.",
     L"Non ancora ricercato. Ricerca Priorità ferroviaria all'università del partito; si attiverà da sola a ricerca completata.",
     L"Priorità ferroviaria",
     L"I dirigenti del movimento danno priorità ai treni passeggeri ai segnali: un treno merci in attesa lascia passare il treno passeggeri. Attivala con il pulsante ATTIVO in Opzioni - Script."},
    {"brazil", L"Prioridade ferroviária",
     L"LIGADA: os trens de passageiros passam primeiro nos sinais.",
     L"DESLIGADA: o jogo decide a ordem nos sinais.",
     L"Espera máxima: %d s. Um trem de carga espera os trens de passageiros no máximo esse tempo e depois passa primeiro.",
     L"Pesquisa concluída: agora os trens de passageiros passam primeiro nos sinais.",
     L"Ainda não pesquisado. Pesquise Prioridade ferroviária na universidade do partido; ela liga sozinha quando a pesquisa terminar.",
     L"Prioridade ferroviária",
     L"Os controladores dão prioridade aos trens de passageiros nos sinais: um trem de carga parado deixa o de passageiros passar primeiro. Ligue com o botão LIGADO em Opções - Scripts."},
    {"portug", L"Prioridade ferroviária",
     L"LIGADA: os comboios de passageiros passam primeiro nos sinais.",
     L"DESLIGADA: o jogo decide a ordem nos sinais.",
     L"Espera máxima: %d s. Um comboio de mercadorias espera pelos comboios de passageiros no máximo este tempo e depois passa primeiro.",
     L"Investigação concluída: os comboios de passageiros passam agora primeiro nos sinais.",
     L"Ainda não investigado. Investigue Prioridade ferroviária na universidade do partido; liga-se sozinha quando a investigação terminar.",
     L"Prioridade ferroviária",
     L"Os controladores dão prioridade aos comboios de passageiros nos sinais: um comboio de mercadorias parado deixa passar o de passageiros. Ligue com o botão LIGADO em Opções - Scripts."},
    {"dutch", L"Spoorvoorrang",
     L"AAN: reizigerstreinen gaan bij seinen voor.",
     L"UIT: het spel bepaalt de volgorde bij seinen.",
     L"Maximale wachttijd: %d s. Een goederentrein wacht hoogstens zo lang op reizigerstreinen en gaat dan als eerste.",
     L"Onderzoek voltooid: reizigerstreinen gaan nu als eerste bij seinen.",
     L"Nog niet onderzocht. Onderzoek Spoorvoorrang aan de partijuniversiteit; daarna gaat het vanzelf aan.",
     L"Spoorvoorrang",
     L"Treindienstleiders geven reizigerstreinen voorrang bij seinen: een wachtende goederentrein laat de reizigerstrein eerst gaan. Zet het aan met de AAN-knop in Opties - Scripts."},
    {"swed", L"Järnvägsprioritet",
     L"PÅ: persontåg går först vid signaler.",
     L"AV: spelet avgör ordningen vid signaler.",
     L"Längsta väntetid: %d s. Ett godståg väntar på persontåg högst så länge och kör sedan först.",
     L"Forskningen klar: persontåg kör nu först vid signaler.",
     L"Ännu inte utforskat. Forska fram Järnvägsprioritet på partiuniversitetet; den slås på av sig själv när forskningen är klar.",
     L"Järnvägsprioritet",
     L"Tågklarerare ger persontåg företräde vid signaler: ett väntande godståg släpper förbi persontåget. Slå på med PÅ-knappen under Alternativ - Skript."},
    {"finn", L"Rautatien etuajo-oikeus",
     L"PÄÄLLÄ: matkustajajunat menevät opastimilla ensin.",
     L"POIS: peli päättää järjestyksen opastimilla.",
     L"Enimmäisodotus: %d s. Tavarajuna odottaa matkustajajunia enintään näin kauan ja lähtee sitten ensimmäisenä.",
     L"Tutkimus valmis: matkustajajunat menevät nyt opastimilla ensin.",
     L"Ei vielä tutkittu. Tutki Rautatieetuoikeus puoluekorkeakoulussa; se kytkeytyy päälle itsestään, kun tutkimus valmistuu.",
     L"Rautatieetuoikeus",
     L"Liikenteenohjaajat antavat matkustajajunille etuoikeuden opastimilla: odottava tavarajuna päästää matkustajajunan edelle. Kytke päälle PÄÄLLÄ-painikkeella kohdassa Asetukset - Skriptit."},
    {"turk", L"Demiryolu önceliği",
     L"AÇIK: yolcu trenleri sinyallerde önce geçer.",
     L"KAPALI: sinyallerdeki sırayı oyun belirler.",
     L"En uzun bekleme: %d sn. Yük treni yolcu trenlerini en fazla bu kadar bekler, sonra önce o geçer.",
     L"Araştırma tamamlandı: yolcu trenleri artık sinyallerde önce geçiyor.",
     L"Henüz araştırılmadı. Parti üniversitesinde Demiryolu önceliğini araştır; araştırma bitince kendiliğinden açılır.",
     L"Demiryolu önceliği",
     L"Hareket memurları sinyallerde yolcu trenlerine öncelik verir: bekleyen yük treni yolcu trenini önce geçirir. Seçenekler - Betikler altındaki AÇIK düğmesiyle aç."},
    {"romanian", L"Prioritate feroviară",
     L"PORNIT: trenurile de călători trec primele la semnale.",
     L"OPRIT: ordinea la semnale o decide jocul.",
     L"Așteptare maximă: %d s. Un tren de marfă așteaptă trenurile de călători cel mult atât, apoi trece primul.",
     L"Cercetare finalizată: trenurile de călători trec acum primele la semnale.",
     L"Încă necercetat. Cercetează Prioritate feroviară la universitatea de partid; se pornește singură când cercetarea e gata.",
     L"Prioritate feroviară",
     L"Impiegații dau prioritate trenurilor de călători la semnale: un tren de marfă care așteaptă lasă trenul de călători să treacă primul. Pornește-o cu butonul PORNIT din Opțiuni - Scripturi."},
    {"greek", L"Σιδηροδρομική προτεραιότητα",
     L"ΕΝΕΡΓΟ: τα επιβατικά τρένα περνούν πρώτα στα σήματα.",
     L"ΑΝΕΝΕΡΓΟ: τη σειρά στα σήματα την αποφασίζει το παιχνίδι.",
     L"Μέγιστη αναμονή: %d δευτ. Ένα εμπορικό τρένο περιμένει τα επιβατικά το πολύ τόσο, μετά περνά πρώτο.",
     L"Η έρευνα ολοκληρώθηκε: τα επιβατικά τρένα περνούν πλέον πρώτα στα σήματα.",
     L"Δεν έχει ερευνηθεί ακόμη. Ερεύνησε τη Σιδηροδρομική προτεραιότητα στο πανεπιστήμιο του κόμματος· ενεργοποιείται μόνη της όταν ολοκληρωθεί.",
     L"Σιδηροδρομική προτεραιότητα",
     L"Οι κλειδούχοι δίνουν προτεραιότητα στα επιβατικά τρένα στα σήματα: ένα εμπορικό που περιμένει αφήνει το επιβατικό να περάσει πρώτο. Ενεργοποίηση με το κουμπί ΕΝΕΡΓΟ στις Επιλογές - Σενάρια."},
    {"tchinese", L"鐵路優先權",
     L"開：客運列車在號誌處優先通過。",
     L"關：號誌處的通行順序由遊戲決定。",
     L"最長等待：%d 秒。貨運列車最多等待客運列車這麼久，之後由它先行。",
     L"研究完成：客運列車現在在號誌處優先通過。",
     L"尚未研究。請在黨校研究「鐵路優先權」；研究完成後會自動開啟。",
     L"鐵路優先權",
     L"調度員讓客運列車在號誌處優先：等待中的貨運列車讓客運列車先行。在「選項 - 腳本」中按「開」啟用。"},
    {"traditional", L"鐵路優先權",
     L"開：客運列車在號誌處優先通過。",
     L"關：號誌處的通行順序由遊戲決定。",
     L"最長等待：%d 秒。貨運列車最多等待客運列車這麼久，之後由它先行。",
     L"研究完成：客運列車現在在號誌處優先通過。",
     L"尚未研究。請在黨校研究「鐵路優先權」；研究完成後會自動開啟。",
     L"鐵路優先權",
     L"調度員讓客運列車在號誌處優先：等待中的貨運列車讓客運列車先行。在「選項 - 腳本」中按「開」啟用。"},
    {"chinese", L"铁路优先权",
     L"开：客运列车在信号机处优先通过。",
     L"关：信号机处的通行顺序由游戏决定。",
     L"最长等待：%d 秒。货运列车最多等待客运列车这么久，之后由它先行。",
     L"研究完成：客运列车现在在信号机处优先通过。",
     L"尚未研究。请在党校研究“铁路优先权”；研究完成后会自动开启。",
     L"铁路优先权",
     L"调度员让客运列车在信号机处优先：等待中的货运列车让客运列车先行。在“选项 - 脚本”中按“开”启用。"},
    {"japan", L"鉄道の優先通行",
     L"オン：信号で旅客列車が優先されます。",
     L"オフ：信号での順番はゲームが決めます。",
     L"最大待ち時間：%d 秒。貨物列車は旅客列車をこの時間まで待ち、その後先に進みます。",
     L"研究完了：旅客列車が信号で優先されるようになりました。",
     L"まだ研究されていません。党大学で「鉄道の優先通行」を研究してください。研究が終わると自動でオンになります。",
     L"鉄道の優先通行",
     L"指令員が信号で旅客列車を優先します：待機中の貨物列車は旅客列車を先に通します。オプション - スクリプトの「オン」ボタンで有効にします。"},
    {"korean", L"철도 우선권",
     L"켜짐: 신호에서 여객 열차가 먼저 통과합니다.",
     L"꺼짐: 신호에서의 순서는 게임이 정합니다.",
     L"최대 대기: %d초. 화물 열차는 여객 열차를 최대 이만큼 기다린 뒤 먼저 갑니다.",
     L"연구 완료: 이제 여객 열차가 신호에서 먼저 통과합니다.",
     L"아직 연구하지 않았습니다. 당 대학에서 철도 우선권을 연구하세요. 연구가 끝나면 자동으로 켜집니다.",
     L"철도 우선권",
     L"관제사가 신호에서 여객 열차에 우선권을 줍니다: 대기 중인 화물 열차가 여객 열차를 먼저 보냅니다. 옵션 - 스크립트의 켜기 버튼으로 켭니다."},
};

const Texts* volatile g_current = &kTexts[0];

// GetString returns a non-const pointer, so hand out our own writable copies.
wchar_t g_title[128], g_on[256], g_off[256];
const int kHoldValues[5] = {30, 60, 90, 120, 180};
wchar_t g_hold[5][256];
wchar_t g_done[256], g_notyet[384], g_rname[128], g_rdesc[512];

void Apply(const Texts* t)
{
    wcsncpy(g_title, t->title, 127);
    wcsncpy(g_on, t->on, 255);
    wcsncpy(g_off, t->off, 255);
    wcsncpy(g_done, t->done, 255);
    wcsncpy(g_notyet, t->notyet, 383);
    wcsncpy(g_rname, t->rname, 127);
    wcsncpy(g_rdesc, t->rdesc, 511);
    for (int i = 0; i < 5; ++i)
    {
        _snwprintf(g_hold[i], 255, t->hold, kHoldValues[i]);
        g_hold[i][255] = 0;
    }
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
    case ID_TEXT_DONE:   return g_done;
    case ID_TEXT_NOTYET: return g_notyet;
    case research::ID_NAME: return g_rname;
    case research::ID_DESC: return g_rdesc;
    // TEST ONLY button (workshop/test): English is enough.
    case 1957090: return (wchar_t*)L"TEST: logistic_optimization completed = YES";
    case 1957091: return (wchar_t*)L"TEST: logistic_optimization completed = NO";
    case 1957092: return (wchar_t*)L"TEST: rail_priority completed = YES";
    case 1957093: return (wchar_t*)L"TEST: rail_priority completed = NO";
    case 1957094: return (wchar_t*)L"TEST: container_standards completed = YES";
    case 1957095: return (wchar_t*)L"TEST: container_standards completed = NO";
    case 1957096: return (wchar_t*)L"TEST: rail_priority available = YES";
    case 1957097: return (wchar_t*)L"TEST: rail_priority available = NO";
    }
    if (id >= ID_HOLD_FIRST && id < ID_HOLD_FIRST + 5) return g_hold[id - ID_HOLD_FIRST];
    return o_getString(self, id);
}

// research::Map* may hand the game our extended research.ini instead.
template <int M> FILE* h_fopen(const char* p, const char* m) { NoteFile(p); return o_fopen[M](research::MapA(p), m); }
template <int M> FILE* h_wfopen(const wchar_t* p, const wchar_t* m) { NoteFileW(p); return o_wfopen[M](research::MapW(p), m); }
template <int M> errno_t h_fopen_s(FILE** f, const char* p, const char* m) { NoteFile(p); return o_fopen_s[M](f, research::MapA(p), m); }
template <int M> errno_t h_wfopen_s(FILE** f, const wchar_t* p, const wchar_t* m) { NoteFileW(p); return o_wfopen_s[M](f, research::MapW(p), m); }
int h_readFile(const char* p, char** out, unsigned* size, bool b) { NoteFile(p); return o_readFile(research::MapA(p), out, size, b); }

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
