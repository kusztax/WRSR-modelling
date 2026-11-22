# Mit vár el a loader a plugintől?

Elemzett források (2026-11-22-i állapot):

| Repo | Mi ez | Ami nekünk számít |
|---|---|---|
| [MaxLegend/TesmioLoader](https://github.com/MaxLegend/TesmioLoader) | launcher + loader DLL + plugin host | a plugin-ABI (`tesmio_api.h`), betöltési sorrend, hook-szolgáltatások |
| [Ultimate-Universe/WRSR-RepublicModLoader](https://github.com/Ultimate-Universe/WRSR-RepublicModLoader) (RML) | másik loader, Workshopról | saját API + **Tesmio API 3/4 kompatibilitási réteg** |
| [veryinsanee/RepublicMP](https://github.com/veryinsanee/RepublicMP) | multiplayer mod (alfa) | **maga is TesmioLoader-plugin** (ugyanazok az exportok); a host `InstallInlineHook`/`PatchIat`-jét és egy DXGI vtable-hookot használ, hardkódolt RVA-kkal; nem loader |

Nem két keretrendszer van tehát, hanem egy plugin-ABI (Tesmio) és két loader,
amelyik ezt hostolja (TesmioLoader, RML). A RepublicMP csak egy nagyobb
felhasználó, jó minta ImGui-overlayre és IAT-hookra a `C3DDLL64.dll` felé.

## A belépési pontok (TesmioLoader API 4, RML alatt is így fut)

```cpp
extern "C" __declspec(dllexport) unsigned TsmPluginApiVersion(void);              // kötelező
extern "C" __declspec(dllexport) int TsmPluginInit(const TsmHost*, TsmPluginInfo*); // kötelező
extern "C" __declspec(dllexport) int TsmPluginStart(void);                         // opcionális
```

- A loader a `plugins\` mappában lévő DLL-ek **exporttábláját** nézi, futtatás
  nélkül. Ha a két kötelező export nincs meg: „not a tesmioloader plugin”.
- `TsmPluginApiVersion` csak a konstanst adja vissza – a loader ez alapján dönti
  el, egyáltalán meghívja-e az `Init`-et (elfogadott tartomány most: 3..4).
- `Init`: konfiguráció olvasása, szolgáltatások `provide`-olása. **Hookolni tilos.**
- `Start`: minden plugin `Init`-je után fut → itt `consume` és hookok telepítése.
- Nem nulla visszatérés = „semmit nem hookoltam, tölts ki”. Élő hook után ez
  összeomlást okoz (a játék felszabadított memóriába ugrik).
- A DLL neve = a plugin neve; a beállítás `plugins\<név>.ini`, `[<név>]`
  szekció, UTF-8 **BOM nélkül**, ASCII értékek.
- Minden plugin saját CRT-vel (`/MT` ill. MinGW-nél `-static`), a határon csak
  POD és C-string megy át, semmit nem szabad a másik oldalon felszabadítani.

A `TsmHost` tábla (amit `Init` kap): `exeBase/exeSize` (ASLR van, mindig RVA +
base), `log`, `findIatSlot`/`patchIat`, `installInlineHook` (elvárt bájtokat
ellenőriz, ≥14 bájt), `allocNear` (±2 GB), `readablePtr`, `faultFilter`,
`configInt/String`, `provide/consume`, `vfsRoot`.

## Hogyan kerül be a DLL a játékba – és mit jelent ez CrossOver alatt

Mindkét loader ugyanígy injektál: `CreateProcess(SUSPENDED)` →
`VirtualAllocEx` + `WriteProcessMemory` a DLL útvonalával →
`CreateRemoteThread(LoadLibraryW)` → `ResumeThread`. **Nincs proxy DLL**
(`version.dll`/`winmm.dll`/`dinput8.dll`), ezért **`WINEDLLOVERRIDES` nem kell**.
Az a brief-ben szereplő megoldás csak akkor lenne releváns, ha saját,
loader nélküli injektálást csinálnánk – erre most nincs ok.

Wine/Proton alatt a TesmioLoader dokumentáltan működik (a wrsr-agents
tudásbázis szerint a Linuxos Proton prefixben a pluginek változatlanul futnak).
**CrossOver + Rosetta 2 alatt ezt senki nem dokumentálta** – ezt neked kell
először igazolnod. Ezért van a `fake_host.exe` (lásd `02-crossover-setup.md`).

**Igazolva (2026-11-22), `fake_host`, a játék nélkül:** CrossOver (Wine 11.0),
macOS Darwin 27.2.0, Apple Silicon (Rosetta 2), Homebrew MinGW GCC 16.2.0
(UCRT) build. A DLL betöltődik, `Init`/`Start` lefut, a MinHook önteszt
PASSED – a kódpatchelés Rosetta alatt működik.

**Igazolva (2026-11-22), a játékban:** TesmioLoader b0.4.3 (Workshop
3773169177) `--nogui`-val indítva ugyanebben a bottle-ben. A loader injektál
(főmenü: `tesmioloader v. 0.4.3`), a plugin `Init`/`Start` lefut a
`SOVIET64.exe` 1.1.1.13-ban (PE stamp `0x6A8D4087`), MinHook önteszt PASSED,
és a railspeed `SIG_C` mintája 1.1.1.13-on is egyedi: `+0x3BCE90`, a
konstans 120.0. A `SOVIET64.exe`-ben nincs VERSIONINFO – a build azonosítója
a PE timestamp.

Gyakorlati megjegyzések a CrossOveres telepítéshez:
- A Workshopos TesmioLoader kiadás lapos: `tesmioloader/{tesmiolauncher.exe,
  tesmioloader.dll, tesmioloader.ini, plugins/, vfs/}` – nincs `build/`.
- A loader a Workshop-mappát is végigjárja DLL-ekért; ezért egy
  feliratkozott, mástól származó plugin (pl. `achieve.dll`) is betöltődik.
  Tiszta teszthez: `[plugins]` szekció, `achieve = 0`.

Figyelem: a TesmioLoader launcher **verzióellenőrzést** végez; a mostani verzió
csak a `1.1.1.13` buildet (PE timestamp `0x6A8D4087`) fogadja el. A pluginünk
logja kiírja a játék verzióját és PE-időbélyegét – ha nem egyezik, a launcher
el sem indítja a játékot.

## MinHook vs. a loader hookja

A két ismert vasúti plugin (RailPhysics, Rail Operations/railreverse) **nem
MinHookot** használ, hanem a host `installInlineHook`-ját, ugyanarra a
vasúti update-függvényre. RML ezeket a hookokat **láncolja** (több plugin
ugyanazon a címen). Egy MinHook-patch ezen a láncon kívül esik: RML
„unmanaged change”-nek látja, és könnyen eltöri a másik plugint vagy minket.

Szabály a projektben (`src/hooks.h`):
- **játékkód → host hook** (elvárt bájtokkal),
- **MinHook → csak saját/OS-függvény**, önteszt, vagy loader nélküli futás.

## MinGW-specifikus csapdák

- x64 Windowson **egyetlen hívási konvenció** van (Microsoft x64:
  RCX/RDX/R8/R9 + XMM0–3, 32 bájt shadow space). A `__fastcall`/`__thiscall`/
  `__stdcall` x64-en **hatástalan**; a MinGW alapból ezt az ABI-t használja.
  A valódi kockázat a **C++ ABI**: MinGW és MSVC osztály-elrendezése, vtable-je,
  name manglingje eltér. Ezért játékobjektumot sosem modellezünk C++ osztállyal
  virtuális függvényekkel; tagfüggvényt sima függvénypointerrel hívunk, ahol a
  `this` az első paraméter (`using Fn = float(*)(void* self, int)`).
- GCC-MinGW **nem ismeri a `__try/__except`-et**. Játékmemória olvasása előtt
  `readablePtr`, kivételkezelésre VEH (`AddVectoredExceptionHandler`) – a
  railreverse is így csinálja.
- `DllMain` C++-ban `extern "C"` legyen, különben a CRT üres alapértelmezettje
  fut helyette, csendben.
- Statikus linkelés kötelező: a játékmappában nincs `libstdc++-6.dll`, és a
  hiányzó függőség csak „failed to load (126)” formában látszik.
