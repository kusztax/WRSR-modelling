# Előzmények és javasolt irány

Ez a fejezet azért készült, mert a brief két fő célja közül **az egyiket
nagyrészt már megcsinálta valaki** – és ezt jobb most tudni, mint három hónap
Ghidra után.

## Ami már létezik

| Plugin | Mit csinál | Licenc / állapot |
|---|---|---|
| [pautov-ya/WRSR-RailReverse](https://github.com/pautov-ya/WRSR-RailReverse) („Rail Operations”, Workshop 3823129138) | irányváltáskor **a mozdony tolja** a szerelvényt; **tolási sebesség 25 km/h-ra korlátozva**; mindkét végén mozdonyos vonat a vanilla módon fordul; **körüljárás** útvonal-megállókkal (Uncouple + Stop and turn round); jelzők átengedik a mozdonyt a kocsijaihoz | GPL-3.0, aktív (0.6.2, 2026-11-21), 1.1.1.9 és 1.1.1.13, RML elsődleges |
| [TheRealMeowMeow00/WRSR_RailPhysics](https://github.com/TheRealMeowMeow00/WRSR_RailPhysics) | a vasúti fizika cseréje (gyorsulás, fékezés, lejtő, ív); **részletes RE-dokumentáció** a vonat- és blokkstruktúrákról | GPL-3.0 |
| [SosseTurner/Railspeed](https://github.com/SosseTurner/Railspeed-Plugin-for-Workers-and-Resources) | a pálya sebességkorlátjainak emelése, AOB-mintákkal | GPL-3.0 |

### Mit jelent ez a briefre nézve

**2. cél, 1. fázis (tolatási büntetés, `max_speed * 0.5`)** – a vanilla játékban
ez a helyzet **nem fordul elő**: irányváltáskor a mozdony „átugrik” a
szerelvény másik végére, tehát mindig elöl van. Tolás csak a Rail Operations
pluginnel létezik – és az már korlátozza is a sebességet (25 km/h). Ha ezt
magadtól újraírod, akkor (a) először a tolást kellene megcsinálnod, ami a
nehezebb rész, és (b) ugyanazt a vasúti update-et hookolnád, mint ők → ütközés.
A „-50%” egyébként sem realisztikus: a valós tolatási korlát fix érték
(25–40 km/h), nem a pályasebesség fele.

**2. cél, 2. fázis (automatikus körüljárás)** – a Rail Operations manuálisan,
útvonal-megállókkal már tudja. Az *automatikus* útvonalkeresés hiányzik, de ez
egy hónapokig tartó feladat egy olyan játékmotorban, ahol a pathfindert még
senki nem fejtette vissza nyilvánosan.

**1. cél (személyvonat-elsőbbség a jelzőknél)** – **ezt senki nem csinálta
meg**, legalábbis nyilvános repóban nem találtam. Ez az, ahol valódi új
értéket tudsz hozzáadni.

## Javaslat

1. **A jelzőprioritás legyen az első és fő cél.** Eredeti, jól körülhatárolt,
   és nem ütközik a meglévő pluginekkel (más függvényt érint: a blokk-foglalást,
   nem a mozgás-update-et).
2. A tolatási logikánál **ne írj párhuzamos implementációt**. Ha a Rail
   Operations viselkedése nem tetszik (pl. a 25 km/h helyett más érték kell,
   vagy vezérlőkocsi-felismerés hiányzik), az egy PR vagy issue az ő repójukban
   – vagy egy kiegészítő plugin, ami az ő állapotukat olvassa. Ez hetek helyett
   napok.
3. Csak akkor menj a körüljárás automatizálása felé, ha az 1. pont kész és
   stabil a játékban.

## Kiinduló ismeretek a jelzőprioritáshoz (RailPhysics RE, 1.1.1.9)

Ezek **adatok, nem kódcímek** – a kódcímek buildenként mozognak, ezért mintával
keressük őket. Mindent ellenőrizni kell 1.1.1.13-on Cheat Engine-nel.

| Hol | Offset | Mi |
|---|---|---|
| vonat (jármű) | `+0x3A0` | kocsik vektora (begin, end `+8`) |
| vonat | `+0x398` | kocsin: az őt húzó jármű; 0 a vezető egységen |
| vonat | `+0x4C4` | byte: a kocsi fordítva van csatolva |
| vonat | `+0x528` | a blokk/lánc, amelyhez a vonat **épp kapcsolódik / közeledik** (blokkfoglalás) |
| vonat | `+0x4F0` | a lánc, amelyhez megérkezett |
| vonat | `+0x7A0` | byte: menetirány a szegmensen (node0→node1 / fordítva) |
| vonat | `+0xD24` / `+0xD28` | sebesség / cél-sebesség, km/h |
| vonat | `+0x1708` | jármű-típus pointer |
| típus | `+0x294` | kategória: 3 kocsi, 4 mozdony, 5 szolgálati |
| típus | `+0x8600` | szállítási osztály (**7 = utas**) |
| típus | `+0x8624` | végsebesség |
| lánc (blokk) | `+0x318 → +0x360` | típus-azonosító (300 = sima pályablokk) |
| lánc | `+0x604` | az épp bent lévő vonat távolsága (a blokk egyszerre egy vonaté) |
| lánc | `+0x610/+0x618` | POI-tábla (stride `0x21B8`); POI típus 10/12/16 = jelzőfajták |
| lánc | `+0xC70/+0xC78` | az itt álló járművek vektora |

Ebből a „személyvonat-e” kérdés már megválaszolható: van-e a szerelvényben
olyan egység, amelynek típusán `+0x8600 == 7`.

A nyitott kérdés – és a következő RE-feladatod – az, **melyik függvény dönti
el, hogy egy várakozó vonat megkapja-e a blokkot** (aki beírja a `+0x528`-at /
foglalja a láncot). Ott kell a hook: ha a kérelmező tehervonat, és a blokk
egy másik bejövő ágán személyvonat várakozik, a kérést elutasítjuk.

Kockázat, amit előre végig kell gondolni: **holtpont**. Ha a személyvonat maga
is egy olyan blokkra vár, amit a visszatartott tehervonat szabadítana fel,
mindkettő örökre áll. Kell egy időkorlát (pl. N másodperc után a tehervonat
mehet) – ez az első verzióba is kerüljön bele.

## Mérföldkövek (javasolt)

| # | Kész, ha… | Becsült munka |
|---|---|---|
| M0 | `fake_host` PASSED CrossOverben, a játékban megjelenik a `probe ... 120.0` sor | 1 este (ez a repo) |
| M1 | Cheat Engine-nel igazolva 1.1.1.13-on: vonat `+0x3A0`, típus `+0x8600`, vonat `+0x528` | 1–2 hét |
| M2 | Ghidrában megvan a blokkfoglaló függvény + egyedi AOB-minta; csak **logoló** hook (ki kér, mit, mikor) | 2–4 hét |
| M3 | Prioritás egyetlen kereszteződésen, időkorláttal, ini-kapcsolóval | 1–2 hét |
| M4 | Teszt RailPhysics-szel és Rail Operations-szel együtt, RML és TesmioLoader alatt | 1 hét |

Minden mérföldkő végén legyen egy mondat a logban, ami igazolja. Ha valami nem
logolható, nem tudod bizonyítani, hogy működik.
