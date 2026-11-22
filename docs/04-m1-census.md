# M1 – vonat- és blokkfelmérés (census)

Cél: igazolni 1.1.1.13-on a `src/game_offsets.h` mezőit, és megfigyelni,
hogyan foglalják a vonatok a blokkokat. Ez a bemenet a jelzőprioritás
hookjához (M2). **A játék viselkedése nem változik.**

## Mit csinál

1. Mintával megkeresi a globális lánc-vektort (minden blokk, állomás, depó a
   pályán) – csak olvas.
2. `train_census = 1` esetén a loader hookjával (nem MinHookkal) elfogja a
   vasúti gyorsítási segédfüggvényt (`float divisor(vehicle*)`). A detour
   előbb az eredetit hívja, az eredményt változatlanul adja vissza, és utána
   – a játék saját szálán – logol.

Két független minta (a függvény eleje és a hívási helye) kell egyezzen,
különben nincs hook. A loader az első 14 bájtot is összeveti a várt
értékkel, eltérés esetén megtagadja a hookot.

## Teszt

1. `git pull`, `cmake --build --preset mingw`, a `wrsr_rail_mod.dll` és
   `wrsr_rail_mod.ini` másolása a `tesmioloader/plugins/` mappába.
2. Játék indítása a launcherrel; olyan mentés, ahol **személy- és tehervonat
   is jár**, lehetőleg jelzős elágazással.
3. 3–5 perc normál sebességen, aztán kilépés.

## Mit nézz a logban

| Sor | Ha jó | Ha rossz |
|---|---|---|
| `rail  chain vector at +0x... (N sites agree)` | megvan a blokklista | a minta 1.1.1.13-on más – M1 Ghidrával |
| `rail  divisor fn confirmed by its call site` + `hook ... ok (host)` | a hook él | `REFUSED`: a 14 bájt eltér, semmi nem íródott |
| `rail  train #N NEW lead: cat 4 ...` | `cat` 4 (mozdony) vagy 3/5, `top` értelmes km/h | szemét számok = eltolódott offset |
| `consist: ... => PASSENGER` | a személyvonataidnál PASSENGER, a teher­vonatoknál nem | ha fordítva/mindig ugyanaz: `+0x8600` rossz |
| `rail  chains: ... 300 xN(...)` | sok 300-as (sima blokk) típus | nincs 300: a típus-azonosítás más |
| `train #N next ... -> ...` | blokkváltások menet közben | soha nem változik: `+0x528` rossz |

Korlát: a `divisor` csak a rail update egy ágából hívódik – egy álló
(jelzőnél váró) vonatot lehet, hogy addig nem lát, amíg el nem indul. Ez a
következő lépés kérdése: melyik függvény dönt a várakozásról.

## Eredmény – 1. futás (2026-11-22, 1.1.1.13, CrossOver, Test map, 3 vonat)

Igazolva:
- A hook él: `divisor` a `+0x698DF0` címen (1.1.1.9-ben `+0x698C50` volt), a
  lánc-vektor a `+0x9E6A18` címen. Mindkettő mintával lett megtalálva.
- `V_TYPE +0x1708`, `T_CATEGORY +0x294` (mozdony = 4), `T_TOPSPEED +0x8624`
  (115 km/h, hihető), `V_WAGON_VEC +0x3A0`, `V_LEADER +0x398` (0 a vezető egységen).
- **Személyvonat-felismerés működik:** `T_TRANSPORT +0x8600 == 7` a
  személykocsin; 2 vonat PASSENGER, 1 freight – egyezik a valósággal.
  A mozdony saját osztálya 18.

Új tudás, ami **módosítja a tervet**:
- `+0x528` nem blokkfoglalás: az épp megközelített **állomás**-láncot mutatja
  (0 és 1 típus), kb. 0,4–2 s-ig, utána nullázódik.
- Jelzők mellett sincs 300-as típusú lánc a vektorban: a **jelzők nem
  láncok**, hanem a pálya csomópontjain ülnek (Rail Operations RE:
  `node+0xB8` → jelző, `+0x14` fajta, `+0x1A8/+0x1A9` „blokk szabad” jelzők,
  `+0x1AA` láncjelző).
- A döntés helye a játék jelzőellenőrző függvénye: `char maySignalPass(lead)`
  (Rail Operations: `FUN_1406ac6e0` 1.1.1.9-en) – ennek kell a hook.
- A vasúti update **legalább két szálon** fut (a logban két szálazonosító
  váltakozik). Az elsőbbség logikájának szálbiztosnak kell lennie.

Nyitott:
- A sebesség (`+0xD24`) minden mintavételnél 0.0 volt. Vagy a `divisor` csak
  induláskor hívódik, vagy az offszet elcsúszott. A 2. futás 30 mp-es
  `stat` sorai ezt eldöntik.
- `+0x4F0` mindig null volt a mintavételkor.

## 2. futás – jelzőfelmérés

`signal_census = 1`: log-only hook a jelzőellenőrzőn (`rail  sig` sorok:
WAIT/GO váltások vonatonként, várakozási idővel) és `rail  stat` sorok 30
másodpercenként (hívásszám, sebesség).

## Eredmény – 2. futás (jelzőfelmérés, kétkörös tesztpálya)

Igazolva 1.1.1.13-on:
- A jelzőellenőrző hook él: `+0x6AC880` (Rail Operations mintája).
- `V_SPEED +0xD24` jó: 115 km/h-s utazósebesség, fékezés, 0 állva.
  Az 1. futás 0.0-s értékei azért voltak, mert ott csak állomáson álló
  vonatot mintáztunk.
- A `divisor` képkockánként hívódik (≈ 60/s vonatonként), a jelzőellenőrző
  60–120/s vonatonként.

**A vanilla döntés** (6 ismétlés, mind ugyanaz):
- Amikor a közös blokk felszabadul, a játék **ugyanabban a képkockában
  mindkét várakozó vonatnak GO-t mond** (pl. 21:46:35.940 FRT GO és
  21:46:35.956 PAX GO).
- Mindkettő elindul, a személyvonat ~0,7 s múlva **újra WAIT-et kap**
  (v ≈ 9 km/h): a blokkot a tehervonat foglalta le.
- Ez **nem érkezési sorrend**: a személyvonat 26–28 s-ot várt, a tehervonat
  csak 11–13 s-ot, mégis a tehervonat nyert.
- Valószínűleg a feldolgozási sorrend dönt (a vonatlista sorrendje). Ezt
  nem kell tovább kutatni: a mod a WAIT→GO átmenetnél avatkozik be.

Következmény a tervezéshez: a beavatkozási pont a **várakozó** tehervonat
GO-ja. Ha ilyenkor egy másik jelzőnél személyvonat vár ugyanarra a blokkra,
a tehervonat WAIT-et kap. Mozgó vonatot a mod soha nem állít meg.
Részletek: `src/priority_policy.h`, `src/signal_priority.cpp`.
