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
