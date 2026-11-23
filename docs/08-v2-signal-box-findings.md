# v2.0 forgalmi iroda – megvalósíthatósági próbák (2026-11-23)

Cél: épület a vasút mellett, 1500 m-es hatókör a pályahálózaton, min. 4
munkás; a hatókörön belüli jelzőkön él az elsőbbség.

## Amit megtudtunk
- `building.ini`: nincs kulcsszó, amivel szkriptet/gombot/szöveget lehetne
  egy épület ablakához kötni (335 kulcsszó a stock + feliratkozott modok
  épületeiben). Épületablakba gomb csak a játék UI-jának visszafejtésével.
- Szkript-API: az épületek bejárhatók és kiolvashatók
  (`Building_GetNumberOfBuildings(n)`, `bui.GetDataByIndex(i)`,
  `bui.nValidRead`, `bui.nType`, `bui.vPosition`, `bui.nWorkersNum`;
  `?` = egyenlő, `&` = és, `for (i=0, i<n, i=i+1)`).
- Szkript -> plugin adatcsatorna (változók a memóriában): **nem sikerült**.
  - A szkript literáljai egy állandótáblában vannak (float, megjelenési
    sorrendben) – ezt az első próba megtalálta.
  - Futás közben számolt érték (731.25 * 3 = 2193.75) sehol nem volt a
    memóriában (float/double, minden írható régió), sem a szkriptobjektum
    négy vektora (1 bájtkód-szerű, 3 mutatótömb: 74 / 20 / 427 elem)
    elemeinek első 0x40 bájtjában.
  - Szkriptobjektum: `world+0x13BB8` listában; `+0xB8` fájlnév (wide),
    `+0x08/+0x20/+0x38/+0x50` std::vector, `+0xE0` a szkript órája (float).
- A próbakód (office_probe, TestOffices szkript) eltávolítva; a git
  előzményekben megvan (8073d3f..c296c61).

## Lehetséges folytatás
1. A DLL maga járja be az épületeket (Rail Operations-szerű visszafejtés).
2. A szkript futtatójának visszafejtése (a változók tárolása).
