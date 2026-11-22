# wrsr_rail_mod

Natív plugin (`.dll`) a **Workers & Resources: Soviet Republic** játékhoz
(`SOVIET64.exe`, Windows x86_64), macOS-ről keresztfordítva, CrossOver alatt
futtatva. Cél: személyvonat-elsőbbség a jelzőknél (és később tolatási logika).

**Állapot: projektváz (M0).** A DLL betöltődik, logol, önteszteli a MinHookot,
és **csak olvasva** megkeresi a játék sebességkorlát-függvényét. Játékkódot
**nem módosít**.

## Gyors start

```sh
brew install cmake mingw-w64
cmake --preset mingw && cmake --build --preset mingw
# -> build/mingw/wrsr_rail_mod.dll, wrsr_rail_mod.ini, fake_host.exe
```

Részletek, CrossOver-teszt és hibakeresés: [docs/02-crossover-setup.md](docs/02-crossover-setup.md).

## Hogyan töltődik be

TesmioLoader-plugin (API 4): exportálja a `TsmPluginApiVersion`,
`TsmPluginInit` és `TsmPluginStart` függvényeket. Ugyanez a DLL a Republic Mod
Loader (RML) Tesmio-kompatibilitási rétegével is fut. Nincs proxy DLL, nincs
`WINEDLLOVERRIDES`. Részletek: [docs/01-loader-findings.md](docs/01-loader-findings.md).

Betöltéskor a játék mappájába (`SOVIET64.exe` mellé) írja a
`wrsr_rail_mod.log`-ot, és ugyanezt a loader logjába is tükrözi.

## Felépítés

```
CMakeLists.txt, CMakePresets.json   build (mingw / llvm-mingw / host-tests)
cmake/toolchain-mingw-w64-x86_64.cmake
config/wrsr_rail_mod.ini            a plugin beállításai (a DLL mellé másolódik)
src/dllmain.cpp                     exportok + DllMain
src/logger.*                        saját fájllog (+ tükrözés a loader logjába)
src/hooks.*                         host hook (játékkódhoz) / MinHook (minden máshoz) + önteszt
src/aob_pattern.*                   AOB-minták, platformfüggetlen, unit-tesztelt
src/aob_scan.*                      szkennelés PE-szekciókban, egyediség-ellenőrzéssel
src/env_info.*                      játékverzió, PE-időbélyeg, Wine/CrossOver/host OS
tools/fake_host.cpp                 a plugin betöltése a játék nélkül (CrossOver-teszt)
tests/aob_tests.cpp                 natív unit tesztek
third_party/tesmio/                 TesmioLoader plugin-fejlécek (GPL-3.0)
docs/                               loader-elemzés, setup, előzmények és ütemterv
```

## Mielőtt továbbmész

Olvasd el: [docs/03-prior-art-and-roadmap.md](docs/03-prior-art-and-roadmap.md).
A tolatás/körüljárás nagy részét a *Rail Operations* plugin már megcsinálta;
a jelzőprioritást viszont senki.

## Licenc

A beépített TesmioLoader-fejlécek GPL-3.0 alatt vannak, így a lefordított DLL
GPL-3.0 mű. Ha terjeszted, a forrást is elérhetővé kell tenned.
