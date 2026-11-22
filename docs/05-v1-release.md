# v1.0 – tartalom és tesztlista

## Tartalom

| Rész | Hol |
|---|---|
| Elsőbbség: várakozó tehervonat WAIT-et kap, amíg személyvonat vár ugyanarra a blokkra; a tehervonat kiéheztethető | `src/signal_priority.cpp`, `src/priority_policy.*` |
| Holtpont-védelem: ha **ugyanaz** a személyvonat `max_hold_s` ideig nem halad, a tehervonat mehet (30/60/90/120/180) | `config/wrsr_rail_mod.ini` |
| Beállítások → Szkriptek: BE és KI gomb, mentésenként; alapból BE | `workshop/item/RailPriority_*.txt`, `src/script_switch.*` |
| Gombnyomásra értesítés a játékos nyelvén (25 nyelvi tábla, ismeretlen nyelv: angol) | `src/lang.*` |
| Ikonok | `workshop/art/*.svg` → `workshop/item/*.png` |

Nem része (v1.1+): a visszatartott tehervonat jelzője pirosra váltása;
kutatás az egyetemen.

## Tesztlista (M4)

Mac/CrossOver, `tools/deploy-mac.sh` után:

1. [ ] Főmenü: log `prio  Scripts menu switch ready`, `lang  texts ready`,
       majd egy `lang  game language file "soviet<Nyelv>.btf" -> ...` sor.
2. [ ] Beállítások → Szkriptek: megjelenik a két gomb a két ikonnal.
       Mit ír ki a játék a gombok mellé/fölé (fájlnév? semmi?) – ez dönti el,
       fordítható-e a gombok neve.
3. [ ] KI gomb: értesítés a játék nyelvén; log `Scripts menu: priority OFF`;
       a tesztpályán a tehervonat újra „versenyez”.
4. [ ] BE gomb: értesítés; log `priority ON`; a személyvonat megy elsőként.
5. [ ] Mentés KI állapotban, újratöltés: KI marad. Mentés BE állapotban: BE.
6. [ ] Nyelvváltás a beállításokban: a következő értesítés az új nyelven.
7. [ ] Régi mentés (a mod előtti) betölt, az elsőbbség BE.
8. [ ] A moddal készült mentés betölt a mod nélkül (plugin és szkriptek
       eltávolítva) – különösen KI állapotban mentve (fut a flag-szkript).
9. [ ] Windows natívan: 1–8.
10. [ ] RML alatt: 1–4; Rail Operations-szel és RailPhysics-szel együtt.
