# v1.0 – tartalom és tesztlista

## Tartalom

| Rész | Hol |
|---|---|
| Elsőbbség: várakozó tehervonat WAIT-et kap, amíg személyvonat vár ugyanarra a blokkra; a tehervonat kiéheztethető | `src/signal_priority.cpp`, `src/priority_policy.*` |
| Holtpont-védelem: ha **ugyanaz** a személyvonat `max_hold_s` ideig nem halad, a tehervonat mehet (30/60/90/120/180) | `config/wrsr_rail_mod.ini` |
| Beállítások → Szkriptek: BE és KI gomb (felirat a workshopconfig `$OBJECT_SCRIPT` soraiból, nem fordítható), mentésenként; alapból BE | `workshop/item/RailPriority_*.txt`, `src/script_switch.*` |
| Gombnyomásra értesítés a játékos nyelvén (25 nyelvi tábla, ismeretlen nyelv: angol) | `src/lang.*` |
| Ikonok | `workshop/art/*.svg` → `workshop/item/*.png` |

Döntés (2026-11-22): a futó jelzőszkript a **KI** állapotot jelenti, így
az elsőbbség minden mentésben azonnal BE van. Mellékhatás: a Szkriptek
ablak „Jelenleg futó szkriptek: 1” sora KI állásban látszik. Ezt a
Workshop-leírásban el kell mondani.

Nem része (v1.1+): a visszatartott tehervonat jelzője pirosra váltása;
kutatás az egyetemen.

## Tesztlista (M4)

Mac/CrossOver, `tools/deploy-mac.sh` után:

1. [x] Főmenü: log `prio  Scripts menu switch ready`, `lang  texts ready`,
       majd egy `lang  game language file "soviet<Nyelv>.btf" -> ...` sor.
2. [x] Beállítások → Szkriptek (felirat: a `$OBJECT_SCRIPT` szövege, angol): megjelenik a két gomb a két ikonnal.
       Mit ír ki a játék a gombok mellé/fölé (fájlnév? semmi?) – ez dönti el,
       fordítható-e a gombok neve.
3. [x] KI gomb (2026-11-22, Mac): értesítés a játék nyelvén; log `Scripts menu: priority OFF`;
       a tesztpályán a tehervonat újra „versenyez”.
4. [x] BE gomb (2026-11-22, Mac): értesítés; log `priority ON`; a személyvonat megy elsőként.
5. [x] Mentés KI állapotban, újratöltés: KI marad. Mentés BE állapotban: BE.
6. [~] Nyelvváltás: a játék csak induláskor vált nyelvet; induláskor a magyar felismerve. (Angol indulás próbája opcionális.)
7. [x] Régi mentés (a mod előtti) betölt, az elsőbbség BE.
8. [x] A moddal készült mentés betölt a mod nélkül (plugin és szkriptek
       eltávolítva) – különösen KI állapotban mentve (fut a flag-szkript).
9. [x] Windows natívan (2026-11-22).
10. [x] RML 1.0.1 alatt (2026-11-22): 1–4 egyedül, és Rail Operations-szel együtt (RML a hookot elé láncolja, nincs DEGRADED). RailPhysics-szel nem tesztelt.
11. [x] TesmioLoader + Rail Operations (hook elé fűzve, 2026-11-22): log `prio  live (chained in front of another plugin)`,
        és a Rail Operations `signal check hooked` sora is megvan; mindkettő működik.

## Kiadás

1. ~~RML-teszt~~ kész.
2. A repó publikus (GPL-3.0: a forrásnak elérhetőnek kell lennie).
3. `tools/package-release.sh` → `dist/RailPriority/`, és ha `GAME` be van
   állítva, `media_soviet/workshop_wip/RailPriority/`.
4. Feltöltés a játék Műhely-szerkesztőjéből. Az első feltöltés után a játék
   beírja az `$ITEM_ID`/`$OWNER_ID` sorokat a workshopconfig.ini-be: ezt a
   fájlt meg kell tartani (a script a következő csomagoláskor átveszi).
5. Utána a helyi tesztmappa (`workshop_wip/9957001`) törlése, különben a
   gombok kétszer látszanak.

### Workshop-próba (2026-11-22)

- Elem: 3844461371 (feltöltve rejtve). Feliratkozás után a TesmioLoader a
  Workshop-mappából tölti be (`plugin wrsr_rail_mod 1.0.0 from ...\3844461371\plugins`).
- Gombok, KI/BE, elsőbbség: működik a Workshop-példányból.
- Mentés KI állapotban, majd a jelző-szkriptet adó elem hiányzik: a játék
  „Hiányzó műhelyi elem(ek)” kérdést tesz fel. **Piros X** = betöltés
  feliratkozás nélkül: rendben betölt (a futó szkriptet a mentésből veszi).
  `>>` = a feliratkozások listája. A leírás ezt elmondja.
- A szerzőnél a gombok kétszer látszanak (projektmappa + feliratkozás):
  feltöltés után `tools/package-release.sh clean`.
