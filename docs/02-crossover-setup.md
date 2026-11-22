# Fordítás macOS-en és tesztelés CrossOver alatt

## 1. Eszközök (Apple Silicon, Homebrew)

```sh
brew install cmake mingw-w64
x86_64-w64-mingw32-g++ --version     # GCC, Windows x86_64 célra
cmake --version                      # >= 3.20
```

Alternatíva (a közösségi vasúti pluginek ezt használják): **llvm-mingw**.
Töltsd le a macOS-es kiadást (`llvm-mingw-<dátum>-ucrt-macos-universal.tar.xz`)
a https://github.com/mstorsjo/llvm-mingw/releases oldalról, csomagold ki, és:

```sh
export LLVM_MINGW=$HOME/toolchains/llvm-mingw-<dátum>-ucrt-macos-universal
```

## 2. Fordítás

```sh
cmake --preset mingw          # vagy: cmake --preset llvm-mingw
cmake --build --preset mingw
# eredmény: build/mingw/wrsr_rail_mod.dll, wrsr_rail_mod.ini, fake_host.exe

# platformfüggetlen unit tesztek natívan a Macen:
cmake --preset host-tests && cmake --build --preset host-tests && ctest --preset host-tests
```

Az első konfiguráláskor a CMake letölti a MinHook v1.3.4-et (rögzített commit).
Offline: `-DFETCHCONTENT_SOURCE_DIR_MINHOOK=/út/a/minhookhoz`.

Gyors ellenőrzés, hogy jó-e a kimenet:

```sh
file build/mingw/wrsr_rail_mod.dll        # PE32+ executable (DLL) x86-64
x86_64-w64-mingw32-objdump -p build/mingw/wrsr_rail_mod.dll | grep -E 'DLL Name|Tsm'
# importok: csak KERNEL32, msvcrt (llvm-mingw-nél api-ms-win-crt-*), VERSION
# exportok: TsmPluginApiVersion, TsmPluginInit, TsmPluginStart
```

## 3. Első teszt: a DLL a játék NÉLKÜL (`fake_host.exe`)

Ez választja szét a hibaforrásokat. A `fake_host.exe` ugyanúgy tölti be a
plugint, mint a TesmioLoader (verziócsekk → `Init` → `Start`), de a játék
nélkül.

1. Másold a `build/mingw/` tartalmát (`fake_host.exe`, `wrsr_rail_mod.dll`,
   `wrsr_rail_mod.ini`) egy mappába a bottle-ön belül, pl.
   `~/Library/Application Support/CrossOver/Bottles/<bottle>/drive_c/wrsrtest/`.
2. Futtasd a bottle-ben (CrossOver → *Run Command*, vagy terminálból):

   ```sh
   CX=/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin
   "$CX/wine" --bottle "<bottle>" 'C:\wrsrtest\fake_host.exe' 'C:\wrsrtest\wrsr_rail_mod.dll'
   ```
3. Nézd meg a `drive_c/wrsrtest/wrsr_rail_mod.log`-ot. Elvárt sorok:

   ```
   attach wrsr_rail_mod 0.1.0 loaded at ...
   init  host API 4 (we were built against 4) ...
   env   wine     <verzió> on Darwin <kernel>  (macOS: CrossOver, x86_64 via Rosetta 2)
   self  MinHook self-test: PASSED
   aob   probe rail speed lookup      not found (0 hits) - feature must stay off
   ```

   Az utolsó sor itt **helyes**: a „játék” ebben a tesztben a `fake_host.exe`.
   A `self ... PASSED` a lényeg: azt bizonyítja, hogy Rosetta 2 alatt működik a
   kódpatchelés (VirtualProtect, végrehajtható memória a közelben, i-cache flush).

## 4. Második teszt: a játékban, TesmioLoaderrel

1. A bottle-ben fut a Windowsos Steam, a játék telepítve van, és Steam fut.
2. TesmioLoader: a release `tesmioloader` mappáját a játék mappájába másold
   (`media_soviet` mellé).
3. `wrsr_rail_mod.dll` + `wrsr_rail_mod.ini` → `tesmioloader/build/plugins/`.
4. Indítás a bottle-ben: `tesmioloader\build\tesmiolauncher.exe` (nem a
   `SOVIET64.exe`, és nem a Steam „Play” gombja).
5. Elvárt eredmény a **játék mappájában** (`SOVIET64.exe` mellett):
   `wrsr_rail_mod.log`, benne az `env exe ... version 1.1.1.13` sor és:

   ```
   aob   probe rail speed lookup      unique at +0x........
   probe rail speed lookup: first constant = 120.0 km/h (matches stock 120 - ...)
   ```

   Ez a sor bizonyítja, hogy a mintakereső a valódi binárisban talál, és a
   memóriaolvasás jó címre mutat. Ugyanez a sor `wrsr_rail` előtaggal a
   `tesmioloader.log`-ban is megjelenik.

## Hibakeresés

| Tünet | Valószínű ok |
|---|---|
| `fake_host` hiba: `LoadLibrary error 126` | hiányzó függőség – nem statikus build. `objdump -p` |
| `fake_host` OK, a játékban nincs log | az injektálás nem futott: launcher-verziócsekk (nem 1.1.1.13), Steam nem fut ugyanabban a bottle-ben, vagy nem a launcherrel indítottad |
| van `attach`, nincs `init` sor | a DLL betöltődött, de nem plugin-hostként (pl. Cheat Engine „Inject DLL”) – ez várt |
| `init ... enabled = 0` | `wrsr_rail_mod.ini`, vagy BOM van a fájl elején |
| `self ... FAILED` | Rosetta/Wine kódpatchelési gond – **ezt jelezd**, mielőtt bármi mást csinálnál, mert minden hook erre épül |
| a log `%TEMP%`-be került | a játékmappa nem írható (a log első `env log file` sora mutatja, hova írt) |

Logot macOS-ről futás közben is olvashatsz: `tail -f` a bottle `drive_c`-jén
belüli fájlra (a fájlt megosztott írásra nyitjuk).
