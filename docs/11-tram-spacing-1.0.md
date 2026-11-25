# Tram & Railcar Spacing 1.0 – külön mod (2026-11-25)

Önálló plugin: `wrsr_tram_spacing.dll` + `wrsr_tram_spacing.ini` (`enabled`), saját napló
(`wrsr_tram_spacing.log`), saját Workshop-elem (`workshop-spacing/`, `tools/package-spacing.sh`).
A Rail Priority (wrsr_rail_mod) nem tartalmazza.

Feltárás: `docs/10-line-spacing-findings.md`. Kód: `src/line_spacing.{h,cpp}`, belépési pont `src/spacing_main.cpp`.

## Mit csinál
1. `lineMaySpace(line)` (RVA 0x4B76C0) host-hook: ha a játék nemet mond, de a vonal minden
   járműve kat. 4 / osztály 7 (villamos, motorkocsi), igazat ad. Ettől a játék körönként
   kiszámolja a sebességkorlátot (`jármű+0x670`), és a mozgató kód alkalmazza.
2. Vonalablak (`cmp dword [line+4],1; jne` a 0x811FB0 környékén): 10 bájt helyén
   `call cave; nop5`; a cave `ShowSpacingBlock(line)`-t hív (közút VAGY villamos/motorkocsi),
   nemnél a régi ugrási célra megy. A jelölőnégyzet, a kattintás és a mentés a játék kódja.
3. Ismeretlen minta → naplóz, nem ír semmit (1.1.1.13-on nincs sortávolság: kimarad).

A teszt- és feltáró kód (`line_probe`, `spacing_test`) törölve.

## Tesztlista (Mac, TesmioLoader, 1.2.0.8 build 6B05A574)
1. [x] Napló: `the line window has the spacing switch`.
2. [x] Villamosvonal ablakában van kapcsoló; bekapcsolva a közel érő villamos lassít.
3. [x] Mentés + visszatöltés: a pipa megmarad, a lassítás is.
4. [x] Tehervonat-vonal ablakában nincs új elem.
5. [x] Buszvonal ablaka változatlanul működik.
6. [x] Motorkocsi-vonal (Komarek): a kapcsoló megjelenik, működik.
7. [ ] Külön DLL-ként, a Rail Priority mellett betöltve.
