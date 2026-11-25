# Sortávolság villamosra / motorkocsira – feltárás (játék 1.2.0.8, build 6B05A574)

A vanília 1.2 „sortávolság” csak közúti vonalon kapcsolható. Feltárás memóriából
(különbségi szkennelés) és az exe statikus elemzéséből (`objdump`).

## Adatszerkezet

| Hely | Jelentés |
|---|---|
| `jármű+0x650` | a jármű vonala (pointer) |
| `jármű+0x658` | sortávolság kapcsoló (int, 0/1), járművenként |
| `jármű+0x670` | sortávolság sebességkorlát (float; -1 = nincs) |
| `jármű+0x1748` | típus; `típus+0x2B8` kategória, `típus+0x8650` szállítási osztály, `típus+0x8674` max. sebesség |
| `vonal+0x4` | vonal fajtája (1 = közút); mentéskor negatívan írva, ha a sortávolság be van |
| `vonal+0x240/0x248` | `vector<jármű*>` |

Járművek egy 0x1C40 bájtos rekeszekből álló tömbben ülnek.

## Kód (RVA = cím − 0x140000000)

- `0x4B7980` `lineSpacingOn(line)`: igaz, ha a vonal minden járművén `+0x658 >= 1`.
- `0x4B76C0` `lineMaySpace(line)`: **kapu** – csak `vonal+0x4 == 1` (közút) esetén igaz.
  Ez zárja ki a villamost. Hívói: a körönkénti számítás (`0x4BC1D0`, `0x4B91C0`) és a vonal ablaka.
- `0x4B91C0` vonalankénti számítás (a fő ciklus hívja minden vonalra, `0x32B252`): kiírja a
  járművek `+0x670` sebességkorlátját.
- A korlátot a mozgató kód alkalmazza: `min(típus+0x8674, jármű+0x670)` – közúton `0x6CCDC0`-ban,
  és a `0x6B5B60` útvonaltervezőben, amit a vonatmozgató (`0x6DAFC0`, ez hívja a jelzőfüggvényt) is elér.
- Vonalablak: `0x811FB0` körül, `cmpl $1, line+4` után – ezért nincs jelölőnégyzet villamosnál.

## Teszt (spacing_test = 1)

`lineMaySpace` hookolva: villamos/motorkocsi vonalra (minden jármű kat. 4, osztály 7) igazat ad,
és a járműveken `+0x658 = 1`. Eredmény: a játék 4–6 villamosra 30–35 km/h korlátot számolt,
a közel érő villamos ~5 km/h-val lassított. **A mechanizmus villamoson is működik.**
